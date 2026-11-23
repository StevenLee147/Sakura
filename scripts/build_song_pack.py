"""General audio-analysis Chart v3 & procedural cover generator for Sakura Official Song Pack.

Reads curated MP3 tracks from git-ignored download/songs/, transcodes reproducible music.ogg,
estimates BPM & beat grid via percussive onset autocorrelation, separates drums/melody via STFT
HPSS, authors multi-difficulty Chart Format v3 files (A/S/D/F + Spacebar thumb bar + Link chords
+ Synergy modifiers + Sliders + SV), and renders luminous night-sakura cover.png artwork.
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
    synergy: bool      # enable Chart v3 link_id & require_lane
    sv: bool           # enable scroll-velocity modulation


@dataclass(frozen=True)
class TrackSpec:
    chart_id: str
    source_file: str
    title: str
    artist: str
    tags: tuple[str, ...]
    palette: tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]
    diffs: tuple[DiffSpec, ...]


PACK_SPECS: tuple[TrackSpec, ...] = (
    TrackSpec(
        "sakura_awakening", "樱光初醒.mp3", "樱光初醒", "Sakura Soundworks",
        ("original", "electronic", "melodic", "v3-synergy"),
        ((18, 22, 48), (246, 172, 208), (252, 222, 156)),
        (
            DiffSpec("Easy", "easy.json", 2.5, 4, 1.15, 0.35, 0.12, False, False),
            DiffSpec("Normal", "normal.json", 4.5, 2, 2.05, 0.62, 0.16, False, False),
            DiffSpec("Hard", "hard.json", 7.0, 2, 3.10, 0.95, 0.20, True, False),
        ),
    ),
    TrackSpec(
        "starry_sky", "繁星.mp3", "繁星", "Sakura Soundworks",
        ("original", "synth", "atmospheric", "v3-synergy"),
        ((12, 20, 52), (132, 206, 252), (250, 224, 168)),
        (
            DiffSpec("Normal", "normal.json", 3.5, 4, 1.55, 0.48, 0.14, False, False),
            DiffSpec("Hard", "hard.json", 6.5, 2, 2.85, 0.88, 0.18, True, False),
            DiffSpec("Expert", "expert.json", 9.5, 1, 4.35, 1.32, 0.22, True, True),
        ),
    ),
    TrackSpec(
        "distant_star", "遥远的星.mp3", "遥远的星", "Sakura Soundworks",
        ("original", "electronic", "trance", "v3-synergy"),
        ((16, 18, 46), (156, 182, 255), (246, 184, 218)),
        (
            DiffSpec("Normal", "normal.json", 4.5, 4, 1.75, 0.52, 0.15, False, False),
            DiffSpec("Hard", "hard.json", 7.5, 2, 3.20, 0.96, 0.20, True, False),
            DiffSpec("Expert", "expert.json", 10.5, 1, 4.85, 1.48, 0.24, True, True),
        ),
    ),
    TrackSpec(
        "unfallen_light", "樱光未落.mp3", "樱光未落", "Sakura Soundworks",
        ("original", "drum-and-bass", "sakura", "v3-synergy"),
        ((24, 18, 44), (250, 166, 198), (255, 226, 160)),
        (
            DiffSpec("Normal", "normal.json", 5.0, 4, 1.85, 0.56, 0.15, False, False),
            DiffSpec("Hard", "hard.json", 8.0, 2, 3.40, 1.05, 0.20, True, False),
            DiffSpec("Expert", "expert.json", 11.0, 1, 5.10, 1.55, 0.24, True, True),
        ),
    ),
    TrackSpec(
        "sidechain_dream", "樱落侧链梦.mp3", "樱落侧链梦", "Sakura Soundworks",
        ("original", "future-bass", "electronic", "v3-synergy"),
        ((22, 16, 48), (218, 158, 250), (142, 232, 246)),
        (
            DiffSpec("Normal", "normal.json", 5.5, 2, 2.15, 0.64, 0.16, False, False),
            DiffSpec("Hard", "hard.json", 8.5, 2, 3.55, 1.10, 0.22, True, False),
            DiffSpec("Expert", "expert.json", 11.5, 1, 5.35, 1.62, 0.25, True, True),
        ),
    ),
    TrackSpec(
        "flowing_petals", "樱落流光.mp3", "樱落流光", "Sakura Soundworks",
        ("original", "artcore", "melodic", "v3-synergy"),
        ((16, 24, 48), (248, 184, 212), (148, 234, 224)),
        (
            DiffSpec("Normal", "normal.json", 5.0, 4, 1.85, 0.58, 0.15, False, False),
            DiffSpec("Hard", "hard.json", 8.5, 2, 3.50, 1.08, 0.20, True, False),
            DiffSpec("Expert", "expert.json", 12.0, 1, 5.50, 1.68, 0.25, True, True),
        ),
    ),
    TrackSpec(
        "instant_bloom", "一瞬花绽.mp3", "一瞬花绽", "Sakura Soundworks",
        ("original", "hyper-melodic", "sakura", "v3-synergy"),
        ((28, 16, 42), (255, 154, 188), (255, 224, 148)),
        (
            DiffSpec("Normal", "normal.json", 5.5, 2, 2.05, 0.62, 0.16, False, False),
            DiffSpec("Hard", "hard.json", 9.0, 2, 3.75, 1.15, 0.22, True, False),
            DiffSpec("Expert", "expert.json", 12.5, 1, 5.80, 1.75, 0.26, True, True),
        ),
    ),
    TrackSpec(
        "petal_storm_overdrive", "Petal Storm_ Overdrive.mp3", "Petal Storm: Overdrive", "Sakura Soundworks",
        ("original", "speedcore", "boss", "v3-synergy"),
        ((32, 12, 36), (255, 118, 156), (255, 218, 132)),
        (
            DiffSpec("Hard", "hard.json", 10.5, 2, 4.15, 1.28, 0.24, True, True),
            DiffSpec("Expert", "expert.json", 14.5, 1, 6.40, 1.92, 0.28, True, True),
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
    smooth_e = ndimage.uniform_filter1d(energy, size=int(8 * FPS))
    peak_frame = int(np.argmax(smooth_e[: max(1, len(smooth_e) - int(12 * FPS))]))
    preview_ms = max(0, int(round(peak_frame / FPS * 1000.0)))

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


def build_chart_v3(an: dict, diff: DiffSpec) -> dict:
    dur = an["duration"]
    bpm = an["bpm"]
    beat = 60.0 / bpm
    step_sec = beat / 4.0
    start_s = max(1.4, an["grid0"] + 2.0 * beat)
    end_s = max(start_s + 8.0, dur - 1.6)
    total_steps = int((end_s - an["grid0"]) / step_sec)

    def step_ms(s: int) -> int:
        return int(round((an["grid0"] + s * step_sec) * 1000.0))

    def sample(arr: np.ndarray, s: int) -> float:
        f = int(round((an["grid0"] + s * step_sec) * FPS))
        if f < 0 or f >= len(arr):
            return 0.0
        return float(arr[max(0, f - 1) : min(len(arr), f + 2)].max())

    cand_kb = []
    cand_ms = []
    for s in range(0, total_steps, diff.grid):
        t_s = an["grid0"] + s * step_sec
        if t_s < start_s or t_s > end_s:
            continue
        pos_w = 1.28 if (s % 16 == 0) else (1.14 if (s % 4 == 0) else 1.0)
        k_str = (1.1 * sample(an["kick"], s) + 1.0 * sample(an["snare"], s) + 0.65 * sample(an["hat"], s)) * pos_w
        m_str = (1.15 * sample(an["mel"], s) + 0.35 * sample(an["energy"], s)) * (1.15 if s % 4 == 0 else 1.0)
        cand_kb.append((k_str, s))
        if s % max(2, diff.grid) == 0:
            cand_ms.append((m_str, s))

    span = max(5.0, end_s - start_s)
    target_kb = max(24, int(round(span * diff.kb_density)))
    target_ms = max(8, int(round(span * diff.ms_density)))

    cand_kb.sort(key=lambda x: x[0], reverse=True)
    picked_kb_steps = sorted([s for _, s in cand_kb[:target_kb]])

    cand_ms.sort(key=lambda x: x[0], reverse=True)
    min_ms_step_gap = max(2, int(round(0.24 / step_sec)))
    picked_ms_steps: list[int] = []
    for _, s in cand_ms:
        if len(picked_ms_steps) >= target_ms:
            break
        if all(abs(s - prev) >= min_ms_step_gap for prev in picked_ms_steps):
            picked_ms_steps.append(s)
    picked_ms_steps.sort()

    kb_notes: list[dict] = []
    lane_end = [-1000, -1000, -1000, -1000, -1000]
    last_lane = 1
    kb_by_step: dict[int, int] = {}

    for idx, s in enumerate(picked_kb_steps):
        t = step_ms(s)
        k_val = sample(an["kick"], s)
        sn_val = sample(an["snare"], s)
        use_thumb = (s % 16 == 0 and (idx % max(3, int(round(1.0 / max(0.08, diff.thumb_ratio)))) == 0))
        if use_thumb and t > lane_end[4] + 120:
            lane = 4
        else:
            if k_val >= sn_val and k_val > 0.45:
                pref = 0 if last_lane != 0 else 1
            elif sn_val > 0.45:
                pref = 3 if last_lane != 3 else 2
            else:
                pref = (last_lane + 1 + (s % 2)) % 4
            lane = pref
            for offset_try in range(4):
                cand_l = (pref + offset_try) % 4
                if t > lane_end[cand_l] + 75:
                    lane = cand_l
                    break
            if t <= lane_end[lane] + 75:
                continue
            last_lane = lane

        next_same_t = int(end_s * 1000.0)
        is_hold = (s % 16 == 0 and idx % 5 == 1) or (len(kb_notes) == 3 and not any(n["type"] == "hold" for n in kb_notes))
        dur_ms = int(round(beat * (1.5 if lane == 4 else 2.0) * 1000.0)) if is_hold else 0
        if is_hold and t + dur_ms < next_same_t - 80:
            note = {"time": t, "lane": lane, "type": "hold", "duration": max(220, dur_ms)}
            lane_end[lane] = t + note["duration"]
        else:
            note = {"time": t, "lane": lane, "type": "tap"}
            lane_end[lane] = t
        kb_by_step[s] = len(kb_notes)
        kb_notes.append(note)

    if not any(n["type"] == "hold" for n in kb_notes) and kb_notes:
        kb_notes[0]["type"] = "hold"
        kb_notes[0]["duration"] = 300

    ms_notes: list[dict] = []
    ms_end = -1000
    sweep_dir = 1.0
    cur_x = 0.28
    next_link_id = 1

    for m_idx, s in enumerate(picked_ms_steps):
        t = step_ms(s)
        if t <= ms_end + 140:
            continue
        next_s = picked_ms_steps[m_idx + 1] if m_idx + 1 < len(picked_ms_steps) else total_steps
        avail_ms = step_ms(next_s) - t - 160

        f_idx = min(len(an["pitch"]) - 1, max(0, int(round((an["grid0"] + s * step_sec) * FPS))))
        py = float(np.clip(0.80 - 0.60 * an["pitch"][f_idx], 0.18, 0.82))
        cur_x += sweep_dir * 0.16
        if cur_x > 0.80:
            cur_x, sweep_dir = 0.78, -1.0
        elif cur_x < 0.20:
            cur_x, sweep_dir = 0.22, 1.0
        px = round(float(cur_x), 3)
        py = round(py, 3)

        want_slider = (m_idx % 5 == 2) or (m_idx == len(picked_ms_steps) - 1 and not any(n["type"] == "slider" for n in ms_notes))
        slider_dur = min(avail_ms, int(round(beat * 1.5 * 1000.0)))
        if want_slider and slider_dur >= 320:
            wp1 = [round(float(np.clip(px + sweep_dir * 0.12, 0.16, 0.84)), 3),
                   round(float(np.clip(py + (0.10 if py < 0.5 else -0.10), 0.16, 0.84)), 3)]
            wp2 = [round(float(np.clip(wp1[0] + sweep_dir * 0.10, 0.16, 0.84)), 3),
                   round(float(np.clip(py, 0.16, 0.84)), 3)]
            mnote: dict = {
                "time": t, "x": px, "y": py, "type": "slider",
                "slider_duration": slider_dur, "slider_path": [wp1, wp2],
            }
            ms_end = t + slider_dur
        else:
            mnote = {"time": t, "x": px, "y": py, "type": "circle"}
            ms_end = t

        if diff.synergy:
            if s in kb_by_step and (m_idx % 3 == 0):
                lid = next_link_id
                next_link_id += 1
                kb_notes[kb_by_step[s]]["link_id"] = lid
                mnote["link_id"] = lid
            elif mnote["type"] == "circle" and (m_idx % 6 == 4):
                if s in kb_by_step:
                    mnote["require_lane"] = kb_notes[kb_by_step[s]]["lane"]
                elif diff.sv and (m_idx % 12 == 4):
                    mnote["require_lane"] = 5  # RMB synergy
                else:
                    mnote["require_lane"] = 4  # Spacebar synergy
                    if t > lane_end[4] + 150:
                        kb_notes.append({"time": t, "lane": 4, "type": "tap"})
                        lane_end[4] = t

        ms_notes.append(mnote)

    kb_notes.sort(key=lambda n: (n["time"], n["lane"]))
    dedup_kb: list[dict] = []
    end_guard = [-1000] * 5
    for n in kb_notes:
        l = n["lane"]
        if n["time"] <= end_guard[l] + 40:
            continue
        end_guard[l] = n["time"] + n.get("duration", 0)
        dedup_kb.append(n)
    kb_notes = dedup_kb

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

    return {
        "version": 3,
        "timing_points": [{"time": 0, "bpm": bpm, "time_signature": [4, 4]}],
        "sv_points": sv_points,
        "keyboard_notes": kb_notes,
        "mouse_notes": ms_notes,
    }


def render_cover(dest: Path, spec: TrackSpec, seed_val: int) -> None:
    size = 512
    bg, primary, accent = spec.palette
    y_grid, x_grid = np.mgrid[0:size, 0:size].astype(np.float32) / size
    radial = np.sqrt((x_grid - 0.5) ** 2 + (y_grid - 0.38) ** 2)
    glow = np.clip(1.0 - radial * 1.45, 0.0, 1.0) ** 1.8

    arr = np.zeros((size, size, 3), dtype=np.float32)
    for c in range(3):
        arr[:, :, c] = bg[c] * (1.0 - 0.35 * y_grid) + primary[c] * glow * 0.48 + accent[c] * (glow ** 3) * 0.32
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), mode="RGB").convert("RGBA")

    overlay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    cx, cy = size // 2, int(size * 0.44)
    draw.ellipse((cx - 118, cy - 118, cx + 118, cy + 118), fill=(*accent, 44), outline=(*accent, 190), width=2)
    draw.ellipse((cx - 154, cy - 154, cx + 154, cy + 154), outline=(*primary, 130), width=2)

    rng = np.random.default_rng(seed_val)
    for k in range(5):
        ang = k * (2.0 * math.pi / 5.0) - math.pi / 2.0 + (seed_val % 7) * 0.08
        px = cx + math.cos(ang) * 64
        py = cy + math.sin(ang) * 64
        pts = [
            (cx + math.cos(ang) * 14, cy + math.sin(ang) * 14),
            (px + math.cos(ang + 0.55) * 28, py + math.sin(ang + 0.55) * 28),
            (cx + math.cos(ang) * 96, cy + math.sin(ang) * 96),
            (px + math.cos(ang - 0.55) * 28, py + math.sin(ang - 0.55) * 28),
        ]
        draw.polygon(pts, fill=(*primary, 185), outline=(*accent, 225))

    for _ in range(28):
        bx = int(rng.integers(24, size - 24))
        by = int(rng.integers(24, size - 24))
        br = int(rng.integers(3, 14))
        col = primary if rng.random() < 0.6 else accent
        draw.ellipse((bx - br, by - br, bx + br, by + br), fill=(*col, int(rng.integers(35, 115))))

    blurred = overlay.filter(ImageFilter.GaussianBlur(radius=3.5))
    img = Image.alpha_composite(img, blurred)
    img = Image.alpha_composite(img, overlay)

    frame = ImageDraw.Draw(img)
    frame.rounded_rectangle((18, 18, size - 18, size - 18), radius=22, outline=(*accent, 165), width=2)
    frame.line((48, size - 78, size - 48, size - 78), fill=(*accent, 150), width=2)
    img.convert("RGB").save(dest, format="PNG", optimize=True)


def build_track(spec: TrackSpec, skip_audio: bool = False) -> dict:
    src = SONGS_DIR / spec.source_file
    if not src.is_file():
        raise FileNotFoundError(f"Missing source audio: {src}")
    out_dir = CHARTS_DIR / spec.chart_id
    out_dir.mkdir(parents=True, exist_ok=True)

    ogg_path = out_dir / "music.ogg"
    if not (skip_audio and ogg_path.is_file()):
        encode_ogg(src, ogg_path, spec.title, spec.artist)

    y = decode_mono(ogg_path)
    an = analyze_audio(y)
    seed_val = sum((i + 1) * ord(ch) for i, ch in enumerate(spec.chart_id))
    render_cover(out_dir / "cover.png", spec, seed_val)

    diff_entries = []
    for dspec in spec.diffs:
        chart_doc = build_chart_v3(an, dspec)
        valid_links = {n.get("link_id", 0) for n in chart_doc["keyboard_notes"]} - {0}
        for mn in chart_doc["mouse_notes"]:
            if mn.get("link_id", 0) not in valid_links:
                mn.pop("link_id", None)
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
        "version": 3,
        "id": spec.chart_id,
        "title": spec.title,
        "artist": spec.artist,
        "charter": "Sakura Chart Studio",
        "source": "Sakura Official Pack · Vol. 03",
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
    print(f"[song-pack] Built {spec.chart_id} ({spec.title}): {an['duration']:.1f}s @ {an['bpm']} BPM, {len(diff_entries)} diffs")
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
    args = parser.parse_args()

    album_path = CHARTS_DIR / "album.json"
    existing = json.loads(album_path.read_text(encoding="utf-8")) if album_path.is_file() else []
    pack_ids = {s.chart_id for s in PACK_SPECS}
    kept = [entry for entry in existing if entry.get("id") not in pack_ids]

    for spec in PACK_SPECS:
        kept.append(build_track(spec, skip_audio=args.skip_audio))

    album_path.write_text(json.dumps(kept, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"[song-pack] Updated {album_path} with {len(kept)} album entries.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
