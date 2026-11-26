"""Audio-analysis Chart Format v4 & bespoke cover generator for Sakura Official Pack · Vol. 04.

Reads the 6 curated real Suno MP3 tracks from git-ignored download/songs/, transcodes reproducible
music.ogg via ffmpeg (never synthesizing placeholder audio), estimates BPM & beat grid via
percussive onset autocorrelation, separates harmonic/percussive stems via STFT HPSS, authors
multi-difficulty Chart Format v4 files (A/S/D/F + Spacebar thumb bar + Link chords + Synergy
modifiers + curved Sliders + SV + section-peak musical stage_events), and renders bespoke luminous
night-sakura cover.png artwork.
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import subprocess

import numpy as np
from PIL import Image, ImageDraw, ImageFilter
from scipy import ndimage, signal

ROOT = Path(__file__).resolve().parents[1]
SONGS_DIR = ROOT / "download" / "songs"
CHARTS_DIR = ROOT / "resources" / "charts"
AN_SR = 24000
HOP = 240  # 10 ms
FPS = AN_SR / HOP
N_FFT = 1024


@dataclass(frozen=True)
class DiffSpec:
    name: str
    chart_file: str
    level: float
    grid: int          # 16th-step quantum: 4=beat, 2=8th, 1=16th
    kb_density: float  # target keyboard notes / sec
    ms_density: float  # target mouse notes / sec
    thumb_ratio: float # share of accented downbeats using lane 4 (Space)
    synergy: bool      # enable Chart v3/v4 link_id & require_lane
    sv: bool           # enable scroll-velocity modulation


@dataclass(frozen=True)
class TrackSpec:
    chart_id: str
    source_file: str
    title: str
    artist: str
    motif_style: str
    tags: tuple[str, ...]
    palette: tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]
    diffs: tuple[DiffSpec, ...]


PACK_SPECS: tuple[TrackSpec, ...] = (
    TrackSpec(
        "sakura_memories",
        "Sakura Memories.mp3",
        "Sakura Memories",
        "Sakura Soundworks",
        "ribbons",
        ("original", "melodic", "nostalgic", "v4-stage"),
        ((18, 16, 44), (250, 174, 210), (255, 224, 158)),
        (
            DiffSpec("Normal", "normal.json", 4.0, 4, 1.70, 0.52, 0.15, False, False),
            DiffSpec("Hard", "hard.json", 7.5, 2, 3.20, 0.96, 0.20, True, False),
            DiffSpec("Expert", "expert.json", 10.5, 1, 4.85, 1.46, 0.24, True, True),
        ),
    ),
    TrackSpec(
        "moonlit_prelude",
        "【Intro】.mp3",
        "月夜序曲 · Prelude",
        "Sakura Soundworks",
        "crescent",
        ("original", "prelude", "atmospheric", "v4-stage"),
        ((12, 18, 50), (168, 206, 255), (252, 196, 224)),
        (
            DiffSpec("Easy", "easy.json", 3.0, 4, 1.35, 0.42, 0.14, False, False),
            DiffSpec("Normal", "normal.json", 5.5, 2, 2.15, 0.65, 0.17, False, False),
            DiffSpec("Hard", "hard.json", 8.5, 2, 3.55, 1.08, 0.22, True, True),
        ),
    ),
    TrackSpec(
        "sakura_pulse",
        "樱落呼吸.mp3",
        "樱落脉动",
        "Sakura Soundworks",
        "pulse_rings",
        ("original", "electronic", "sakura", "v4-stage"),
        ((22, 16, 46), (248, 162, 204), (146, 232, 248)),
        (
            DiffSpec("Normal", "normal.json", 4.5, 2, 1.95, 0.58, 0.16, False, False),
            DiffSpec("Hard", "hard.json", 7.5, 2, 3.25, 0.98, 0.20, True, False),
            DiffSpec("Expert", "expert.json", 10.5, 1, 4.90, 1.48, 0.24, True, True),
        ),
    ),
    TrackSpec(
        "shattered_drive_f",
        "破碎推进到F.mp3",
        "破碎推进到F",
        "Sakura Soundworks",
        "prism_drive",
        ("original", "electronic", "drive", "v4-stage"),
        ((16, 20, 48), (138, 218, 255), (250, 164, 212)),
        (
            DiffSpec("Normal", "normal.json", 5.5, 2, 2.15, 0.65, 0.16, False, False),
            DiffSpec("Hard", "hard.json", 9.0, 2, 3.75, 1.14, 0.22, True, True),
            DiffSpec("Expert", "expert.json", 12.0, 1, 5.50, 1.62, 0.25, True, True),
        ),
    ),
    TrackSpec(
        "sakura_sprint",
        "樱语疾行.mp3",
        "樱语疾行",
        "Sakura Soundworks",
        "gale_stream",
        ("original", "drum-and-bass", "high-speed", "v4-stage"),
        ((24, 14, 42), (255, 152, 194), (255, 228, 152)),
        (
            DiffSpec("Normal", "normal.json", 6.0, 2, 2.35, 0.72, 0.17, False, False),
            DiffSpec("Hard", "hard.json", 9.5, 2, 3.90, 1.18, 0.23, True, True),
            DiffSpec("Expert", "expert.json", 13.0, 1, 5.95, 1.72, 0.26, True, True),
        ),
    ),
    TrackSpec(
        "shattered_threshold",
        "破碎阈值.mp3",
        "破碎阈值",
        "Sakura Soundworks",
        "shatter_core",
        ("original", "hardcore", "boss", "v4-stage"),
        ((30, 10, 38), (255, 118, 164), (162, 234, 255)),
        (
            DiffSpec("Hard", "hard.json", 10.0, 2, 4.05, 1.24, 0.24, True, True),
            DiffSpec("Expert", "expert.json", 13.5, 1, 6.15, 1.78, 0.27, True, True),
        ),
    ),
)


def decode_mono(source: Path) -> np.ndarray:
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-nostdin", "-i", str(source), "-map", "0:a:0",
         "-f", "f32le", "-acodec", "pcm_f32le", "-ac", "1", "-ar", str(AN_SR), "-"],
        capture_output=True, check=True,
    ).stdout
    return np.frombuffer(raw, dtype="<f4").astype(np.float64)


def encode_ogg(source: Path, dest: Path, title: str, artist: str) -> None:
    tmp = dest.with_suffix(".tmp.ogg")
    try:
        subprocess.run(
            ["ffmpeg", "-v", "error", "-nostdin", "-y", "-i", str(source), "-map", "0:a:0", "-vn",
             "-map_metadata", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
             "-ac", "2", "-ar", "44100", "-c:a", "libvorbis", "-q:a", "4.5",
             "-metadata", f"title={title}", "-metadata", f"artist={artist}", str(tmp)],
            check=True,
        )
        tmp.replace(dest)
    finally:
        tmp.unlink(missing_ok=True)


def stft_mag(y: np.ndarray) -> np.ndarray:
    win = signal.get_window("hann", N_FFT).astype(np.float32)
    padded = np.pad(y.astype(np.float32), (N_FFT // 2, N_FFT // 2))
    frames = 1 + (len(padded) - N_FFT) // HOP
    out = np.empty((N_FFT // 2 + 1, frames), dtype=np.float32)
    for s in range(0, frames, 2048):
        e = min(frames, s + 2048)
        idx = np.arange(N_FFT)[None, :] + HOP * np.arange(s, e)[:, None]
        out[:, s:e] = np.abs(np.fft.rfft(padded[idx] * win, axis=1)).T
    return out


def estimate_bpm_and_offset(perc_flux: np.ndarray) -> tuple[float, float]:
    corr = signal.correlate(perc_flux - perc_flux.mean(), perc_flux - perc_flux.mean(), mode="full")
    corr = corr[len(perc_flux) - 1:]
    best_bpm, best_score = 140.0, -1.0
    for bpm in np.arange(100.0, 196.5, 0.5):
        lag = 60.0 * FPS / bpm
        score = 0.0
        for mult, w in ((1, 1.0), (2, 0.65), (4, 0.45)):
            idx = int(round(lag * mult))
            if idx < len(corr):
                score += w * float(corr[idx])
        if score > best_score:
            best_score, best_bpm = score, float(round(bpm * 2.0) / 2.0)
    beat_sec = 60.0 / best_bpm
    phases = np.linspace(0.0, beat_sec, 60, endpoint=False)
    best_phase, best_p_val = 0.0, -1.0
    max_t = len(perc_flux) / FPS
    for ph in phases:
        beats = np.arange(ph, max_t, beat_sec)
        frames = np.clip(np.round(beats * FPS).astype(int), 0, len(perc_flux) - 1)
        val = float(perc_flux[frames].sum())
        if val > best_p_val:
            best_p_val, best_phase = val, float(ph)
    return best_bpm, best_phase


def analyze_audio(y: np.ndarray) -> dict:
    duration = len(y) / AN_SR
    mag = stft_mag(y)
    freqs = np.fft.rfftfreq(N_FFT, 1.0 / AN_SR)
    harm = ndimage.median_filter(mag, size=(1, 21), mode="nearest")
    perc = ndimage.median_filter(mag, size=(17, 1), mode="nearest")
    h2, p2 = harm.astype(np.float64) ** 2, perc.astype(np.float64) ** 2
    tot = h2 + p2 + 1e-12
    harm_m = mag * (h2 / tot)
    perc_m = mag * (p2 / tot)

    def flux_band(m: np.ndarray, lo: float, hi: float) -> np.ndarray:
        b = np.log1p(80.0 * m[(freqs >= lo) & (freqs < hi)].astype(np.float64))
        f = np.maximum(0.0, np.diff(b, axis=1)).sum(axis=0)
        return np.concatenate([[0.0], f])

    kick_f = flux_band(perc_m, 45, 145)
    snare_f = flux_band(perc_m, 150, 900)
    hat_f = flux_band(perc_m, 3000, 11000)
    mel_f = flux_band(harm_m, 220, 4200)
    bpm, grid0 = estimate_bpm_and_offset(kick_f + 0.8 * snare_f + 0.4 * hat_f)

    mel_bins = (freqs >= 220) & (freqs <= 2600)
    mel_freqs = freqs[mel_bins]
    mel_spec = harm_m[mel_bins].astype(np.float64) + 1e-9
    centroid_hz = (mel_spec * mel_freqs[:, None]).sum(axis=0) / mel_spec.sum(axis=0)
    norm_pitch = np.clip((np.log2(np.maximum(centroid_hz, 220.0) / 220.0)) / 3.2, 0.0, 1.0)

    energy = (mag.astype(np.float64) ** 2).mean(axis=0)
    win_f = max(1, min(int(8 * FPS), max(1, len(energy) // 3)))
    tail_guard = min(int(12 * FPS), max(1, len(energy) // 4))
    smooth_e = ndimage.uniform_filter1d(energy, size=win_f)
    peak_frame = int(np.argmax(smooth_e[: max(1, len(smooth_e) - tail_guard)]))
    preview_ms = max(500, int(round(peak_frame / FPS * 1000.0)))
    if preview_ms >= int(duration * 1000.0) - 1000:
        preview_ms = max(250, int(duration * 250.0))

    return {
        "duration": duration,
        "bpm": bpm,
        "grid0": grid0,
        "kick": kick_f / (np.percentile(kick_f, 95) + 1e-9),
        "snare": snare_f / (np.percentile(snare_f, 95) + 1e-9),
        "hat": hat_f / (np.percentile(hat_f, 95) + 1e-9),
        "mel": mel_f / (np.percentile(mel_f, 95) + 1e-9),
        "pitch": norm_pitch,
        "energy": energy / (np.percentile(energy, 95) + 1e-9),
        "preview_ms": preview_ms,
    }


STAGE_X_MIN = 0.16
STAGE_X_MAX = 0.84
STAGE_Y_MIN = 0.18
STAGE_Y_MAX = 0.82
GOLDEN_ANGLE = math.pi * (3.0 - math.sqrt(5.0))
MIN_SAME_LANE_GAP_MS = 120
MIN_APPROACH_DIST = 0.18
APPROACH_WINDOW_MS = 1200


def _angle_diff(a: float, b: float) -> float:
    d = (a - b + math.pi) % math.tau - math.pi
    return abs(d)


def _ray_box_max_r(lx: float, ly: float, dx: float, dy: float, pad: float = 0.0) -> float:
    x0, x1 = STAGE_X_MIN + pad, STAGE_X_MAX - pad
    y0, y1 = STAGE_Y_MIN + pad, STAGE_Y_MAX - pad
    limits: list[float] = []
    if dx > 1e-6:
        limits.append((x1 - lx) / dx)
    elif dx < -1e-6:
        limits.append((x0 - lx) / dx)
    if dy > 1e-6:
        limits.append((y1 - ly) / dy)
    elif dy < -1e-6:
        limits.append((y0 - ly) / dy)
    return max(0.0, min(limits)) if limits else 0.0


def build_stage_events(an: dict, diff: DiffSpec, spec: TrackSpec) -> list[dict]:
    """Author musical Chart v4 stage_events aligned to bar downbeats & section energy peaks."""
    dur = float(an["duration"])
    bpm = float(an["bpm"])
    beat = 60.0 / bpm
    bar_sec = beat * 4.0
    grid0 = float(an["grid0"])
    energy = an["energy"]
    kick = an["kick"]
    snare = an["snare"]
    mel = an["mel"]

    bg, primary, accent = spec.palette
    c_primary = [int(primary[0]), int(primary[1]), int(primary[2])]
    c_accent = [int(accent[0]), int(accent[1]), int(accent[2])]
    c_bloom = [
        min(255, int(round(primary[0] * 0.72 + 70))),
        min(255, int(round(primary[1] * 0.72 + 52))),
        min(255, int(round(primary[2] * 0.72 + 58))),
    ]
    c_vignette = [
        min(255, int(bg[0] * 3 + 74)),
        min(255, int(bg[1] * 3 + 82)),
        min(255, int(bg[2] * 3 + 116)),
    ]

    diff_mult = {"Easy": 0.88, "Normal": 0.95, "Hard": 1.04, "Expert": 1.12}.get(diff.name, 1.0)
    max_event_s = max(1.2, dur - 1.2)

    bars: list[dict] = []
    b_idx = 0
    while True:
        t0 = grid0 + b_idx * bar_sec
        if t0 > max_event_s:
            break
        if t0 >= 0.25:
            f0 = max(0, min(len(energy) - 1, int(round(t0 * FPS))))
            f1 = max(f0 + 1, min(len(energy), int(round((t0 + bar_sec) * FPS))))
            df1 = max(f0 + 1, min(len(energy), f0 + max(2, int(round(beat * 0.5 * FPS)))))
            e_mean = float(energy[f0:f1].mean())
            m_mean = float(mel[f0:f1].mean())
            p_peak = float((kick[f0:df1] + 0.8 * snare[f0:df1]).max())
            bars.append({
                "idx": len(bars),
                "time": int(round(t0 * 1000.0)),
                "energy": e_mean,
                "mel": m_mean,
                "perc": p_peak,
            })
        b_idx += 1

    if len(bars) < 4:
        # Ultra-short audio safety fallback: 4 evenly spaced musical landmarks
        span_ms = max(2000, int(round(dur * 850.0)))
        return [
            {"time": int(span_ms * 0.08), "duration": 750, "type": "pulse", "intensity": 0.55, "color": c_primary},
            {"time": int(span_ms * 0.28), "duration": 2200, "type": "tint", "intensity": 0.45, "color": c_accent},
            {"time": int(span_ms * 0.52), "duration": 1600, "type": "vignette", "intensity": 0.52, "color": c_vignette},
            {"time": int(span_ms * 0.74), "duration": 3000, "type": "petal_burst", "intensity": 1.55, "color": c_bloom},
        ]

    n_bars = len(bars)
    e_arr = np.array([b["energy"] for b in bars], dtype=np.float64)
    e_max = float(np.percentile(e_arr, 95)) + 1e-9
    for i, b in enumerate(bars):
        prev_e = float(e_arr[max(0, i - 2): max(1, i)].mean()) if i > 0 else float(e_arr[0])
        next_e = float(e_arr[i: min(n_bars, i + 2)].mean())
        b["rise"] = max(0.0, next_e - prev_e)
        b["norm_e"] = float(np.clip(b["energy"] / e_max, 0.0, 1.35))

    used: set[int] = set()

    def pick_best(candidates: list[int], key_fn, min_gap: int) -> int:
        for gap in range(min_gap, 0, -1):
            valid = [i for i in candidates if i not in used and all(abs(i - u) >= gap for u in used)]
            if valid:
                chosen = max(valid, key=key_fn)
                used.add(chosen)
                return chosen
        fallback = [i for i in range(n_bars) if i not in used]
        if not fallback:
            return 0
        chosen = max(fallback, key=key_fn)
        used.add(chosen)
        return chosen

    all_indices = list(range(n_bars))
    intro_indices = list(range(0, max(1, min(n_bars, max(2, n_bars // 6)))))
    body_indices = list(range(max(1, n_bars // 8), max(2, n_bars - 1)))
    sep = max(2, n_bars // 6)

    # Guarantee all 4 Chart v4 event types first, then add secondary section peaks up to 8-9 events
    b_open_pulse = pick_best(intro_indices, lambda i: bars[i]["perc"] + 0.4 * bars[i]["norm_e"], 1)
    b_main_burst = pick_best(body_indices, lambda i: 0.68 * bars[i]["norm_e"] + 0.45 * bars[i]["rise"], sep)
    b_mel_tint_1 = pick_best(all_indices, lambda i: bars[i]["mel"] + 0.25 * bars[i]["norm_e"], max(1, sep - 1))
    b_tension_vig_1 = pick_best(
        all_indices,
        lambda i: (1.0 - 0.45 * min(1.0, bars[i]["norm_e"])) + 0.65 * bars[i]["rise"],
        max(1, sep - 1),
    )

    planned: list[tuple[int, str, list[int]]] = [
        (b_open_pulse, "pulse", c_primary),
        (b_main_burst, "petal_burst", c_bloom),
        (b_mel_tint_1, "tint", c_accent),
        (b_tension_vig_1, "vignette", c_vignette),
    ]

    if n_bars >= 8:
        b_sec_burst = pick_best(body_indices, lambda i: 0.65 * bars[i]["norm_e"] + 0.40 * bars[i]["rise"], sep)
        b_mel_tint_2 = pick_best(all_indices, lambda i: bars[i]["mel"] + 0.20 * bars[i]["perc"], max(1, sep - 1))
        planned.append((b_sec_burst, "petal_burst", c_primary))
        planned.append((b_mel_tint_2, "tint", c_primary))

    if n_bars >= 12:
        b_peak_pulse = pick_best(body_indices, lambda i: bars[i]["perc"] + 0.55 * bars[i]["norm_e"], max(1, sep - 1))
        b_tension_vig_2 = pick_best(
            all_indices,
            lambda i: (1.0 - 0.40 * min(1.0, bars[i]["norm_e"])) + 0.55 * bars[i]["rise"],
            max(1, sep - 1),
        )
        planned.append((b_peak_pulse, "pulse", c_accent))
        planned.append((b_tension_vig_2, "vignette", c_vignette))

    if n_bars >= 24:
        outro_indices = list(range(max(1, int(n_bars * 0.72)), n_bars))
        b_outro_pulse = pick_best(outro_indices, lambda i: bars[i]["perc"] + 0.5 * bars[i]["norm_e"], 2)
        planned.append((b_outro_pulse, "pulse", c_bloom))

    pulse_dur = int(np.clip(round(beat * 2.0 * 1000.0), 600, 1500))
    burst_dur = int(np.clip(round(beat * 8.0 * 1000.0), 2400, 7200))
    tint_dur = int(np.clip(round(beat * 6.0 * 1000.0), 1800, 4600))
    vig_dur = int(np.clip(round(beat * 4.0 * 1000.0), 1400, 3400))

    events: list[dict] = []
    for b_i, ev_type, color in sorted(planned, key=lambda item: bars[item[0]]["time"]):
        ne = bars[b_i]["norm_e"]
        t_ms = max(0, int(bars[b_i]["time"]))
        if events and t_ms <= events[-1]["time"]:
            t_ms = events[-1]["time"] + 20
        if ev_type == "pulse":
            dur_ms = pulse_dur
            inten = round(float(np.clip((0.48 + 0.16 * ne) * diff_mult, 0.40, 0.85)), 2)
        elif ev_type == "petal_burst":
            dur_ms = burst_dur
            inten = round(float(np.clip((1.28 + 0.42 * ne) * diff_mult, 1.15, 1.95)), 2)
        elif ev_type == "tint":
            dur_ms = tint_dur
            inten = round(float(np.clip((0.38 + 0.14 * ne) * diff_mult, 0.34, 0.68)), 2)
        else:
            dur_ms = vig_dur
            inten = round(float(np.clip((0.44 + 0.14 * (1.1 - 0.35 * ne)) * diff_mult, 0.36, 0.72)), 2)
        events.append({
            "time": t_ms,
            "duration": dur_ms,
            "type": ev_type,
            "intensity": inten,
            "color": color,
        })

    return events


def build_chart_v4(an: dict, diff: DiffSpec, spec: TrackSpec) -> dict:
    dur = float(an["duration"])
    bpm = float(an["bpm"])
    beat = 60.0 / bpm
    step_sec = beat / 4.0
    if dur < 20.0:
        start_s = min(max(0.8, dur * 0.10), max(1.0, float(an["grid0"]) + beat))
        end_s = max(start_s + 3.0, dur - max(0.5, dur * 0.08))
    else:
        start_s = max(1.4, float(an["grid0"]) + 2.0 * beat)
        end_s = max(start_s + 6.0, dur - 1.6)
    total_steps = int((end_s - float(an["grid0"])) / step_sec)

    track_seed = (
        int(round(dur * 100.0)) * 37
        + int(round(bpm * 2.0)) * 19
        + int(round(diff.level * 10.0)) * 7
        + sum(ord(c) for c in spec.chart_id)
    )

    def step_ms(s: int) -> int:
        return int(round((an["grid0"] + s * step_sec) * 1000.0))

    def sample(arr: np.ndarray, s: int) -> float:
        f = int(round((an["grid0"] + s * step_sec) * FPS))
        if f < 0 or f >= len(arr):
            return 0.0
        return float(arr[max(0, f - 1): min(len(arr), f + 2)].max())

    def sample_pitch(s: int) -> float:
        f = min(len(an["pitch"]) - 1, max(0, int(round((an["grid0"] + s * step_sec) * FPS))))
        return float(an["pitch"][f])

    # 1. Phrase-balanced musical onset selection for keyboard and mouse zones
    span = max(5.0, end_s - start_s)
    want_chords = diff.name in ("Hard", "Expert")
    chord_share = 0.13 if diff.name == "Expert" else (0.09 if diff.name == "Hard" else 0.0)
    target_kb_total = max(16, int(round(span * diff.kb_density)))
    target_kb_events = max(14, int(round(target_kb_total / (1.0 + chord_share))))
    target_ms = max(6, int(round(span * diff.ms_density)))

    valid_steps: list[int] = []
    kb_score: dict[int, float] = {}
    ms_score: dict[int, float] = {}
    for s in range(0, total_steps, diff.grid):
        t_s = an["grid0"] + s * step_sec
        if t_s < start_s or t_s > end_s:
            continue
        pos_w = 1.30 if (s % 16 == 0) else (1.15 if (s % 4 == 0) else (1.04 if s % 2 == 0 else 0.96))
        k_str = (
            1.12 * sample(an["kick"], s)
            + 1.02 * sample(an["snare"], s)
            + 0.62 * sample(an["hat"], s)
            + 0.22 * sample(an["mel"], s)
        ) * pos_w
        valid_steps.append(s)
        kb_score[s] = k_str
        if s % max(2, diff.grid) == 0:
            m_str = (
                1.18 * sample(an["mel"], s)
                + 0.36 * sample(an["energy"], s)
                + 0.18 * sample(an["snare"], s)
            ) * (1.16 if s % 4 == 0 else 1.0)
            ms_score[s] = m_str

    # Distribute keyboard event budget across 2-bar (32-step) windows following local musical energy
    picked_kb_steps: list[int] = []
    if valid_steps:
        s_min, s_max = valid_steps[0], valid_steps[-1] + 1
        win_size = 32
        windows: list[list[int]] = []
        win_weights: list[float] = []
        for w0 in range((s_min // win_size) * win_size, s_max, win_size):
            ws = [s for s in valid_steps if w0 <= s < w0 + win_size]
            if not ws:
                continue
            windows.append(ws)
            mean_e = sum(kb_score[s] for s in ws) / len(ws)
            win_weights.append(mean_e * len(ws))
        mean_w = (sum(win_weights) / len(win_weights)) if win_weights else 1.0
        rel_w = [float(np.clip(math.sqrt(w / max(1e-9, mean_w)), 0.45, 1.45)) for w in win_weights]
        w_sum = sum(rel_w) or 1.0
        carry = 0.0
        max_16th_run = 6 if diff.level >= 11.0 else 4

        def _kb_step_ok(cand_s: int, chosen_set: set[int]) -> bool:
            if diff.grid == 1 and (cand_s % 2 == 1):
                lo = hi = cand_s
                while (lo - 1) in chosen_set:
                    lo -= 1
                while (hi + 1) in chosen_set:
                    hi += 1
                if hi - lo + 1 > max_16th_run:
                    return False
            return True

        chosen_kb_set: set[int] = set()
        for ws, rw in zip(windows, rel_w):
            quota_f = target_kb_events * (rw / w_sum) + carry
            quota = int(math.floor(quota_f + 0.5))
            carry = min(2.0, quota_f - quota)
            ranked = sorted(ws, key=lambda x: (-kb_score[x], x))
            added = 0
            for s in ranked:
                if added >= quota:
                    break
                if _kb_step_ok(s, chosen_set=chosen_kb_set):
                    chosen_kb_set.add(s)
                    added += 1
            if added < quota:
                carry = min(2.5, carry + (quota - added))
        picked_kb_steps = sorted(chosen_kb_set)

    # Distribute mouse event budget across 4-bar (64-step) phrases with natural breathing gaps
    min_ms_step_gap = max(2, int(round(0.24 / step_sec)))
    picked_ms_steps: list[int] = []
    ms_candidates = sorted(ms_score.keys())
    if ms_candidates:
        s_min, s_max = ms_candidates[0], ms_candidates[-1] + 1
        p_size = 64
        phrases: list[list[int]] = []
        p_weights: list[float] = []
        for p0 in range((s_min // p_size) * p_size, s_max, p_size):
            ps = [s for s in ms_candidates if p0 <= s < p0 + p_size]
            if not ps:
                continue
            phrases.append(ps)
            p_weights.append((sum(ms_score[s] for s in ps) / len(ps)) * len(ps))
        mean_pw = (sum(p_weights) / len(p_weights)) if p_weights else 1.0
        rel_pw = [float(np.clip(math.sqrt(w / max(1e-9, mean_pw)), 0.40, 1.45)) for w in p_weights]
        pw_sum = sum(rel_pw) or 1.0
        carry_m = 0.0
        for ps, rpw in zip(phrases, rel_pw):
            q_f = target_ms * (rpw / pw_sum) + carry_m
            q_n = int(math.floor(q_f + 0.5))
            carry_m = min(2.0, q_f - q_n)
            added_m = 0
            for s in sorted(ps, key=lambda x: (-ms_score[x], x)):
                if added_m >= q_n:
                    break
                if all(abs(s - prev) >= min_ms_step_gap for prev in picked_ms_steps):
                    picked_ms_steps.append(s)
                    added_m += 1
            if added_m < q_n:
                carry_m = min(2.0, carry_m + (q_n - added_m))
        picked_ms_steps.sort()

    # Ensure comfortable breathing window for >= 1 curved slider even on ultra-dense Expert charts
    if len(picked_ms_steps) >= 4:
        wide_gaps = sum(
            1
            for i in range(len(picked_ms_steps) - 1)
            if step_ms(picked_ms_steps[i + 1]) - step_ms(picked_ms_steps[i]) >= 620
        )
        if wide_gaps < 2:
            del picked_ms_steps[len(picked_ms_steps) // 2]

    # 2. Left-hand keyboard authoring: stairs / alternating / bounded trills, chords & thumb bar
    STAIR_ASC = (0, 1, 2, 3)
    STAIR_DESC = (3, 2, 1, 0)
    WAVE_ASC = (0, 1, 2, 3, 2, 1)
    WAVE_DESC = (3, 2, 1, 0, 1, 2)
    ALT_PATTERNS = (
        (0, 2, 1, 3),
        (3, 1, 2, 0),
        (0, 3, 1, 2),
        (1, 3, 0, 2),
    )
    TRILL_PATTERNS = (
        (1, 2, 1, 2),
        (0, 2, 0, 2),
        (1, 3, 1, 3),
        (0, 1, 0, 1),
        (2, 3, 2, 3),
    )
    CHORD_PARTNERS = {
        0: ((2, 3, 1), (3, 2, 1)),
        1: ((3, 2, 0), (2, 3, 0)),
        2: ((0, 1, 3), (1, 0, 3)),
        3: ((1, 0, 2), (0, 1, 2)),
    }

    kb_notes: list[dict] = []
    lane_free_ms = [-10000] * 5
    hold_until_ms = [-10000] * 5
    last_single_lane = -1
    last_kb_ms = -10000
    last_chord_ms = -10000
    chord_min_gap_ms = 720 if diff.name == "Expert" else 1250
    thumb_stride = max(2, int(round(1.0 / max(0.10, diff.thumb_ratio))))
    downbeat_counter = 0

    cur_bar = -1
    cur_motif: tuple[int, ...] = STAIR_ASC
    motif_cursor = 0
    trill_streak = 0
    is_trill_bar = False

    for idx, s in enumerate(picked_kb_steps):
        t = step_ms(s)
        next_t = step_ms(picked_kb_steps[idx + 1]) if idx + 1 < len(picked_kb_steps) else int(end_s * 1000.0)
        bar = s // 16
        if bar != cur_bar:
            cur_bar = bar
            motif_cursor = 0
            trill_streak = 0
            p_start = sample_pitch(bar * 16)
            p_end = sample_pitch(min(total_steps - 1, bar * 16 + 12))
            p_delta = p_end - p_start
            hat_bar = (sample(an["hat"], bar * 16 + 2) + sample(an["hat"], bar * 16 + 6)) * 0.5
            selector = (bar + track_seed) % 6
            if hat_bar > 0.48 and selector in (1, 4):
                cur_motif = TRILL_PATTERNS[(bar + track_seed) % len(TRILL_PATTERNS)]
                is_trill_bar = True
            elif p_delta > 0.07:
                cur_motif = STAIR_ASC if (bar % 2 == 0) else WAVE_ASC
                is_trill_bar = False
            elif p_delta < -0.07:
                cur_motif = STAIR_DESC if (bar % 2 == 0) else WAVE_DESC
                is_trill_bar = False
            elif selector in (0, 3):
                cur_motif = ALT_PATTERNS[(bar + track_seed) % len(ALT_PATTERNS)]
                is_trill_bar = False
            else:
                cur_motif = STAIR_ASC if ((bar + track_seed) % 2 == 0) else STAIR_DESC
                is_trill_bar = False

        active_holds = sum(1 for h_end in hold_until_ms if h_end > t)
        k_val = sample(an["kick"], s)
        sn_val = sample(an["snare"], s)
        e_val = sample(an["energy"], s)
        is_bar_downbeat = (s % 16 == 0)
        if is_bar_downbeat:
            downbeat_counter += 1

        # Spacebar thumb accent on strong bar downbeats
        want_thumb = (
            is_bar_downbeat
            and (downbeat_counter % thumb_stride == 1)
            and active_holds == 0
            and (t - lane_free_ms[4] >= max(380, int(round(beat * 1.5 * 1000.0))))
        )
        if want_thumb:
            want_thumb_hold = (downbeat_counter % (thumb_stride * 3) == 1) and (next_t - t >= 340)
            hold_dur = int(round(beat * 1.25 * 1000.0))
            if want_thumb_hold and hold_dur >= 240 and (t + hold_dur <= int(end_s * 1000.0) - 100) and (next_t - t >= hold_dur + 80):
                kb_notes.append({"time": t, "lane": 4, "type": "hold", "duration": hold_dur})
                lane_free_ms[4] = t + hold_dur + 30
                hold_until_ms[4] = t + hold_dur
            else:
                kb_notes.append({"time": t, "lane": 4, "type": "tap"})
                lane_free_ms[4] = t
            last_kb_ms = t
            continue

        # Standard lanes 0..3: follow motif (stairs / alternating / bounded trills) without jacks
        if is_trill_bar and trill_streak >= 4:
            cur_motif = STAIR_ASC if (bar % 2 == 0) else STAIR_DESC
            is_trill_bar = False
            motif_cursor = 0

        if t - last_kb_ms >= int(round(beat * 1.35 * 1000.0)):
            if k_val >= sn_val and k_val > 0.42:
                pref_lane = 0 if (bar % 2 == 0) else 1
            elif sn_val > 0.42:
                pref_lane = 3 if (bar % 2 == 0) else 2
            else:
                pref_lane = cur_motif[motif_cursor % len(cur_motif)]
            if pref_lane in cur_motif:
                motif_cursor = cur_motif.index(pref_lane)
        else:
            pref_lane = cur_motif[motif_cursor % len(cur_motif)]

        motif_cursor += 1
        if is_trill_bar:
            trill_streak += 1

        step_order = [pref_lane]
        for delta in (1, 2, 3):
            cand = (pref_lane + delta if (bar % 2 == 0) else pref_lane - delta) % 4
            if cand not in step_order:
                step_order.append(cand)

        if t - last_kb_ms < 420 and last_single_lane in step_order:
            step_order = [l for l in step_order if l != last_single_lane] + [last_single_lane]

        chosen_lane = -1
        for l in step_order:
            if t - lane_free_ms[l] >= MIN_SAME_LANE_GAP_MS:
                if (t - last_kb_ms < 260) and (l == last_single_lane):
                    continue
                chosen_lane = l
                break
        if chosen_lane < 0:
            for l in step_order:
                if t - lane_free_ms[l] >= MIN_SAME_LANE_GAP_MS:
                    chosen_lane = l
                    break
        if chosen_lane < 0:
            continue

        # Intentional 2-lane simultaneous chords on strong downbeat accents in Hard/Expert
        is_strong_accent = (
            is_bar_downbeat
            or (diff.name == "Expert" and (s % 8 == 0) and (k_val + sn_val + 0.4 * e_val) >= 1.15)
            or (diff.name == "Hard" and (s % 8 == 0) and (k_val + sn_val) >= 1.35)
        )
        placed_chord = False
        if (
            want_chords
            and is_strong_accent
            and active_holds == 0
            and (t - last_chord_ms >= chord_min_gap_ms)
            and (next_t - t >= 115)
        ):
            partner_candidates = CHORD_PARTNERS[chosen_lane][(bar + track_seed) % 2]
            partner_lane = -1
            for pl in partner_candidates:
                if pl != chosen_lane and (t - lane_free_ms[pl] >= MIN_SAME_LANE_GAP_MS):
                    if (t - last_kb_ms < 220) and (pl == last_single_lane):
                        continue
                    partner_lane = pl
                    break
            if partner_lane >= 0:
                l_lo, l_hi = (chosen_lane, partner_lane) if chosen_lane < partner_lane else (partner_lane, chosen_lane)
                kb_notes.append({"time": t, "lane": l_lo, "type": "tap"})
                kb_notes.append({"time": t, "lane": l_hi, "type": "tap"})
                lane_free_ms[l_lo] = t
                lane_free_ms[l_hi] = t
                last_chord_ms = t
                last_kb_ms = t
                last_single_lane = partner_lane
                placed_chord = True

        if placed_chord:
            continue

        # Sustained keyboard hold on phrase downbeats when hands are free
        want_hold = (
            (is_bar_downbeat and (bar % 4 == 1) and active_holds == 0)
            or (len(kb_notes) >= 4 and not any(n["type"] == "hold" for n in kb_notes) and active_holds == 0)
        )
        hold_beats = 1.5 if diff.grid >= 2 else 1.0
        dur_ms = int(round(beat * hold_beats * 1000.0))
        if (
            want_hold
            and dur_ms >= 240
            and (t + dur_ms <= int(end_s * 1000.0) - 100)
            and (diff.grid < 4 or next_t - t >= dur_ms + 60)
        ):
            kb_notes.append({"time": t, "lane": chosen_lane, "type": "hold", "duration": dur_ms})
            lane_free_ms[chosen_lane] = t + dur_ms + 30
            hold_until_ms[chosen_lane] = t + dur_ms
        else:
            kb_notes.append({"time": t, "lane": chosen_lane, "type": "tap"})
            lane_free_ms[chosen_lane] = t

        last_single_lane = chosen_lane
        last_kb_ms = t

    # Guarantee >= 1 hold, >= 1 thumb note, and >= 1 chord on Hard/Expert
    if kb_notes and not any(n["type"] == "hold" for n in kb_notes):
        for i, n in enumerate(kb_notes):
            if n["lane"] < 4:
                t0 = n["time"]
                same_next = next((m["time"] for m in kb_notes[i + 1:] if m["lane"] == n["lane"]), int(end_s * 1000.0))
                if same_next - t0 >= 360:
                    n["type"] = "hold"
                    n["duration"] = min(360, same_next - t0 - 120)
                    break

    if kb_notes and not any(n["lane"] == 4 for n in kb_notes):
        time_counts: dict[int, int] = {}
        for n in kb_notes:
            time_counts[n["time"]] = time_counts.get(n["time"], 0) + 1
        for n in kb_notes:
            if n["type"] == "tap" and time_counts[n["time"]] == 1:
                n["lane"] = 4
                break

    if want_chords:
        time_counts = {}
        for n in kb_notes:
            time_counts[n["time"]] = time_counts.get(n["time"], 0) + 1
        if not any(cnt >= 2 for cnt in time_counts.values()):
            for n in kb_notes:
                if n["type"] == "tap" and n["lane"] < 4 and time_counts[n["time"]] == 1:
                    t0 = n["time"]
                    cand_l = (n["lane"] + 2) % 4
                    conflict = any(
                        m["lane"] == cand_l and not (
                            t0 >= m["time"] + m.get("duration", 0) + MIN_SAME_LANE_GAP_MS
                            or m["time"] >= t0 + MIN_SAME_LANE_GAP_MS
                        )
                        for m in kb_notes
                    )
                    if not conflict:
                        kb_notes.append({"time": t0, "lane": cand_l, "type": "tap"})
                        break

    kb_notes.sort(key=lambda n: (n["time"], n["lane"]))

    # 3. Right-hand mouse dt-adaptive spatial layout & smooth 2-waypoint curved sliders
    ms_notes: list[dict] = []
    ms_history: list[tuple[int, int, list[tuple[float, float]]]] = []
    prev_end_ms = -10000
    prev_pos: tuple[float, float] | None = None
    prev_angle = ((track_seed % 12) / 12.0) * math.tau

    def _violates_approach_spacing(cx: float, cy: float, now_ms: int, min_d: float = 0.184) -> bool:
        for st_ms, en_ms, pts in ms_history:
            if (now_ms - st_ms <= APPROACH_WINDOW_MS) or (now_ms - en_ms <= APPROACH_WINDOW_MS):
                for ox, oy in pts:
                    if math.hypot(cx - ox, cy - oy) < min_d:
                        return True
        return False

    def _wall_penalty(cx: float, cy: float) -> float:
        dx_w = min(cx - STAGE_X_MIN, STAGE_X_MAX - cx)
        dy_w = min(cy - STAGE_Y_MIN, STAGE_Y_MAX - cy)
        pen = 0.0
        if dx_w < 0.065:
            pen += 2.2 * ((0.065 - dx_w) / 0.065) ** 2
        if dy_w < 0.065:
            pen += 2.2 * ((0.065 - dy_w) / 0.065) ** 2
        return pen

    for m_idx, s in enumerate(picked_ms_steps):
        t = step_ms(s)
        if prev_pos is not None and t <= prev_end_ms + 145:
            continue

        pitch_val = sample_pitch(s)
        pitch_y = float(np.clip(0.80 - 0.60 * pitch_val, STAGE_Y_MIN + 0.02, STAGE_Y_MAX - 0.02))

        if prev_pos is None:
            px = round(float(np.clip(0.26 + 0.09 * ((track_seed + s) % 5), STAGE_X_MIN + 0.04, STAGE_X_MAX - 0.04)), 3)
            py = round(pitch_y, 3)
        else:
            lx, ly = prev_pos
            dt_ms = float(max(1, t - prev_end_ms))
            target_d = min(0.68, max(0.15, 0.15 + 0.50 * (1.0 - math.exp(-dt_ms / 620.0))))

            phrase_idx = s // 16
            arc_dir = 1.0 if ((phrase_idx + track_seed) % 2 == 0) else -1.0
            arc_turn = 0.54 if dt_ms < 520.0 else 0.92
            arc_theta = (prev_angle + arc_dir * arc_turn) % math.tau
            gold_theta = (prev_angle * 0.25 + (m_idx + 1) * GOLDEN_ANGLE + (track_seed % 17) * 0.19) % math.tau
            cross_theta = math.atan2(pitch_y - ly, (1.0 - lx) - lx)

            recent_heads = [pts[0] for _, _, pts in ms_history[-6:]]
            mean_rx = sum(p[0] for p in recent_heads) / len(recent_heads)
            mean_ry = sum(p[1] for p in recent_heads) / len(recent_heads)

            base_angles = [k * (math.tau / 48.0) for k in range(48)] + [arc_theta, gold_theta, cross_theta]
            best_pt: tuple[float, float] | None = None
            best_cost = 1e18
            best_theta = prev_angle

            for theta in base_angles:
                cs, sn = math.cos(theta), math.sin(theta)
                r_box = _ray_box_max_r(lx, ly, cs, sn, pad=0.0)
                r_pad = _ray_box_max_r(lx, ly, cs, sn, pad=0.03)
                radii = (
                    target_d,
                    target_d * 0.94,
                    target_d * 1.06,
                    target_d * 0.86,
                    target_d * 1.14,
                    min(target_d, r_pad),
                    r_box * 0.96,
                )
                for r in radii:
                    if r < 0.145:
                        continue
                    cx = round(float(np.clip(lx + r * cs, STAGE_X_MIN, STAGE_X_MAX)), 3)
                    cy = round(float(np.clip(ly + r * sn, STAGE_Y_MIN, STAGE_Y_MAX)), 3)
                    if _violates_approach_spacing(cx, cy, t, min_d=0.184):
                        continue
                    actual_d = math.hypot(cx - lx, cy - ly)
                    if actual_d < 0.15:
                        continue

                    eff_theta = math.atan2(cy - ly, cx - lx)
                    d_cost = 5.2 * abs(actual_d - target_d)
                    if dt_ms < 560.0:
                        ang_cost = 0.55 * _angle_diff(eff_theta, arc_theta) + 0.22 * _angle_diff(eff_theta, gold_theta)
                        if _angle_diff(eff_theta, prev_angle) > 2.35:
                            ang_cost += 0.95
                    else:
                        ang_cost = (
                            0.42 * _angle_diff(eff_theta, gold_theta)
                            + 0.32 * _angle_diff(eff_theta, cross_theta)
                            + 0.18 * _angle_diff(eff_theta, arc_theta)
                        )
                    pitch_cost = 1.15 * abs(cy - pitch_y)
                    wall_cost = _wall_penalty(cx, cy)
                    spread_cost = 0.32 * max(0.0, 0.34 - math.hypot(cx - mean_rx, cy - mean_ry))
                    total_cost = d_cost + ang_cost + pitch_cost + wall_cost + spread_cost
                    if total_cost < best_cost:
                        best_cost = total_cost
                        best_pt = (cx, cy)
                        best_theta = eff_theta

            if best_pt is None:
                for gx in np.linspace(STAGE_X_MIN + 0.01, STAGE_X_MAX - 0.01, 17):
                    for gy in np.linspace(STAGE_Y_MIN + 0.01, STAGE_Y_MAX - 0.01, 17):
                        cx, cy = round(float(gx), 3), round(float(gy), 3)
                        if _violates_approach_spacing(cx, cy, t, min_d=0.184):
                            continue
                        actual_d = math.hypot(cx - lx, cy - ly)
                        cost = 4.5 * abs(actual_d - target_d) + 1.1 * abs(cy - pitch_y) + _wall_penalty(cx, cy)
                        if cost < best_cost:
                            best_cost = cost
                            best_pt = (cx, cy)
                            best_theta = math.atan2(cy - ly, cx - lx)

            if best_pt is None:
                continue
            px, py = best_pt
            prev_angle = best_theta

        # Smooth 2-waypoint curved slider generation respecting comfortable velocity & bounds
        next_s = picked_ms_steps[m_idx + 1] if m_idx + 1 < len(picked_ms_steps) else total_steps
        avail_ms = step_ms(next_s) - t - 185
        mel_hold = sample(an["mel"], min(total_steps - 1, s + 2)) + 0.5 * sample(an["energy"], min(total_steps - 1, s + 2))
        has_slider_yet = any(n["type"] == "slider" for n in ms_notes)
        want_slider = (
            (m_idx % 5 == 2)
            or (mel_hold > 1.15 and m_idx % 3 == 1)
            or (not has_slider_yet and avail_ms >= 320 and m_idx >= max(1, len(picked_ms_steps) // 3))
            or (m_idx >= len(picked_ms_steps) - 2 and not has_slider_yet)
        )
        max_slider_ms = int(round(beat * (1.5 if diff.level < 10.0 else 1.25) * 1000.0))
        slider_dur = min(avail_ms, max_slider_ms)
        built_slider = False

        if want_slider and slider_dur >= 320:
            dur_s = slider_dur / 1000.0
            target_len = float(np.clip(0.34 * dur_s, 0.17, 0.42))
            chord_d = 0.94 * target_len
            end_step = min(total_steps - 1, s + max(1, int(round(dur_s / step_sec))))
            end_pitch_y = float(np.clip(0.80 - 0.60 * sample_pitch(end_step), STAGE_Y_MIN + 0.02, STAGE_Y_MAX - 0.02))

            best_slider: tuple[list[float], list[float], float] | None = None
            best_sl_cost = 1e18
            for k in range(48):
                phi = (prev_angle + k * (math.tau / 48.0)) % math.tau
                for scale in (1.0, 0.88, 0.76, 0.64):
                    cd = max(0.152, chord_d * scale)
                    ex_raw = px + cd * math.cos(phi)
                    ey_raw = py + cd * math.sin(phi)
                    if not (STAGE_X_MIN + 0.01 <= ex_raw <= STAGE_X_MAX - 0.01 and STAGE_Y_MIN + 0.01 <= ey_raw <= STAGE_Y_MAX - 0.01):
                        continue
                    nx, ny = -math.sin(phi), math.cos(phi)
                    mid_x0, mid_y0 = 0.5 * (px + ex_raw), 0.5 * (py + ey_raw)
                    dot_center = (0.50 - mid_x0) * nx + (0.50 - mid_y0) * ny
                    curve_sign = 1.0 if dot_center >= 0.0 else -1.0
                    bulge = 0.21 * cd * curve_sign
                    mx_raw = mid_x0 + nx * bulge
                    my_raw = mid_y0 + ny * bulge
                    if not (STAGE_X_MIN + 0.01 <= mx_raw <= STAGE_X_MAX - 0.01 and STAGE_Y_MIN + 0.01 <= my_raw <= STAGE_Y_MAX - 0.01):
                        continue
                    wp1 = [round(float(mx_raw), 3), round(float(my_raw), 3)]
                    wp2 = [round(float(ex_raw), 3), round(float(ey_raw), 3)]
                    seg1 = math.hypot(wp1[0] - px, wp1[1] - py)
                    seg2 = math.hypot(wp2[0] - wp1[0], wp2[1] - wp1[1])
                    if seg1 < 0.075 or seg2 < 0.075 or (seg1 + seg2) / dur_s > 0.62:
                        continue
                    if _violates_approach_spacing(wp2[0], wp2[1], t + slider_dur, min_d=0.184):
                        continue
                    sl_cost = (
                        0.65 * _angle_diff(phi, prev_angle)
                        + 1.10 * abs(wp2[1] - end_pitch_y)
                        + _wall_penalty(wp1[0], wp1[1])
                        + _wall_penalty(wp2[0], wp2[1])
                        + 1.8 * abs(cd - chord_d)
                    )
                    if sl_cost < best_sl_cost:
                        best_sl_cost = sl_cost
                        out_theta = math.atan2(wp2[1] - wp1[1], wp2[0] - wp1[0])
                        best_slider = (wp1, wp2, out_theta)
                    break

            if best_slider is not None:
                wp1, wp2, out_theta = best_slider
                mnote: dict = {
                    "time": t,
                    "x": px,
                    "y": py,
                    "type": "slider",
                    "slider_duration": slider_dur,
                    "slider_path": [wp1, wp2],
                }
                prev_end_ms = t + slider_dur
                prev_pos = (wp2[0], wp2[1])
                prev_angle = out_theta
                ms_history.append((t, t + slider_dur, [(px, py), (wp2[0], wp2[1])]))
                ms_notes.append(mnote)
                built_slider = True

        if not built_slider:
            mnote = {"time": t, "x": px, "y": py, "type": "circle"}
            prev_end_ms = t
            prev_pos = (px, py)
            ms_history.append((t, t, [(px, py)]))
            ms_notes.append(mnote)

    # 4. Cross-zone Chart v3/v4 synergies (link_id resonance & require_lane modifiers) on Hard/Expert
    if diff.synergy and ms_notes:
        kb_at_time: dict[int, list[int]] = {}
        for k_i, kn in enumerate(kb_notes):
            kb_at_time.setdefault(kn["time"], []).append(k_i)

        next_link_id = 1
        link_placed = 0
        mod_placed = 0

        for m_i, mn in enumerate(ms_notes):
            t = mn["time"]
            matching_kb = kb_at_time.get(t, [])

            if matching_kb and (m_i % 3 == 0):
                k_target = next((ki for ki in matching_kb if "link_id" not in kb_notes[ki]), -1)
                if k_target >= 0:
                    lid = next_link_id
                    next_link_id += 1
                    kb_notes[k_target]["link_id"] = lid
                    mn["link_id"] = lid
                    link_placed += 1
                    continue

            if mn["type"] == "circle" and (m_i % 5 == 3):
                active_hold_lane = next(
                    (
                        kn["lane"]
                        for kn in kb_notes
                        if kn["type"] == "hold" and (kn["time"] + 40 <= t <= kn["time"] + kn["duration"] - 60)
                    ),
                    -1,
                )
                if active_hold_lane >= 0:
                    mn["require_lane"] = active_hold_lane
                    mod_placed += 1
                elif matching_kb:
                    pref_k = next((kb_notes[ki] for ki in matching_kb if kb_notes[ki]["lane"] == 4), kb_notes[matching_kb[0]])
                    mn["require_lane"] = pref_k["lane"]
                    mod_placed += 1
                elif diff.sv and (m_i % 10 == 3):
                    mn["require_lane"] = 5  # Right-click circle synergy
                    mod_placed += 1
                else:
                    has_chord_here = len(matching_kb) >= 2
                    lane4_clear = all(
                        (t >= kn["time"] + kn.get("duration", 0) + 240) or (kn["time"] >= t + 240)
                        for kn in kb_notes
                        if kn["lane"] == 4
                    )
                    if not has_chord_here and lane4_clear:
                        kb_notes.append({"time": t, "lane": 4, "type": "tap"})
                        kb_at_time.setdefault(t, []).append(len(kb_notes) - 1)
                        mn["require_lane"] = 4
                        mod_placed += 1

        # Guarantee both link_id > 0 and require_lane >= 0 appear on every Hard/Expert chart
        if link_placed == 0:
            for mn in ms_notes:
                t = mn["time"]
                matching_kb = kb_at_time.get(t, [])
                if matching_kb:
                    lid = next_link_id
                    next_link_id += 1
                    kb_notes[matching_kb[0]]["link_id"] = lid
                    mn["link_id"] = lid
                    link_placed += 1
                    break
            if link_placed == 0:
                for mn in ms_notes:
                    t = mn["time"]
                    for cand_l in (4, 0, 1, 2, 3):
                        clear = all(
                            (t >= kn["time"] + kn.get("duration", 0) + 220) or (kn["time"] >= t + 220)
                            for kn in kb_notes
                            if kn["lane"] == cand_l
                        )
                        if clear:
                            lid = next_link_id
                            next_link_id += 1
                            kb_notes.append({"time": t, "lane": cand_l, "type": "tap", "link_id": lid})
                            mn["link_id"] = lid
                            link_placed += 1
                            break
                    if link_placed > 0:
                        break

        if mod_placed == 0:
            for mn in ms_notes:
                if mn["type"] == "circle" and "link_id" not in mn:
                    mn["require_lane"] = 5
                    break

    kb_notes.sort(key=lambda n: (n["time"], n["lane"]))
    ms_notes.sort(key=lambda n: n["time"])

    sv_points = []
    if diff.sv:
        bar_ms = int(round(beat * 4.0 * 1000.0))
        t_cur = step_ms(16)
        toggle = False
        while t_cur < int((end_s - 4.0) * 1000.0) and len(sv_points) < 12:
            sv_points.append({
                "time": max(0, t_cur),
                "speed": 1.18 if not toggle else 1.0,
                "easing": "ease_in_out" if toggle else "ease_out",
            })
            toggle = not toggle
            t_cur += bar_ms * 4

    stage_events = build_stage_events(an, diff, spec)

    return {
        "version": 4,
        "timing_points": [{"time": 0, "bpm": bpm, "time_signature": [4, 4]}],
        "sv_points": sv_points,
        "keyboard_notes": kb_notes,
        "mouse_notes": ms_notes,
        "stage_events": stage_events,
    }


def render_cover(dest: Path, spec: TrackSpec, seed_val: int) -> None:
    """Render bespoke 512x512 luminous night-sakura cover art tailored to each Vol.04 track."""
    size = 512
    bg, primary, accent = spec.palette
    y_grid, x_grid = np.mgrid[0:size, 0:size].astype(np.float32) / size
    radial = np.sqrt((x_grid - 0.5) ** 2 + (y_grid - 0.40) ** 2)
    glow = np.clip(1.0 - radial * 1.42, 0.0, 1.0) ** 1.75
    diag = np.clip(1.0 - np.abs((x_grid - 0.5) * 0.9 + (y_grid - 0.45) * 0.7) * 1.6, 0.0, 1.0) ** 2.2

    arr = np.zeros((size, size, 3), dtype=np.float32)
    for c in range(3):
        arr[:, :, c] = (
            bg[c] * (1.05 - 0.38 * y_grid)
            + primary[c] * glow * 0.50
            + accent[c] * (glow ** 3) * 0.34
            + accent[c] * diag * 0.14
        )
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), mode="RGB").convert("RGBA")

    overlay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    cx, cy = size // 2, int(size * 0.44)
    rng = np.random.default_rng(seed_val)

    # Outer halo rings
    draw.ellipse((cx - 166, cy - 166, cx + 166, cy + 166), outline=(*primary, 110), width=2)
    draw.ellipse((cx - 126, cy - 126, cx + 126, cy + 126), fill=(*accent, 36), outline=(*accent, 195), width=2)

    # Bespoke per-track luminous backdrop motif
    if spec.motif_style == "crescent":
        moon_layer = Image.new("RGBA", (size, size), (0, 0, 0, 0))
        md = ImageDraw.Draw(moon_layer)
        md.ellipse((cx - 98, cy - 98, cx + 98, cy + 98), fill=(*accent, 165))
        md.ellipse((cx - 64, cy - 114, cx + 122, cy + 72), fill=(0, 0, 0, 0))
        overlay = Image.alpha_composite(overlay, moon_layer)
        draw = ImageDraw.Draw(overlay)
    elif spec.motif_style == "ribbons":
        for r_i in range(4):
            pts_rib = []
            phase = r_i * 0.85 + (seed_val % 9) * 0.2
            for step_x in range(28, size - 24, 12):
                u = step_x / size
                sy = int(cy + math.sin(u * math.tau * 1.25 + phase) * (42 + r_i * 18) + (r_i - 1.5) * 16)
                pts_rib.append((step_x, sy))
            col = primary if r_i % 2 == 0 else accent
            draw.line(pts_rib, fill=(*col, 135 - r_i * 18), width=3)
    elif spec.motif_style == "pulse_rings":
        for rad, alpha in ((52, 185), (88, 145), (144, 115), (192, 75)):
            draw.ellipse((cx - rad, cy - rad, cx + rad, cy + rad), outline=(*accent, alpha), width=2)
        wave_pts = []
        for sx in range(44, size - 44, 6):
            u = (sx - cx) / 140.0
            env = math.exp(-u * u * 1.8)
            wy = int(cy + 88 - math.sin(u * 9.5) * 34 * env)
            wave_pts.append((sx, wy))
        draw.line(wave_pts, fill=(*accent, 195), width=3)
    elif spec.motif_style in ("prism_drive", "shatter_core"):
        shard_n = 12 if spec.motif_style == "shatter_core" else 8
        for s_i in range(shard_n):
            ang = s_i * (math.tau / shard_n) + 0.21
            r_in = 36 + (s_i % 3) * 18
            r_out = 132 + (s_i % 4) * 20
            p0 = (cx + math.cos(ang) * r_in, cy + math.sin(ang) * r_in)
            p1 = (cx + math.cos(ang + 0.16) * r_out, cy + math.sin(ang + 0.16) * r_out)
            p2 = (cx + math.cos(ang - 0.14) * (r_out * 0.86), cy + math.sin(ang - 0.14) * (r_out * 0.86))
            col = accent if s_i % 2 == 0 else primary
            draw.polygon([p0, p1, p2], fill=(*col, 92), outline=(*accent, 190))
    elif spec.motif_style == "gale_stream":
        for g_i in range(14):
            x0 = int(rng.integers(36, size - 140))
            y0 = int(rng.integers(64, size - 96))
            ln = int(rng.integers(68, 148))
            col = accent if g_i % 3 == 0 else primary
            draw.line((x0, y0, x0 + ln, y0 - int(ln * 0.42)), fill=(*col, 145), width=2)

    # Luminous 5-petal notched night-sakura blossom centerpiece
    for k in range(5):
        ang = k * (2.0 * math.pi / 5.0) - math.pi / 2.0 + (seed_val % 7) * 0.07
        px = cx + math.cos(ang) * 62
        py = cy + math.sin(ang) * 62
        tip_l = (cx + math.cos(ang - 0.11) * 98, cy + math.sin(ang - 0.11) * 98)
        notch = (cx + math.cos(ang) * 84, cy + math.sin(ang) * 84)
        tip_r = (cx + math.cos(ang + 0.11) * 98, cy + math.sin(ang + 0.11) * 98)
        pts = [
            (cx + math.cos(ang) * 14, cy + math.sin(ang) * 14),
            (px + math.cos(ang - 0.56) * 28, py + math.sin(ang - 0.56) * 28),
            tip_l,
            notch,
            tip_r,
            (px + math.cos(ang + 0.56) * 28, py + math.sin(ang + 0.56) * 28),
        ]
        draw.polygon(pts, fill=(*primary, 192), outline=(*accent, 232))
    draw.ellipse((cx - 14, cy - 14, cx + 14, cy + 14), fill=(*accent, 235))

    # Drifting luminous sakura petals & stardust bokeh
    for _ in range(34):
        bx = int(rng.integers(24, size - 24))
        by = int(rng.integers(24, size - 24))
        br = int(rng.integers(3, 13))
        col = primary if rng.random() < 0.62 else accent
        draw.ellipse((bx - br, by - br, bx + br, by + br), fill=(*col, int(rng.integers(38, 122))))

    glow_wide = overlay.filter(ImageFilter.GaussianBlur(radius=8.0))
    glow_tight = overlay.filter(ImageFilter.GaussianBlur(radius=3.0))
    img = Image.alpha_composite(img, glow_wide)
    img = Image.alpha_composite(img, glow_tight)
    img = Image.alpha_composite(img, overlay)

    frame = ImageDraw.Draw(img)
    frame.rounded_rectangle((18, 18, size - 18, size - 18), radius=22, outline=(*accent, 175), width=2)
    frame.rounded_rectangle((26, 26, size - 26, size - 26), radius=18, outline=(*primary, 95), width=1)
    frame.line((48, size - 78, size - 48, size - 78), fill=(*accent, 160), width=2)
    img.convert("RGB").save(dest, format="PNG", optimize=True)


def validate_chart_v4(chart_doc: dict, diff: DiffSpec, chart_id: str) -> None:
    label = f"{chart_id}/{diff.chart_file}"
    if chart_doc.get("version") != 4:
        raise ValueError(f"{label}: expected Chart Format v4, got {chart_doc.get('version')}")

    kb = chart_doc["keyboard_notes"]
    ms = chart_doc["mouse_notes"]
    events = chart_doc.get("stage_events", [])

    if [n["time"] for n in kb] != sorted(n["time"] for n in kb):
        raise ValueError(f"{label}: keyboard_notes not sorted by time")
    seen_pairs: set[tuple[int, int]] = set()
    last_lane_end = [-10000] * 5
    chord_timestamps: set[int] = set()
    by_time_count: dict[int, int] = {}
    for n in kb:
        t, l = int(n["time"]), int(n["lane"])
        if not (0 <= l <= 4):
            raise ValueError(f"{label}: invalid lane {l}")
        if (t, l) in seen_pairs:
            raise ValueError(f"{label}: duplicate keyboard lane {l} at time {t}")
        seen_pairs.add((t, l))
        by_time_count[t] = by_time_count.get(t, 0) + 1
        if by_time_count[t] >= 2:
            chord_timestamps.add(t)
        if t - last_lane_end[l] < 110:
            raise ValueError(f"{label}: same-lane gap {t - last_lane_end[l]}ms < 110ms on lane {l} at {t}")
        dur = int(n.get("duration", 0)) if n["type"] == "hold" else 0
        if n["type"] == "hold" and dur <= 0:
            raise ValueError(f"{label}: hold note with non-positive duration at {t}")
        last_lane_end[l] = t + dur

    if sum(1 for n in kb if n["lane"] == 4) == 0:
        raise ValueError(f"{label}: missing Spacebar thumb notes (lane=4)")
    if sum(1 for n in kb if n["type"] == "hold") == 0:
        raise ValueError(f"{label}: missing keyboard hold notes")
    if diff.name in ("Hard", "Expert") and not chord_timestamps:
        raise ValueError(f"{label}: missing 2-lane chords on {diff.name}")

    if [n["time"] for n in ms] != sorted(n["time"] for n in ms):
        raise ValueError(f"{label}: mouse_notes not sorted by time")
    prev_ms_end = -10000
    long_dists: list[float] = []
    short_dists: list[float] = []
    for i, mn in enumerate(ms):
        t = int(mn["time"])
        if t <= prev_ms_end:
            raise ValueError(f"{label}: overlapping mouse note at {t} <= {prev_ms_end}")
        x, y = float(mn["x"]), float(mn["y"])
        if not (STAGE_X_MIN - 1e-6 <= x <= STAGE_X_MAX + 1e-6 and STAGE_Y_MIN - 1e-6 <= y <= STAGE_Y_MAX + 1e-6):
            raise ValueError(f"{label}: mouse head ({x}, {y}) out of stage bounds")
        if i > 0:
            prev0 = ms[i - 1]
            p_end_t = int(prev0["time"]) + (int(prev0["slider_duration"]) if prev0["type"] == "slider" else 0)
            px0, py0 = prev0["slider_path"][-1] if prev0["type"] == "slider" else (prev0["x"], prev0["y"])
            dt_jump = t - p_end_t
            j_dist = math.hypot(x - float(px0), y - float(py0))
            if dt_jump >= 900:
                long_dists.append(j_dist)
            elif dt_jump <= 350:
                short_dists.append(j_dist)
        for j in range(i):
            prev = ms[j]
            prev_t = int(prev["time"])
            prev_e = prev_t + (int(prev["slider_duration"]) if prev["type"] == "slider" else 0)
            if (t - prev_t <= APPROACH_WINDOW_MS) or (t - prev_e <= APPROACH_WINDOW_MS):
                if math.hypot(x - float(prev["x"]), y - float(prev["y"])) < MIN_APPROACH_DIST - 1e-6:
                    raise ValueError(f"{label}: approach ring pile-up between {prev_t} and {t}")
                if prev["type"] == "slider":
                    tx, ty = prev["slider_path"][-1]
                    if math.hypot(x - float(tx), y - float(ty)) < MIN_APPROACH_DIST - 1e-6:
                        raise ValueError(f"{label}: mouse note at {t} too close to slider tail at {prev_e}")
        if mn["type"] == "slider":
            sdur = int(mn.get("slider_duration", 0))
            spath = mn.get("slider_path", [])
            if sdur < 320 or len(spath) != 2:
                raise ValueError(f"{label}: invalid slider at {t}")
            for wx, wy in spath:
                if not (STAGE_X_MIN - 1e-6 <= float(wx) <= STAGE_X_MAX + 1e-6 and STAGE_Y_MIN - 1e-6 <= float(wy) <= STAGE_Y_MAX + 1e-6):
                    raise ValueError(f"{label}: slider waypoint ({wx}, {wy}) out of bounds")
            prev_ms_end = t + sdur
        else:
            prev_ms_end = t

    if sum(1 for mn in ms if mn["type"] == "slider") == 0:
        raise ValueError(f"{label}: missing curved slider notes")

    if long_dists:
        avg_long = sum(long_dists) / len(long_dists)
        if short_dists:
            avg_short = sum(short_dists) / len(short_dists)
            if not (avg_long > avg_short or avg_long >= 0.22):
                raise ValueError(f"{label}: failed spatial jump scaling audit ({avg_long:.3f} vs {avg_short:.3f})")
        elif avg_long < 0.22:
            raise ValueError(f"{label}: avg long jump {avg_long:.3f} < 0.22")

    if diff.synergy:
        if not any(mn.get("link_id", 0) > 0 for mn in ms):
            raise ValueError(f"{label}: missing link_id synergies on {diff.name}")
        if not any(mn.get("require_lane", -1) >= 0 for mn in ms):
            raise ValueError(f"{label}: missing require_lane synergies on {diff.name}")

    required_event_types = {"pulse", "tint", "vignette", "petal_burst"}
    present_types = {ev.get("type") for ev in events}
    if not required_event_types.issubset(present_types):
        raise ValueError(f"{label}: missing stage_event types {required_event_types - present_types}")
    prev_ev_t = -1
    for ev in events:
        et = int(ev["time"])
        edur = int(ev["duration"])
        eint = float(ev["intensity"])
        col = ev.get("color", [])
        if et < prev_ev_t or et < 0 or edur <= 0 or not (0.0 < eint <= 2.0):
            raise ValueError(f"{label}: invalid stage_event {ev}")
        if len(col) != 3 or any(not (0 <= int(c) <= 255) for c in col):
            raise ValueError(f"{label}: invalid stage_event color {col}")
        prev_ev_t = et


def build_track(spec: TrackSpec, skip_audio: bool = False) -> dict:
    out_dir = CHARTS_DIR / spec.chart_id
    out_dir.mkdir(parents=True, exist_ok=True)

    ogg_path = out_dir / "music.ogg"
    if not (skip_audio and ogg_path.is_file()):
        src = SONGS_DIR / spec.source_file
        if not src.is_file():
            raise FileNotFoundError(f"Missing real Suno MP3 source audio: {src}")
        encode_ogg(src, ogg_path, spec.title, spec.artist)

    y = decode_mono(ogg_path)
    an = analyze_audio(y)
    seed_val = sum((i + 1) * ord(ch) for i, ch in enumerate(spec.chart_id)) + 404
    cover_path = out_dir / "cover.png"
    if not (skip_audio and cover_path.is_file()):
        render_cover(cover_path, spec, seed_val)

    diff_entries = []
    for dspec in spec.diffs:
        chart_doc = build_chart_v4(an, dspec, spec)
        valid_links = {n.get("link_id", 0) for n in chart_doc["keyboard_notes"]} - {0}
        for mn in chart_doc["mouse_notes"]:
            if mn.get("link_id", 0) not in valid_links:
                mn.pop("link_id", None)
        validate_chart_v4(chart_doc, dspec, spec.chart_id)
        (out_dir / dspec.chart_file).write_text(
            json.dumps(chart_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8"
        )
        kb = chart_doc["keyboard_notes"]
        ms = chart_doc["mouse_notes"]
        holds = sum(1 for n in kb if n["type"] == "hold")
        thumbs = sum(1 for n in kb if n["lane"] == 4)
        syns = sum(1 for n in ms if n.get("link_id", 0) > 0 or n.get("require_lane", -1) >= 0)
        diff_entries.append({
            "name": dspec.name,
            "level": dspec.level,
            "chart_file": dspec.chart_file,
            "note_count": len(kb),
            "hold_count": holds,
            "mouse_note_count": len(ms),
            "thumb_count": thumbs,
            "synergy_count": syns,
        })

    info_doc = {
        "version": 4,
        "id": spec.chart_id,
        "title": spec.title,
        "artist": spec.artist,
        "charter": "Sakura Chart Studio",
        "source": "Sakura Official Pack · Vol. 04",
        "tags": list(spec.tags),
        "music_file": "music.ogg",
        "cover_file": "cover.png",
        "background_file": "",
        "preview_time": an["preview_ms"],
        "bpm": an["bpm"],
        "offset": 0,
        "difficulties": diff_entries,
    }
    (out_dir / "info.json").write_text(json.dumps(info_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(
        f"[pack-vol4] Built {spec.chart_id} ({spec.title}): "
        f"{an['duration']:.2f}s @ {an['bpm']} BPM, {len(diff_entries)} diffs"
    )
    return {
        "id": spec.chart_id,
        "seconds": round(float(an["duration"]), 2),
        "sample_rate": 44100,
        "channels": 2,
        "difficulties": diff_entries,
    }


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--skip-audio", action="store_true", help="Reuse existing music.ogg if present")
    parser.add_argument("--update-album", action="store_true", help="Also update resources/charts/album.json")
    args = parser.parse_args()

    built = [build_track(spec, skip_audio=args.skip_audio) for spec in PACK_SPECS]

    if args.update_album or not args.skip_audio:
        album_path = CHARTS_DIR / "album.json"
        existing = json.loads(album_path.read_text(encoding="utf-8")) if album_path.is_file() else []
        pack_ids = {s.chart_id for s in PACK_SPECS}
        kept = [entry for entry in existing if entry.get("id") not in pack_ids] + built
        album_path.write_text(json.dumps(kept, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        print(f"[pack-vol4] Updated {album_path} with {len(kept)} album entries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
