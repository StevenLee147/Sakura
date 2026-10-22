"""Build the built-in song 「樱落呼吸」 (resources/charts/sakura_breath) from audio analysis.

The music was generated with Suno by the Sakura project author on a paid plan (the author owns
it). The source MP3 lives in the git-ignored ``download/`` folder and is never committed; this
script converts it to ``music.ogg`` and derives three charts (Normal / Hard / Expert) from the
audio itself, so everything can be rebuilt reproducibly from the repository root:

    python scripts/build_sakura_breath.py --source "download/songs/樱花落下的呼吸 (2).mp3"

Options: ``--preview-dir <dir>`` also renders review piano-rolls (20 s of drop A per difficulty),
``--skip-audio`` keeps an existing ``music.ogg`` (charts, cover and validation are still rebuilt).
Running twice produces byte-identical files; the SHA-256 list printed at the end shows it.

Requirements: Python 3.11+, numpy, scipy, Pillow and ``ffmpeg`` on PATH. Decoding goes through
ffmpeg so the MP3 encoder delay is honoured; ``music.ogg`` keeps exactly that timeline (no trim).

Pipeline (numpy/scipy only, no randomness):
  * Grid: constant 178 BPM. Percussive transients (5 kHz+ envelope, 50 % rise) sit on
    ``t_j = 0.3185 s + (j - 1) * 60 / 178``; bars and sections start at ``j % 4 == 0`` (drop A
    j=64 at 21.55 s, drop B j=280 at 94.36 s, drop C j=368 at 124.03 s). Times are integer ms.
  * STFT of the 24 kHz mono mix (1024-point Hann, 10 ms hop) and HPSS with
    ``scipy.ndimage.median_filter``. Drum onsets: lagged log-energy band flux (level 10-40 ms
    after minus 30-60 ms before, floored 30 dB under the local maximum), peak-picked and snapped
    to the 16th grid -- kick 75-140 Hz, snare body 140-280 Hz (minus kick leakage), sub bass
    38-62 Hz, stabs 270 Hz-3 kHz, hats 3.4-12 kHz (percussive part). Rises one 16th after a
    kick/snare are side-chain releases and are ignored. Melody onsets: log spectral flux of the
    harmonic part (200-4000 Hz), adaptively thresholded and normalised over +-8 beats.
  * Melody pitch: harmonic-sum salience (C4-F#6) on a 4096-point harmonic spectrogram, median
    smoothed and octave-folded toward the local median; sustain = how long that pitch holds.
  * Keyboard = left hand, rhythm: each bar gets a share of the section's note budget that follows
    its drum activity; the strongest onsets on the difficulty's grid win. Kick/bass -> lane A,
    snare -> lane F (the neighbouring lane on quick repeats; Normal alternates), stabs/hats flow
    on S/D, single-instrument rolls become stairs, chords on multi-band accents, holds on section
    and phrase downbeats and on the bar before a drop.
  * Mouse = right hand, melody: strongest melodic onsets (8th grid), sustained notes -> sliders of
    the same length; pitch -> height (higher = up), x sweeps across the area and bounces so
    phrases read left-right-left; every move is checked against a travel-speed limit.
  * Every chart is validated against ChartLoader::ValidateChartData plus playability rules and a
    report (densities, gaps, travel speed, level calibration against the library) is printed.
"""
from __future__ import annotations

import argparse
import bisect
import hashlib
import json
import math
import subprocess
import sys
from dataclasses import dataclass, field
from pathlib import Path

import numpy as np
from scipy import ndimage, signal

ROOT = Path(__file__).resolve().parents[1]
CHART_ID = "sakura_breath"
TITLE = "樱落呼吸"
ARTIST = "Sakura Soundworks"
COMMENT = "Generated with Suno by the Sakura project author (paid plan; owned by the author)."
OUT_DIR = ROOT / "resources" / "charts" / CHART_ID
DEFAULT_SOURCE = ROOT / "download" / "songs" / "樱花落下的呼吸 (2).mp3"

SR = 48000
BPM = 178.0
BEAT = 60.0 / BPM
FIRST_TRANSIENT = 0.3185  # measured: first grid beat (j = 1); see module docstring
GRID0 = FIRST_TRANSIENT - BEAT  # beat j = 0 (just before the file starts)
TIMING_BEAT = 4  # first downbeat inside the file -> timing point
NOTE_START = 1.5  # seconds; no notes earlier
NOTE_END = 191.0  # seconds; the outro fades to silence here

# (name, first beat, end beat, kind) -- beat indices j, bars start at j % 4 == 0. "riser" is the
# kick roll that closes the second break (charted as a build-up, no hold on its first beat).
SECTIONS = [
    ("intro", 0, 32, "quiet"),
    ("build1", 32, 64, "build"),
    ("dropA", 64, 200, "drop"),
    ("break1", 200, 240, "quiet"),
    ("build2", 240, 280, "build"),
    ("dropB", 280, 328, "drop"),
    ("break2", 328, 352, "quiet"),
    ("build3", 352, 368, "riser"),
    ("dropC", 368, 488, "drop"),
    ("outro", 488, 576, "quiet"),
]
# Near-silent beats before drops: never place notes there.
SILENT_BEATS = {279, 365, 366}


def beat_time(j: float) -> float:
    return GRID0 + j * BEAT


def step_time(s: int) -> float:
    """16th-note step s (s = 4 * j + q)."""
    return GRID0 + s * BEAT / 4


def ms(seconds: float) -> int:
    return int(round(seconds * 1000.0))


def section_of(j: float) -> tuple[str, int, int, str]:
    for sec in SECTIONS:
        if sec[1] <= j < sec[2]:
            return sec
    return SECTIONS[-1]


# ── Audio I/O ────────────────────────────────────────────────────────────────

def decode(source: Path) -> np.ndarray:
    """Decode with ffmpeg (honours the MP3 encoder delay) to float32 stereo at 48 kHz."""
    raw = subprocess.run(
        ["ffmpeg", "-v", "error", "-nostdin", "-i", str(source), "-map", "0:a:0", "-f", "f32le",
         "-acodec", "pcm_f32le", "-ac", "2", "-ar", str(SR), "-"],
        capture_output=True, check=True).stdout
    return np.frombuffer(raw, dtype="<f4").reshape(-1, 2)


def encode_ogg(source: Path, dest: Path) -> None:
    """Vorbis q6, 48 kHz stereo, same timeline as the decode; bit-exact flags keep it reproducible."""
    tmp = dest.with_suffix(".tmp.ogg")
    try:
        subprocess.run(
            ["ffmpeg", "-v", "error", "-nostdin", "-y", "-i", str(source), "-map", "0:a:0", "-vn",
             "-map_metadata", "-1", "-map_chapters", "-1", "-fflags", "+bitexact", "-flags:a", "+bitexact",
             "-ac", "2", "-ar", str(SR), "-c:a", "libvorbis", "-q:a", "6",
             "-metadata", f"title={TITLE}", "-metadata", f"artist={ARTIST}", "-metadata", f"comment={COMMENT}",
             str(tmp)],
            check=True)
        tmp.replace(dest)
    finally:
        tmp.unlink(missing_ok=True)


# ── Analysis ─────────────────────────────────────────────────────────────────

AN_SR = 24000
N_FFT = 1024
HOP = 240  # 10 ms
FPS = AN_SR / HOP


def stft_mag(y: np.ndarray, n_fft: int, hop: int) -> np.ndarray:
    """Centered Hann STFT magnitude, shape (bins, frames); frame f is centered at f * hop."""
    window = signal.get_window("hann", n_fft).astype(np.float32)
    padded = np.pad(y.astype(np.float32), (n_fft // 2, n_fft // 2))
    frames = 1 + (len(padded) - n_fft) // hop
    out = np.empty((n_fft // 2 + 1, frames), dtype=np.float32)
    chunk = 2048
    for start in range(0, frames, chunk):
        stop = min(frames, start + chunk)
        idx = np.arange(n_fft)[None, :] + hop * np.arange(start, stop)[:, None]
        out[:, start:stop] = np.abs(np.fft.rfft(padded[idx] * window, axis=1)).T
    return out


def hpss(mag: np.ndarray, time_kernel: int, freq_kernel: int) -> tuple[np.ndarray, np.ndarray]:
    """Median-filter harmonic/percussive separation with soft (Wiener) masks."""
    harmonic = ndimage.median_filter(mag, size=(1, time_kernel), mode="nearest")
    percussive = ndimage.median_filter(mag, size=(freq_kernel, 1), mode="nearest")
    h2, p2 = harmonic.astype(np.float64) ** 2, percussive.astype(np.float64) ** 2
    total = h2 + p2 + 1e-12
    return (mag * (h2 / total)).astype(np.float32), (mag * (p2 / total)).astype(np.float32)


def band_flux(mag: np.ndarray, freqs: np.ndarray, lo: float, hi: float, gamma: float = 100.0) -> np.ndarray:
    """Log-compressed positive spectral flux summed over a band (one value per frame)."""
    band = np.log1p(gamma * mag[(freqs >= lo) & (freqs < hi)].astype(np.float64))
    flux = np.maximum(0.0, np.diff(band, axis=1)).sum(axis=0)
    return np.concatenate([[0.0], flux])


def band_db(mag: np.ndarray, freqs: np.ndarray, lo: float, hi: float) -> np.ndarray:
    energy = (mag[(freqs >= lo) & (freqs < hi)].astype(np.float64) ** 2).sum(axis=0)
    return 10.0 * np.log10(energy + 1e-12)


def window_mean(x: np.ndarray, a: int, b: int) -> np.ndarray:
    """For every frame f: mean of x[f + a .. f + b] (inclusive), edges padded."""
    pad = max(abs(a), abs(b)) + 1
    padded = np.pad(x, (pad, pad), mode="edge")
    csum = np.concatenate([[0.0], np.cumsum(padded)])
    idx = np.arange(len(x)) + pad
    return (csum[idx + b + 1] - csum[idx + a]) / (b - a + 1)


def band_rise(level_db: np.ndarray) -> np.ndarray:
    """Lagged band flux in dB: level 10-40 ms after a frame minus level 30-60 ms before it.

    The gap between the two windows skips the 43 ms analysis window, so a transient yields one
    clean peak at its onset while slow swells (side-chain pumping, pads) stay small. Levels are
    floored 30 dB under the local maximum so gated silences cannot inflate the next onset.
    """
    floor = ndimage.maximum_filter1d(level_db, size=int(4 * FPS) | 1, mode="nearest") - 30.0
    level = np.maximum(level_db, floor)
    return window_mean(level, 1, 4) - window_mean(level, -6, -3)


def quantize_peaks(env: np.ndarray, steps: int, height: float, tolerance: float = 0.030) -> np.ndarray:
    """Peak-pick an onset envelope and snap the peaks to the 16th grid (max value per step)."""
    peaks, props = signal.find_peaks(env, height=height, distance=6)
    out = np.zeros(steps)
    for frame, value in zip(peaks, props["peak_heights"]):
        t = frame / FPS
        s = int(round((t - GRID0) / (BEAT / 4)))
        if 0 <= s < steps and abs(t - step_time(s)) <= tolerance and value > out[s]:
            out[s] = value
    return out


@dataclass
class Analysis:
    duration: float
    steps: int
    kick: np.ndarray  # per 16th step: dB rise of the kick band (0 = no onset)
    snare: np.ndarray  # dB rise of the snare body band (kick leakage removed)
    hat: np.ndarray  # dB rise of the percussive high band
    bass: np.ndarray  # dB rise of the sub band (bass notes / side-chain returns)
    broad: np.ndarray  # dB rise of the 270 Hz-3 kHz band (stabs, crashes, plucks)
    melody: np.ndarray  # harmonic onset strength, normalised locally (~1 = strong)
    pitch: np.ndarray  # MIDI pitch estimate after the step (nan when unpitched)
    sustain: np.ndarray  # how many 16th steps the melodic note starting here keeps ringing
    level: np.ndarray  # full-band level (dB, 0 = loudest)
    extras: dict = field(default_factory=dict)


def melody_pitch(y: np.ndarray) -> np.ndarray:
    """Predominant pitch (MIDI) per 20 ms frame from harmonic-sum salience of the harmonic part.

    Returns rows (time s, MIDI pitch, salience).
    """
    n_fft, hop = 4096, 480
    mag = stft_mag(y, n_fft, hop)
    harmonic, _ = hpss(mag, time_kernel=11, freq_kernel=31)
    freqs = np.fft.rfftfreq(n_fft, 1.0 / AN_SR)
    comp = np.sqrt(harmonic.astype(np.float64))
    cands = np.arange(60.0, 90.0, 0.5)  # C4 .. F#6: lead / vocal register
    salience = np.zeros((len(cands), comp.shape[1]))
    bin_hz = freqs[1]

    def at(freq: float) -> np.ndarray:
        b = freq / bin_hz
        b0 = int(b)
        a = b - b0
        return (1 - a) * comp[b0] + a * comp[b0 + 1]

    for i, m in enumerate(cands):
        f0 = 440.0 * 2.0 ** ((m - 69.0) / 12.0)
        for h, weight in zip(range(1, 6), (1.0, 0.75, 0.55, 0.4, 0.3)):
            if f0 * h / bin_hz + 1 >= len(freqs):
                break
            salience[i] += weight * at(f0 * h)
        # Energy at f0/2 means the candidate is probably a harmonic of a lower note.
        salience[i] -= 0.35 * at(f0 / 2)
    best = np.argmax(salience, axis=0)
    strength = salience[best, np.arange(salience.shape[1])]
    pitch = ndimage.median_filter(cands[best], size=5, mode="nearest")
    times = np.arange(len(pitch)) * hop / AN_SR
    return np.vstack([times, pitch, strength])


def local_normalize(values: np.ndarray, size: int = 129, pct: float = 90.0, floor: float = 0.3) -> np.ndarray:
    """Divide by a running percentile (about +-8 beats) so quiet sections keep readable onsets."""
    ref = ndimage.percentile_filter(values, pct, size=size, mode="nearest")
    ref = np.maximum(ref, floor * np.percentile(values[values > 0], pct))
    return values / ref


def analyze(samples: np.ndarray) -> Analysis:
    mono = samples.astype(np.float64).mean(axis=1)
    y = signal.resample_poly(mono, 1, SR // AN_SR)
    duration = len(mono) / SR
    mag = stft_mag(y, N_FFT, HOP)
    freqs = np.fft.rfftfreq(N_FFT, 1.0 / AN_SR)
    harmonic, percussive = hpss(mag, time_kernel=31, freq_kernel=17)
    steps = int((duration - GRID0) / (BEAT / 4))

    full_db = band_db(mag, freqs, 30, 12000)
    audible = window_mean(full_db, 1, 4) > np.percentile(full_db, 99.5) - 42.0
    rises = {
        "kick": band_rise(band_db(mag, freqs, 75, 140)),
        "snare": band_rise(band_db(mag, freqs, 140, 280)),
        "bass": band_rise(band_db(mag, freqs, 38, 62)),
        "broad": band_rise(band_db(mag, freqs, 270, 3000)),
        "hat": band_rise(band_db(percussive, freqs, 3400, 12000)),
    }
    # The kick sweep also excites the snare-body band; keep only what the kick does not explain.
    rises["snare"] = rises["snare"] - 0.6 * np.maximum(0.0, rises["kick"])
    drums = {}
    for name, env in rises.items():
        drums[name] = quantize_peaks(np.where(audible, env, 0.0), steps, height=3.0)

    melodic_flux = band_flux(harmonic, freqs, 200, 4000)
    floor = ndimage.median_filter(melodic_flux, size=int(0.5 * FPS) | 1, mode="nearest")
    melody = local_normalize(quantize_peaks(np.where(audible, melodic_flux - floor, 0.0), steps, height=1e-6))

    level = np.zeros(steps)
    for s in range(steps):
        f0 = int(round(step_time(s) * FPS))
        f1 = max(f0 + 1, int(round(step_time(s + 1) * FPS)))
        level[s] = full_db[max(0, f0):max(1, f1)].mean()
    level -= np.percentile(level, 99.5)

    track = melody_pitch(y)
    tt, tp, ts = track
    pitch = np.full(steps, np.nan)
    pstrength = np.zeros(steps)
    for s in range(steps):
        sel = (tt >= step_time(s) + 0.03) & (tt < step_time(s) + 0.15)
        if sel.any():
            pitch[s] = float(np.median(tp[sel]))
            pstrength[s] = float(np.median(ts[sel]))
    # Octave errors of the salience peak: fold each value toward the local (+-2 beats) median.
    filled = np.where(np.isnan(pitch), np.nanmedian(pitch), pitch)
    local = ndimage.median_filter(filled, size=33, mode="nearest")
    offset = pitch - local
    pitch = np.where(np.abs(offset) > 7.0, pitch - 12.0 * np.round(offset / 12.0), pitch)

    # Sustain: 16th steps the onset pitch keeps sounding (one-step blips of the pitch track are
    # tolerated) until a clear new note, a pitch move of more than a semitone or a 12 dB drop.
    sustain = np.ones(steps, dtype=int)
    for s in range(steps):
        if np.isnan(pitch[s]):
            continue
        n, miss = 1, 0
        while s + n < steps and n < 32:
            q = pitch[s + n]
            moved = bool(np.isnan(q) or abs(q - pitch[s]) > 1.0)
            if level[s + n] < level[s] - 12.0 or (moved and melody[s + n] > 0.9):
                break
            miss = miss + 1 if moved else 0
            if miss > 1:
                break
            n += 1
        sustain[s] = max(1, n - miss)

    return Analysis(duration=duration, steps=steps, kick=drums["kick"], snare=drums["snare"], hat=drums["hat"],
                    bass=drums["bass"], broad=drums["broad"], melody=melody, pitch=pitch, sustain=sustain,
                    level=level, extras={"pitch_strength": pstrength, "track": track,
                                         "pitch_median": float(np.nanmedian(pitch[melody > 0.3]))})


# ── Chart generation ─────────────────────────────────────────────────────────

ASPECT = (16 / 9 * 0.50) / 0.66  # mouse play area width / height (scene_game MOUSE_W / MOUSE_H)
AREA_MIN, AREA_MAX = 0.12, 0.88  # circle / slider placement range (validated against 0.1..0.9)


def section_rates(intro, build1, drop_a, break1, build2, drop_b, break2, build3, drop_c, outro) -> dict:
    return dict(intro=intro, build1=build1, dropA=drop_a, break1=break1, build2=build2, dropB=drop_b,
                break2=break2, build3=build3, dropC=drop_c, outro=outro)


@dataclass(frozen=True)
class Difficulty:
    name: str
    level: float
    grid: int  # keyboard grid in 16th steps: 4 = beats, 2 = 8ths, 1 = 16ths
    min_gap: int  # minimum spacing between keyboard notes, 16th steps
    lane_gap: float  # minimum spacing inside one lane, beats
    min_strength: float  # weakest drum onset that may become a keyboard note
    kb_rate: dict  # section -> keyboard notes per second (chord notes counted)
    chord_share: float  # share of keyboard notes that belong to 2-note chords
    chord_spacing: int  # minimum distance between chords, beats
    hold_beats: tuple  # (in drops, elsewhere)
    mouse_rate: dict  # section -> mouse notes per second
    mouse_gap: int  # minimum spacing between mouse note starts, 16th steps
    slider_beats: tuple  # (shortest, longest) slider
    slider_quantum: float  # slider length granularity, beats
    slider_rest: float  # free time after a slider ends, beats
    speed: float  # maximum cursor travel between mouse targets, play-area units per second
    waypoints: tuple  # (min, max) slider path points
    sv: bool


DIFFICULTIES = [
    # Levels: the report fits level = a + b * peak load on the built-in Sakura Originals charts. These
    # charts land at 3.2 / 6.3 / 9.3 on that scale; +1.0 accounts for what the fit cannot see (a song
    # ~3x longer than the library tracks and syncopated real-drum rhythms instead of fixed patterns),
    # rounded to half levels.
    Difficulty("Normal", 4.0, grid=4, min_gap=4, lane_gap=1.0, min_strength=0.3,
               kb_rate=section_rates(0.65, 1.15, 1.75, 0.55, 1.25, 1.8, 0.6, 1.4, 1.75, 0.5),
               chord_share=0.0, chord_spacing=0, hold_beats=(2.0, 3.0),
               mouse_rate=section_rates(0.3, 0.35, 0.45, 0.3, 0.35, 0.47, 0.3, 0.3, 0.45, 0.26),
               mouse_gap=8, slider_beats=(2.0, 4.0), slider_quantum=1.0, slider_rest=1.0, speed=0.7,
               waypoints=(2, 2), sv=False),
    Difficulty("Hard", 7.5, grid=2, min_gap=2, lane_gap=1.0, min_strength=0.2,
               kb_rate=section_rates(1.45, 2.6, 3.35, 1.35, 2.8, 3.45, 1.3, 3.0, 3.35, 1.25),
               chord_share=0.05, chord_spacing=8, hold_beats=(1.5, 3.0),
               mouse_rate=section_rates(0.7, 0.8, 1.1, 0.7, 0.85, 1.12, 0.7, 0.7, 1.1, 0.6),
               mouse_gap=4, slider_beats=(1.5, 3.0), slider_quantum=0.5, slider_rest=1.0, speed=1.1,
               waypoints=(2, 3), sv=False),
    Difficulty("Expert", 10.5, grid=1, min_gap=1, lane_gap=0.5, min_strength=0.12,
               kb_rate=section_rates(2.6, 4.3, 5.7, 2.4, 4.5, 5.8, 2.5, 5.0, 5.7, 2.3),
               chord_share=0.1, chord_spacing=2, hold_beats=(1.5, 3.0),
               mouse_rate=section_rates(1.1, 1.3, 1.8, 1.1, 1.4, 1.85, 1.1, 1.1, 1.8, 1.0),
               mouse_gap=2, slider_beats=(1.0, 4.0), slider_quantum=0.5, slider_rest=0.5, speed=1.6,
               waypoints=(2, 4), sv=True),
]

KICK, SNARE, BASS, STAB, HAT = range(5)


def drum_table(an: Analysis) -> tuple[np.ndarray, np.ndarray, np.ndarray]:
    """Per step: dominant drum class, onset strength (~1 = typical strong hit) and multi-band accent."""
    bass = an.bass.copy()
    for s in range(2, an.steps):
        # A sub-bass rise one or two 16ths after a kick/snare is the side-chain releasing, not a hit.
        if max(an.kick[s - 1], an.kick[s - 2], an.snare[s - 1], an.snare[s - 2]) >= 9.0:
            bass[s] = 0.0
    loud = np.zeros(an.steps, dtype=bool)
    for _, j0, j1, kind in SECTIONS:
        if kind == "drop":
            loud[4 * j0:4 * j1] = True

    def norm(values: np.ndarray) -> np.ndarray:
        return values / np.percentile(values[loud & (values > 0)], 90)

    # Kick and snare lead; bass notes, stabs and hats fill in.
    rows = np.vstack([norm(an.kick), norm(an.snare), 0.8 * norm(bass), 0.75 * norm(an.broad), 0.7 * norm(an.hat)])
    rows = np.minimum(rows, 1.6)
    # Anything rising one 16th after a strong kick/snare is the side-chain duck releasing.
    pumped = np.zeros(an.steps, dtype=bool)
    pumped[1:] = np.maximum(rows[KICK], rows[SNARE])[:-1] >= 0.6
    rows[2:, pumped] = 0.0
    best = rows.max(axis=0)
    cls = np.argmax(rows, axis=0)
    cls = np.where((rows[KICK] >= 0.6) & (rows[KICK] >= 0.75 * best), KICK,
                   np.where((rows[SNARE] >= 0.6) & (rows[SNARE] >= 0.75 * best), SNARE, cls))
    return cls, best, rows.sum(axis=0)


def position_weight(s: int) -> float:
    p = s % 16
    if p == 0:
        return 1.3
    if p in (4, 12):
        return 1.18
    if p == 8:
        return 1.1
    return 1.0 if s % 2 == 0 else 0.85


def step_allowed(s: int) -> bool:
    t = step_time(s)
    return NOTE_START <= t <= NOTE_END - 0.05 and (s // 4) not in SILENT_BEATS


def groove_weight(score: np.ndarray, s0: int, s1: int) -> np.ndarray:
    """Weight steps by how typical their place in the 2-bar groove is (keeps repeats consistent)."""
    seg = score[s0:s1]
    template = np.array([seg[p::32].mean() if len(seg[p::32]) else 0.0 for p in range(32)])
    template /= template.max() + 1e-9
    return 0.6 + 0.4 * template[(np.arange(s0, s1) - s0) % 32]


def section_span(j0: int, j1: int) -> float:
    return max(0.0, min(beat_time(j1), NOTE_END) - max(beat_time(j0), NOTE_START))


def select_keyboard(an: Analysis, diff: Difficulty) -> list[int]:
    """Pick keyboard steps bar by bar: each bar gets a share of the section's note budget that
    follows its drum activity, and inside a bar the strongest onsets on the allowed grid win."""
    _, strength, _ = drum_table(an)
    strength = np.minimum(strength, 1.6)  # one huge re-entry after a drop-out must not dominate
    score = strength * np.array([position_weight(s) for s in range(an.steps)])
    picked: list[int] = []

    def acceptable(s: int) -> bool:
        if s % diff.grid or not step_allowed(s):
            return False
        if s % 2 and strength[s] < 0.55:
            return False  # off-8th (16th) positions only for clear hits
        near = picked[bisect.bisect_left(picked, s - 8):bisect.bisect_right(picked, s + 8)]
        if any(abs(q - s) < diff.min_gap for q in near):
            return False
        if any(abs(q - s) == 1 and min(strength[q], strength[s]) < 0.45 for q in near):
            return False  # 16th pairs only where the drums really play both 16ths
        run = sorted(near + [s])
        i = run.index(s)
        lo = hi = i
        while lo > 0 and run[lo] - run[lo - 1] == 1:
            lo -= 1
        while hi + 1 < len(run) and run[hi + 1] - run[hi] == 1:
            hi += 1
        return hi - lo + 1 <= 4  # 16th bursts stay short

    for name, j0, j1, kind in SECTIONS:
        s0, s1 = 4 * j0, min(4 * j1, an.steps)
        if s1 <= s0 or section_span(j0, j1) <= 0:
            continue
        eff = score[s0:s1] * groove_weight(score, s0, s1)
        bars = list(range(s0, s1, 16))
        activity = np.array([np.sum(np.where(strength[b:b + 16] >= diff.min_strength, strength[b:b + 16], 0.0))
                             for b in bars])
        usable = np.array([sum(1 for s in range(b, min(b + 16, s1)) if step_allowed(s)) / 16 for b in bars])
        rel = np.clip(np.sqrt(activity / max(1e-9, np.mean(activity[usable > 0]))), 0.3, 1.45) * usable
        per_bar = diff.kb_rate[name] * 16 * BEAT / 4 / (1.0 + diff.chord_share)
        target = diff.kb_rate[name] * section_span(j0, j1) / (1.0 + diff.chord_share)
        scale = target / max(1e-9, per_bar * rel.sum())
        carry = 0.0
        for b, r in zip(bars, rel):
            want = per_bar * r * scale + carry
            n = int(math.floor(want + 0.5))
            carry = want - n
            local = [s for s in range(b, min(b + 16, s1)) if strength[s] >= diff.min_strength]
            chosen = 0
            for s in sorted(local, key=lambda s: (-eff[s - s0], s)):
                if chosen >= n:
                    break
                if acceptable(s):
                    bisect.insort(picked, s)
                    chosen += 1
            carry += n - chosen if chosen < n else 0.0
            carry = min(carry, 2.0)
    return sorted(picked)


@dataclass
class KeyEvent:
    step: int
    cls: int
    chord: bool = False
    hold: float = 0.0  # beats
    lanes: tuple = ()


def plan_keyboard(an: Analysis, diff: Difficulty) -> list[KeyEvent]:
    cls, strength, accent = drum_table(an)
    events = [KeyEvent(s, int(cls[s])) for s in select_keyboard(an, diff)]

    # Holds start exactly on section openings, quiet-phrase openings and the last bar of each
    # build-up -- wherever the music actually sounds a new chord/hit on that downbeat.
    anchors: list[tuple[int, float]] = []
    for name, j0, j1, kind in SECTIONS:
        length = diff.hold_beats[0] if kind == "drop" else diff.hold_beats[1]
        if kind != "riser":
            anchors.append((4 * j0, length))
        if kind == "quiet":
            anchors.extend((4 * j, length) for j in range(j0 + 16, j1, 16))
        if kind == "build" and diff.name != "Normal":
            anchors.append((4 * (j1 - 4), 2.0))
    for s, length in anchors:
        if not step_allowed(s) or step_time(s) + length * BEAT > NOTE_END:
            continue
        if any(int(j) in SILENT_BEATS for j in np.arange(s / 4, s / 4 + length + 0.5, 0.25)):
            continue
        ev = next((e for e in events if e.step == s), None)
        if ev is None:
            if strength[s] < 0.15:
                continue
            events = [e for e in events if abs(e.step - s) >= diff.min_gap]
            ev = KeyEvent(s, int(cls[s]))
            events.append(ev)
            events.sort(key=lambda e: e.step)
        ev.hold = length
    if diff.name == "Normal":
        # Normal keeps the hand free during a hold: nothing else starts until it is released.
        kept = []
        busy_until = -1.0
        for e in events:
            t = step_time(e.step)
            if t < busy_until - 1e-6:
                continue
            kept.append(e)
            if e.hold:
                busy_until = t + (e.hold + 0.5) * BEAT
        events = kept

    # Chords on accents (never in the quiet parts): strongest multi-band hits, spaced out.
    if diff.chord_share > 0:
        quota = round(diff.chord_share * len(events) / (1 + diff.chord_share))
        isolated = []
        for i, e in enumerate(events):
            kind = section_of(e.step / 4)[3]
            prev_gap = e.step - events[i - 1].step if i else 99
            next_gap = events[i + 1].step - e.step if i + 1 < len(events) else 99
            if kind == "quiet" or e.hold or min(prev_gap, next_gap) < 2 or accent[e.step] < 1.4:
                continue
            if kind in ("build", "riser") and e.step % 16 and accent[e.step] < 2.2:
                continue
            isolated.append(e)
        ranked = sorted(isolated, key=lambda e: (-accent[e.step] * position_weight(e.step), e.step))
        taken: list[int] = []
        for e in ranked:
            if len(taken) >= quota:
                break
            if any(abs(e.step - q) < 4 * diff.chord_spacing for q in taken):
                continue
            e.chord = True
            taken.append(e.step)
    assign_lanes(events, diff)
    return events


def assign_lanes(events: list[KeyEvent], diff: Difficulty) -> None:
    """Kick/bass anchor lane A and snare lane F (moving inward on quick repeats; Normal alternates),
    stabs/hats flow on S/D, single-instrument rolls climb as stairs."""
    free_at = [-1e9] * 4  # time a lane may take a new note (hold end + half beat)
    last_at = [-1e9] * 4
    hold_end = [-1e9] * 4
    lane_gap = diff.lane_gap * BEAT - 1e-6
    prev_lane, prev_time = 1, -1e9
    # Rolls: >= 4 hits of one instrument at a constant 8th/16th spacing become stairs.
    stair = {}
    i, rolls = 0, 0
    while i < len(events):
        k = i
        if i + 1 < len(events) and events[i + 1].step - events[i].step <= 2:
            spacing = events[i + 1].step - events[i].step
            while (k + 1 < len(events) and events[k + 1].step - events[k].step == spacing
                   and events[k + 1].cls == events[i].cls and not events[k + 1].chord and not events[k + 1].hold):
                k += 1
        if k - i + 1 >= 4 and not events[i].chord and not events[i].hold:
            for n, e in enumerate(events[i:k + 1]):
                stair[e.step] = n if rolls % 2 == 0 else 3 - (n % 4)
            rolls += 1
            i = k + 1
        else:
            i += 1

    def usable(lane: int, t: float) -> bool:
        return t >= free_at[lane] - 1e-6 and t - last_at[lane] >= lane_gap

    last_of_class: dict[int, tuple[int, float]] = {}
    for e in events:
        t = step_time(e.step)
        close = t - prev_time < BEAT + 1e-6
        held = sum(1 for end in hold_end if end > t + 1e-6)
        if held >= 2:
            continue
        if e.chord and held:
            e.chord = False  # never more than two keys down at once
        if e.chord:
            pairs = [(0, 3), (1, 2), (0, 2), (1, 3), (0, 1), (2, 3)]
            pair = next((p for p in pairs if usable(p[0], t) and usable(p[1], t)), None)
            if pair is None:
                e.chord = False
            else:
                e.lanes = pair
        if not e.lanes:
            group = KICK if e.cls in (KICK, BASS) else SNARE if e.cls == SNARE else STAB
            recent = last_of_class.get(group)
            repeat = recent is not None and t - recent[1] < BEAT + 1e-6
            if e.step in stair:
                n = stair[e.step] % 4
                order = [n, (n + 1) % 4, (n + 3) % 4, (n + 2) % 4]
            elif group == KICK:
                alternate = repeat or (diff.grid == 4 and recent is not None and recent[0] == 0)
                order = [1, 0] if alternate and recent[0] == 0 else [0, 1]
                order += [2, 3]
            elif group == SNARE:
                alternate = repeat or (diff.grid == 4 and recent is not None and recent[0] == 3)
                order = [2, 3] if alternate and recent[0] == 3 else [3, 2]
                order += [1, 0]
            else:
                order = [2, 1] if recent is not None and recent[0] == 1 else [1, 2]
                order += [0, 3] if prev_lane >= 2 else [3, 0]
            if close:
                # Avoid jacks when a different lane is free.
                order = [l for l in order if l != prev_lane] + [prev_lane]
            lane = next((l for l in order if usable(l, t)), None)
            if lane is None:
                e.lanes = ()
                continue
            e.lanes = (lane,)
            last_of_class[group] = (lane, t)
        for lane in e.lanes:
            last_at[lane] = t
            free_at[lane] = t + (e.hold + 0.5) * BEAT if e.hold else t
            if e.hold:
                hold_end[lane] = t + e.hold * BEAT
        prev_lane = e.lanes[-1] if len(e.lanes) == 1 else e.lanes[0]
        prev_time = t
    events[:] = [e for e in events if e.lanes]


@dataclass
class MouseEvent:
    step: int
    beats: float = 0.0  # slider length (0 = circle)
    x: float = 0.5
    y: float = 0.5
    path: list = field(default_factory=list)


def pitch_reference(an: Analysis, s: int) -> float:
    lo, hi = max(0, s - 64), min(an.steps, s + 64)
    window = an.pitch[lo:hi][an.melody[lo:hi] > 0.3]
    window = window[~np.isnan(window)]
    local = float(np.median(window)) if len(window) else an.extras["pitch_median"]
    return 0.5 * local + 0.5 * an.extras["pitch_median"]


def pitch_to_y(an: Analysis, s: int, pitch: float) -> float:
    if np.isnan(pitch):
        return 0.5
    return float(np.clip(0.5 - (pitch - pitch_reference(an, s)) * 0.034, 0.16, 0.84))


def select_mouse(an: Analysis, diff: Difficulty) -> list[MouseEvent]:
    accepted: list[MouseEvent] = []

    def span(ev: MouseEvent) -> tuple[float, float]:
        start = ev.step / 4
        return start, start + ev.beats

    def fits(ev: MouseEvent, gap_steps: int) -> bool:
        a0, a1 = span(ev)
        for other in accepted:
            b0, b1 = span(other)
            if b0 <= a0:
                rest = diff.slider_rest if other.beats else 0.0
                if a0 - b1 < rest - 1e-9 or a0 - b0 < gap_steps / 4 - 1e-9:
                    return False
            else:
                rest = diff.slider_rest if ev.beats else 0.0
                if b0 - a1 < rest - 1e-9 or b0 - a0 < gap_steps / 4 - 1e-9:
                    return False
        # Quick (< 1 beat) mouse figures stay short: at most three targets in a row.
        starts = sorted([o.step for o in accepted if abs(o.step - ev.step) <= 16] + [ev.step])
        i = starts.index(ev.step)
        lo = hi = i
        while lo > 0 and starts[lo] - starts[lo - 1] < 4:
            lo -= 1
        while hi + 1 < len(starts) and starts[hi + 1] - starts[hi] < 4:
            hi += 1
        return hi - lo + 1 <= 3

    for name, j0, j1, kind in SECTIONS:
        s0, s1 = 4 * j0, min(4 * j1, an.steps)
        if s1 <= s0 or section_span(j0, j1) <= 0:
            continue
        section_gap = max(diff.mouse_gap, 4) if kind == "quiet" else diff.mouse_gap
        target = round(diff.mouse_rate[name] * section_span(j0, j1))
        cands = [s for s in range(s0, s1) if s % 2 == 0 and an.melody[s] >= 0.3 and step_allowed(s)
                 and not np.isnan(an.pitch[s])]
        score = {s: an.melody[s] * (1.15 if s % 4 == 0 else 1.0) * (1 + 0.12 * min(an.sustain[s] / 4, 3.0))
                 for s in cands}
        count = 0
        for s in sorted(cands, key=lambda s: (-score[s], s)):
            if count >= target:
                break
            sustain = an.sustain[s] / 4
            beats = 0.0
            if sustain >= diff.slider_beats[0]:
                q = diff.slider_quantum
                beats = min(diff.slider_beats[1], math.floor(sustain / q) * q)
                while beats >= diff.slider_beats[0] and not all(
                        step_allowed(s + k) for k in range(0, int(round(beats * 4)) + 1, 2)):
                    beats -= q
                if beats < diff.slider_beats[0]:
                    beats = 0.0
            ev = MouseEvent(s, beats)
            if not fits(ev, section_gap):
                if beats and fits(MouseEvent(s, 0.0), section_gap):
                    ev = MouseEvent(s, 0.0)
                else:
                    continue
            accepted.append(ev)
            count += 1
    accepted.sort(key=lambda e: e.step)
    place_mouse(an, diff, accepted)
    return accepted


def distance(a: tuple, b: tuple) -> float:
    """Distance in play-area units with the area's aspect ratio (x stretched like on screen)."""
    return math.hypot((a[0] - b[0]) * ASPECT, a[1] - b[1])


def place_mouse(an: Analysis, diff: Difficulty, events: list[MouseEvent]) -> None:
    """y follows pitch; x sweeps across the area and bounces, so phrases read left-right-left."""
    x, y, direction = 0.3, 0.5, 1.0
    prev_end_t = -1e9
    prev_section = None
    for ev in events:
        t = step_time(ev.step)
        gap = t - prev_end_t
        sec = section_of(ev.step / 4)[0]
        if gap > 3 * BEAT or sec != prev_section:
            direction = 1.0 if x < 0.5 else -1.0  # a new phrase heads back across the area
        prev_section = sec
        gap_beats = min(gap / BEAT, 8.0)
        step_x = float(np.clip(0.06 + 0.07 * gap_beats, 0.10, 0.30))
        nx = x + direction * step_x
        if not AREA_MIN <= nx <= AREA_MAX:
            direction = -direction
            nx = x + direction * step_x
        ny = pitch_to_y(an, ev.step, an.pitch[ev.step])
        if prev_end_t > -1e8:
            limit = diff.speed * gap * 0.92
            d = distance((nx, ny), (x, y))
            if d > limit:
                f = limit / d
                nx, ny = x + (nx - x) * f, y + (ny - y) * f
            if gap < BEAT - 1e-6:
                # Quick consecutive circles must not overlap: keep >= 0.11 apart (plain units).
                while math.hypot(nx - x, ny - y) < 0.11 and AREA_MIN <= nx + direction * 0.01 <= AREA_MAX:
                    nx += direction * 0.01
                if math.hypot(nx - x, ny - y) < 0.11:
                    ny = float(np.clip(y + (0.12 if y < 0.5 else -0.12), 0.16, 0.84))
        ev.x, ev.y = round(float(nx), 4), round(float(ny), 4)
        x, y = ev.x, ev.y
        prev_end_t = t
        if ev.beats:
            n_points = int(np.clip(round(ev.beats), diff.waypoints[0], diff.waypoints[1]))
            length = float(np.clip(0.09 * ev.beats / ASPECT + 0.06, 0.12, 0.34))
            if not AREA_MIN <= x + direction * length <= AREA_MAX:
                direction = -direction
            end_step = min(an.steps - 1, ev.step + int(round(ev.beats * 4)) - 1)
            end_pitch = an.pitch[end_step]
            ex = x + direction * length
            ey = y if np.isnan(end_pitch) else y + (pitch_to_y(an, end_step, end_pitch) - y) * 0.5
            # Quadratic Bezier with a soft bulge toward the centre line: smooth, readable arcs.
            chord = math.hypot(ex - x, ey - y) or 1.0
            nx_, ny_ = -(ey - y) / chord, (ex - x) / chord
            if (0.5 - (y + ey) / 2) * ny_ < 0:
                nx_, ny_ = -nx_, -ny_
            cx, cy = (x + ex) / 2 + nx_ * 0.18 * chord, (y + ey) / 2 + ny_ * 0.18 * chord
            path = []
            for k in range(1, n_points + 1):
                u = k / n_points
                bx = (1 - u) ** 2 * x + 2 * (1 - u) * u * cx + u * u * ex
                by = (1 - u) ** 2 * y + 2 * (1 - u) * u * cy + u * u * ey
                path.append([round(float(np.clip(bx, AREA_MIN, AREA_MAX)), 4),
                             round(float(np.clip(by, AREA_MIN, AREA_MAX)), 4)])
            ev.path = path
            x, y = path[-1]
            prev_end_t = t + ev.beats * BEAT


def sv_points(diff: Difficulty) -> list[dict]:
    """Expert breathes with the song: calmer scroll in quiet parts, a little faster in drops."""
    if not diff.sv:
        return [dict(time=0, speed=1.0, easing="linear")]
    speeds = {"quiet": 0.9, "build": 1.0, "riser": 1.0, "drop": 1.1}
    points: list[dict] = []
    for _, j0, _, kind in SECTIONS:
        if not points or points[-1]["speed"] != speeds[kind]:
            points.append(dict(time=0 if j0 == 0 else ms(beat_time(j0)), speed=speeds[kind], easing="linear"))
    return points


def build_chart(an: Analysis, diff: Difficulty) -> dict:
    keyboard = []
    for e in plan_keyboard(an, diff):
        t = ms(step_time(e.step))
        for lane in e.lanes:
            if e.hold:
                end = ms(step_time(e.step) + e.hold * BEAT)
                keyboard.append(dict(time=t, lane=lane, type="hold", duration=end - t))
            else:
                keyboard.append(dict(time=t, lane=lane, type="tap", duration=0))
    keyboard.sort(key=lambda n: (n["time"], n["lane"]))
    mouse = []
    for m in select_mouse(an, diff):
        t = ms(step_time(m.step))
        if m.beats:
            mouse.append(dict(time=t, x=m.x, y=m.y, type="slider",
                              slider_duration=ms(step_time(m.step) + m.beats * BEAT) - t, slider_path=m.path))
        else:
            mouse.append(dict(time=t, x=m.x, y=m.y, type="circle", slider_duration=0, slider_path=[]))
    return dict(version=2,
                timing_points=[dict(time=ms(beat_time(TIMING_BEAT)), bpm=BPM, time_signature=[4, 4])],
                sv_points=sv_points(diff), keyboard_notes=keyboard, mouse_notes=mouse)


# ── Validation and report ────────────────────────────────────────────────────

MAX_TIME = 3600000


def finite01(v) -> bool:
    return isinstance(v, (int, float)) and math.isfinite(v) and 0.0 <= v <= 1.0


def validate_loader_rules(chart: dict) -> list[str]:
    """Mirror of ChartLoader::ValidateChartData (src/game/chart_loader.cpp)."""
    errors = []
    kb, mouse = chart["keyboard_notes"], chart["mouse_notes"]
    if len(kb) + len(mouse) > 100000:
        errors.append("more than 100000 notes")
    if not chart["timing_points"]:
        errors.append("timing_points is empty")
    previous = -1
    for n in kb:
        t, d = n["time"], n.get("duration", 0)
        if not isinstance(t, int) or t < previous or t < 0 or t > MAX_TIME:
            errors.append(f"keyboard time {t} out of order/range")
        if n["lane"] not in (0, 1, 2, 3) or n["type"] not in ("tap", "hold"):
            errors.append(f"keyboard note {t}: lane/type")
        if d < 0 or d > MAX_TIME - t or (n["type"] == "hold" and d == 0):
            errors.append(f"keyboard note {t}: duration {d}")
        previous = t
    previous = -1
    for n in mouse:
        t, d, path = n["time"], n.get("slider_duration", 0), n.get("slider_path", [])
        if not isinstance(t, int) or t < previous or t < 0 or t > MAX_TIME:
            errors.append(f"mouse time {t} out of order/range")
        if not finite01(n["x"]) or not finite01(n["y"]) or n["type"] not in ("circle", "slider"):
            errors.append(f"mouse note {t}: position/type")
        if d < 0 or d > MAX_TIME - t or len(path) > 1024:
            errors.append(f"mouse note {t}: slider duration/path size")
        if n["type"] == "slider" and (d <= 0 or not path):
            errors.append(f"mouse note {t}: slider needs duration and path")
        if any(len(p) < 2 or not finite01(p[0]) or not finite01(p[1]) for p in path):
            errors.append(f"mouse note {t}: path point outside 0..1")
        previous = t
    previous = -1
    for p in chart["timing_points"]:
        num, den = p.get("time_signature", [4, 4])
        if p["time"] < previous or p["time"] > MAX_TIME or not (0 < p["bpm"] <= 1000) or num < 1 or den < 1:
            errors.append(f"timing point {p}")
        previous = p["time"]
    previous = -1
    for p in chart["sv_points"]:
        if p["time"] < previous or p["time"] > MAX_TIME or not (0 < p["speed"] <= 10) or not math.isfinite(p["speed"]):
            errors.append(f"sv point {p}")
        previous = p["time"]
    return errors


def mouse_end(n: dict) -> tuple[int, tuple[float, float]]:
    if n["type"] == "slider":
        return n["time"] + n["slider_duration"], tuple(n["slider_path"][-1])
    return n["time"], (n["x"], n["y"])


def validate_playability(chart: dict, diff: Difficulty) -> list[str]:
    errors = []
    beat_ms = BEAT * 1000
    kb, mouse = chart["keyboard_notes"], chart["mouse_notes"]
    min_gap = diff.min_gap * beat_ms / 4 - 1
    lane_gap = diff.lane_gap * beat_ms - 1
    silent = [(ms(beat_time(j)), ms(beat_time(j + 1))) for j in sorted(SILENT_BEATS)]

    def in_silence(a: int, b: int) -> bool:
        return any(a < s1 and b >= s0 for s0, s1 in silent)

    times = sorted({n["time"] for n in kb})
    for a, b in zip(times, times[1:]):
        if b - a < min_gap:
            errors.append(f"keyboard gap {b - a} ms at {b}")
    by_lane: dict[int, list[dict]] = {lane: [] for lane in range(4)}
    for n in kb:
        by_lane[n["lane"]].append(n)
        if n["time"] < NOTE_START * 1000 or n["time"] + n["duration"] > NOTE_END * 1000:
            errors.append(f"keyboard note outside {NOTE_START}-{NOTE_END} s at {n['time']}")
        if in_silence(n["time"], n["time"] + n["duration"]):
            errors.append(f"keyboard note in a near-silent beat at {n['time']}")
    for lane, notes in by_lane.items():
        for a, b in zip(notes, notes[1:]):
            if a["type"] == "hold" and b["time"] < a["time"] + a["duration"] + beat_ms / 2 - 1:
                errors.append(f"lane {lane}: note at {b['time']} inside hold {a['time']}+{a['duration']}")
            if b["time"] - a["time"] < lane_gap:
                errors.append(f"lane {lane}: same-lane gap {b['time'] - a['time']} ms at {b['time']}")
    holds = [n for n in kb if n["type"] == "hold"]
    for t in times:
        starting = sum(1 for n in kb if n["time"] == t)
        held = sum(1 for h in holds if h["time"] < t < h["time"] + h["duration"])
        if starting + held > 2:
            errors.append(f"{starting + held} simultaneous keyboard notes at {t}")
        if diff.chord_share == 0 and starting > 1:
            errors.append(f"chord in {diff.name} at {t}")

    rest_ms = diff.slider_rest * beat_ms - 1
    for i, n in enumerate(mouse):
        pts = [(n["x"], n["y"])] + [tuple(p) for p in n["slider_path"]]
        if any(not (0.1 <= c <= 0.9) for p in pts for c in p):
            errors.append(f"mouse note {n['time']}: outside 0.1..0.9")
        end, _ = mouse_end(n)
        if n["time"] < NOTE_START * 1000 or end > NOTE_END * 1000:
            errors.append(f"mouse note outside {NOTE_START}-{NOTE_END} s at {n['time']}")
        if in_silence(n["time"], end):
            errors.append(f"mouse note in a near-silent beat at {n['time']}")
        if n["type"] == "slider":
            seg = [math.hypot(b[0] - a[0], b[1] - a[1]) for a, b in zip(pts, pts[1:])]
            length = sum(seg)
            speed = length / (n["slider_duration"] / 1000)
            if min(seg) < 0.01 or not (0.05 <= speed <= diff.speed):
                errors.append(f"slider {n['time']}: implausible path (length {length:.3f}, {speed:.2f}/s)")
            for a, b, c in zip(pts, pts[1:], pts[2:]):
                v1, v2 = (b[0] - a[0], b[1] - a[1]), (c[0] - b[0], c[1] - b[1])
                cos = (v1[0] * v2[0] + v1[1] * v2[1]) / (math.hypot(*v1) * math.hypot(*v2))
                if cos < math.cos(math.radians(100)):
                    errors.append(f"slider {n['time']}: sharp turn")
        if i == 0:
            continue
        prev = mouse[i - 1]
        prev_end, prev_pos = mouse_end(prev)
        if n["time"] - prev["time"] < diff.mouse_gap * beat_ms / 4 - 1:
            errors.append(f"mouse gap {n['time'] - prev['time']} ms at {n['time']}")
        if prev["type"] == "slider" and n["time"] - prev_end < rest_ms:
            errors.append(f"mouse note at {n['time']} during/just after slider {prev['time']}")
        gap = (n["time"] - prev_end) / 1000
        travel = math.hypot(n["x"] - prev_pos[0], n["y"] - prev_pos[1])
        if gap <= 0 or travel > diff.speed * gap + 1e-9:
            errors.append(f"mouse jump {travel:.3f} in {gap * 1000:.0f} ms at {n['time']}")
        if gap * 1000 < beat_ms - 1 and travel < 0.10:
            errors.append(f"overlapping consecutive targets at {n['time']} ({travel:.3f})")
    return errors


def chart_metrics(chart: dict) -> dict:
    kb, mouse = chart["keyboard_notes"], chart["mouse_notes"]
    times = [n["time"] for n in kb]
    unique = sorted(set(times))
    counts = {t: times.count(t) for t in unique}
    win = [bisect_count(times, t, t + 999) for t in times]
    per_section = {}
    for name, j0, j1, _ in SECTIONS:
        a, b = ms(max(beat_time(j0), NOTE_START)), ms(min(beat_time(j1), NOTE_END))
        span = max(1e-9, (b - a) / 1000)
        k = sum(1 for t in times if a <= t < b)
        m = sum(1 for n in mouse if a <= n["time"] < b)
        per_section[name] = (k / span, m / span)
    lane_gaps = []
    for lane in range(4):
        lt = [n for n in kb if n["lane"] == lane]
        lane_gaps += [b["time"] - a["time"] for a, b in zip(lt, lt[1:])]
    speeds, speeds_aspect, mgaps = [], [], []
    for a, b in zip(mouse, mouse[1:]):
        end, pos = mouse_end(a)
        gap = (b["time"] - end) / 1000
        mgaps.append(b["time"] - a["time"])
        speeds.append(math.hypot(b["x"] - pos[0], b["y"] - pos[1]) / gap)
        speeds_aspect.append(distance((b["x"], b["y"]), pos) / gap)
    span = (max([n["time"] + n["duration"] for n in kb] + [mouse_end(n)[0] for n in mouse])
            - min(times + [n["time"] for n in mouse])) / 1000
    peak8 = max(bisect_count(times, t, t + 7999) for t in times) / 8.0
    mouse_times = [n["time"] for n in mouse]
    mouse_peak8 = max((bisect_count(mouse_times, t, t + 7999) for t in mouse_times), default=0) / 8.0
    return dict(keyboard=len(kb), holds=sum(n["type"] == "hold" for n in kb),
                chords=sum(1 for t in unique if counts[t] > 1), mouse=len(mouse),
                sliders=sum(n["type"] == "slider" for n in mouse), span=span, kb_rate=len(kb) / span,
                mouse_rate=len(mouse) / span, peak8=peak8, mouse_peak8=mouse_peak8, max1s=max(win),
                min_gap=min(b - a for a, b in zip(unique, unique[1:])), min_lane_gap=min(lane_gaps),
                min_mouse_gap=min(mgaps), mouse_speed=max(speeds), mouse_speed_aspect=max(speeds_aspect),
                per_section=per_section)


def load_score(m: dict) -> float:
    """Peak 8-second load: keyboard notes/s plus mouse targets/s weighted 1.5 (aiming + clicking)."""
    return m["peak8"] + 1.5 * m["mouse_peak8"]


def bisect_count(sorted_times: list[int], lo: int, hi: int) -> int:
    return bisect.bisect_right(sorted_times, hi) - bisect.bisect_left(sorted_times, lo)


def library_stats() -> list[tuple[str, str, float, dict]]:
    """Existing built-in charts, for level calibration."""
    rows = []
    for info_path in sorted((ROOT / "resources" / "charts").glob("*/info.json")):
        info = json.loads(info_path.read_text(encoding="utf-8"))
        if info.get("id") == CHART_ID or "Sakura Originals" not in info.get("source", ""):
            continue
        for d in info["difficulties"]:
            chart = json.loads((info_path.parent / d["chart_file"]).read_text(encoding="utf-8"))
            rows.append((info["id"], d["name"], float(d["level"]), chart_metrics(chart)))
    return rows


def print_report(results: list[tuple[Difficulty, dict, list[str]]]) -> None:
    print("\n=== 樱落呼吸 chart report ===")
    for diff, chart, errors in results:
        m = chart_metrics(chart)
        print(f"\n[{diff.name}] level {diff.level}  keyboard {m['keyboard']} (holds {m['holds']}, chords {m['chords']})"
              f"  mouse {m['mouse']} (sliders {m['sliders']})  span {m['span']:.1f} s")
        print(f"  overall: keyboard {m['kb_rate']:.2f}/s (peak 8 s {m['peak8']:.2f}/s, max {m['max1s']} in 1 s), "
              f"mouse {m['mouse_rate']:.2f}/s")
        print(f"  min gaps: keyboard {m['min_gap']} ms, same lane {m['min_lane_gap']} ms, mouse {m['min_mouse_gap']} ms;"
              f" mouse travel max {m['mouse_speed']:.2f}/s (aspect-corrected {m['mouse_speed_aspect']:.2f}/s,"
              f" limit {diff.speed}/s)")
        print("  per section (keyboard/s | mouse/s): " + ", ".join(
            f"{name} {k:.2f}|{mo:.2f}" for name, (k, mo) in m["per_section"].items()))
        print("  validator: " + ("OK" if not errors else f"{len(errors)} problem(s)"))
        for e in errors[:20]:
            print("    - " + e)
    print("\n=== level calibration against the built-in library (Sakura Originals) ===")
    print("  chart                   level  kb/s  peak8s  max1s  chord%  holds  mouse/s  m-peak8s  load  min gap")
    library = library_stats()
    ours = [(CHART_ID, d.name, d.level, chart_metrics(c)) for d, c, _ in results]
    for cid, name, level, m in library + ours:
        print(f"  {cid + ' ' + name:22s} {level:5.1f}  {m['kb_rate']:4.2f}  {m['peak8']:6.2f}  {m['max1s']:5d}"
              f"  {m['chords'] / max(1, m['keyboard']):6.1%}  {m['holds']:5d}  {m['mouse_rate']:7.2f}"
              f"  {m['mouse_peak8']:8.2f}  {load_score(m):4.1f}  {m['min_gap']:5d} ms")
    x = np.array([load_score(m) for *_, m in library])
    y = np.array([level for _, _, level, _ in library])
    slope, intercept = np.polyfit(x, y, 1)
    print(f"  library fit: level = {intercept:.2f} + {slope:.3f} * load")
    for _, name, level, m in ours:
        print(f"  {name:7s} load {load_score(m):4.1f} -> library-equivalent level {intercept + slope * load_score(m):4.1f}"
              f" (assigned {level})")


# ── Preview (optional, for review only) ──────────────────────────────────────

def render_preview(chart: dict, an: Analysis, diff: Difficulty, path: Path, j0: int = 76, seconds: float = 20.0) -> None:
    """Piano-roll of an excerpt: two rows of 10 s, each with the four lanes, mouse y (pitch),
    mouse x and the measured kick / snare / melody onsets underneath."""
    from PIL import Image, ImageDraw, ImageFont
    font_path = str(ROOT / "resources" / "fonts" / "NotoSansSC-Regular.ttf")
    small, title_font = ImageFont.truetype(font_path, 15), ImageFont.truetype(font_path, 22)
    width, rows_n = 2400, 2
    row_s = seconds / rows_n
    px_s = (width - 60) / row_s
    lane_h, mouse_h, x_h, strip_h = 30, 200, 90, 66
    row_h = 30 + 4 * lane_h + 14 + mouse_h + 10 + x_h + 10 + strip_h + 24
    img = Image.new("RGB", (width, 50 + rows_n * row_h), (14, 16, 26))
    d = ImageDraw.Draw(img)
    d.text((14, 10), f"{TITLE} · {diff.name} · {beat_time(j0):.1f}-{beat_time(j0) + seconds:.1f} s · "
                     "lanes A S D F top→bottom, mouse y up = higher pitch", fill=(240, 230, 240), font=title_font)
    lane_colours = [(120, 200, 255), (150, 230, 190), (255, 210, 120), (255, 140, 170)]
    for r in range(rows_n):
        t0 = beat_time(j0) + r * row_s
        t1 = t0 + row_s
        top = 50 + r * row_h + 26
        lanes_top = top
        mouse_top = lanes_top + 4 * lane_h + 14
        x_top = mouse_top + mouse_h + 10
        strip_top = x_top + x_h + 10
        bottom = strip_top + strip_h
        X = lambda t, t0=t0: 50 + (t - t0) * px_s
        j = math.ceil((t0 - GRID0) / BEAT * 4) / 4
        while beat_time(j) <= t1:
            x = X(beat_time(j))
            q = round(j * 4)
            if q % 4 == 0:
                bar = q % 16 == 0
                d.line([(x, lanes_top - 4), (x, bottom)], fill=(95, 95, 128) if bar else (44, 44, 60), width=2 if bar else 1)
                if bar:
                    d.text((x + 3, lanes_top - 22), f"j{q // 4}", fill=(150, 150, 185), font=small)
            j += 0.25
        for name, a, _, _ in SECTIONS:
            if t0 <= beat_time(a) <= t1:
                d.line([(X(beat_time(a)), lanes_top - 24), (X(beat_time(a)), bottom)], fill=(240, 120, 160), width=3)
                d.text((X(beat_time(a)) + 5, lanes_top - 24), name, fill=(240, 160, 190), font=small)
        for lane in range(4):
            d.rectangle([50, lanes_top + lane * lane_h, width - 10, lanes_top + (lane + 1) * lane_h - 3], outline=(38, 40, 58))
            d.text((8, lanes_top + lane * lane_h + 5), "ASDF"[lane], fill=lane_colours[lane], font=small)
        for n in chart["keyboard_notes"]:
            t = n["time"] / 1000
            if not t0 - 2 <= t <= t1:
                continue
            y = lanes_top + n["lane"] * lane_h
            if n["type"] == "hold" and t + n["duration"] / 1000 > t0:
                d.rectangle([max(50, X(t)), y + 9, max(50, min(width - 10, X(t + n["duration"] / 1000))),
                             y + lane_h - 12], fill=lane_colours[n["lane"]])
            if t >= t0:
                d.rounded_rectangle([X(t) - 6, y + 3, X(t) + 6, y + lane_h - 6], radius=3, fill=lane_colours[n["lane"]])
        d.rectangle([50, mouse_top, width - 10, mouse_top + mouse_h], outline=(60, 60, 90))
        d.rectangle([50, x_top, width - 10, x_top + x_h], outline=(60, 60, 90))
        d.text((8, mouse_top + 4), "y", fill=(200, 200, 230), font=small)
        d.text((8, x_top + 4), "x", fill=(200, 200, 230), font=small)
        for n in chart["mouse_notes"]:
            t = n["time"] / 1000
            if not t0 - 3 <= t <= t1:
                continue
            pts = [(n["x"], n["y"])] + [tuple(p) for p in n["slider_path"]]
            times = [t + k / max(1, len(pts) - 1) * n["slider_duration"] / 1000 for k in range(len(pts))]
            keep = [(a, p) for a, p in zip(times, pts) if t0 <= a <= t1]
            colour = (255, 110, 196) if n["type"] == "slider" else (255, 236, 244)
            if len(keep) > 1:
                d.line([(X(a), mouse_top + p[1] * mouse_h) for a, p in keep], fill=colour, width=4)
                d.line([(X(a), x_top + p[0] * x_h) for a, p in keep], fill=colour, width=4)
            if t0 <= t <= t1:
                d.ellipse([X(t) - 7, mouse_top + n["y"] * mouse_h - 7, X(t) + 7, mouse_top + n["y"] * mouse_h + 7], fill=colour)
                d.ellipse([X(t) - 5, x_top + n["x"] * x_h - 5, X(t) + 5, x_top + n["x"] * x_h + 5], fill=colour)
        cls, strength, _ = drum_table(an)
        strips = [("kick", np.where(cls == KICK, strength, 0.0), (120, 200, 255)),
                  ("snare", np.where(cls == SNARE, strength, 0.0), (255, 140, 170)),
                  ("other", np.where((cls != KICK) & (cls != SNARE), strength, 0.0), (190, 190, 120)),
                  ("melody", np.minimum(an.melody, 1.5) / 1.5, (255, 236, 244))]
        for k, (label, values, colour) in enumerate(strips):
            base = strip_top + (k + 1) * 16
            d.text((4, base - 15), label, fill=colour, font=small)
            for s in range(max(0, int((t0 - GRID0) / BEAT * 4)), min(an.steps, int((t1 - GRID0) / BEAT * 4) + 1)):
                v = float(values[s])
                if v > 0.05 and t0 <= step_time(s) <= t1:
                    d.line([(X(step_time(s)), base), (X(step_time(s)), base - min(1.0, v) * 13)], fill=colour, width=3)
    img.save(path)


# ── Main ─────────────────────────────────────────────────────────────────────

def write_json(path: Path, data: dict) -> None:
    path.write_text(json.dumps(data, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__.splitlines()[0])
    parser.add_argument("--source", type=Path, default=DEFAULT_SOURCE, help="author's Suno MP3 (not committed)")
    parser.add_argument("--skip-audio", action="store_true", help="keep an existing music.ogg")
    parser.add_argument("--preview-dir", type=Path, help="also render review piano-rolls into this folder")
    args = parser.parse_args()
    if not args.source.is_file():
        print(f"source audio not found: {args.source}", file=sys.stderr)
        return 2
    OUT_DIR.mkdir(parents=True, exist_ok=True)

    samples = decode(args.source)
    print(f"decoded {args.source.name}: {len(samples) / SR:.3f} s, {SR} Hz stereo")
    ogg = OUT_DIR / "music.ogg"
    if not (args.skip_audio and ogg.is_file()):
        encode_ogg(args.source, ogg)
        print(f"wrote {ogg.relative_to(ROOT)} ({ogg.stat().st_size / 1e6:.2f} MB)")

    an = analyze(samples)
    results = []
    for diff in DIFFICULTIES:
        chart = build_chart(an, diff)
        errors = validate_loader_rules(chart) + validate_playability(chart, diff)
        results.append((diff, chart, errors))

    info = dict(version=2, id=CHART_ID, title=TITLE, artist=ARTIST, charter="Sakura",
                source="Sakura Originals · Vol. 02", tags=["original", "electronic", "ai-generated", "suno"],
                music_file="music.ogg", cover_file="cover.png", background_file="",
                preview_time=ms(beat_time(64)), bpm=BPM, offset=0, difficulties=[])
    for diff, chart, _ in results:
        file_name = f"{diff.name.lower()}.json"
        write_json(OUT_DIR / file_name, chart)
        info["difficulties"].append(dict(
            name=diff.name, level=diff.level, chart_file=file_name, note_count=len(chart["keyboard_notes"]),
            hold_count=sum(n["type"] == "hold" for n in chart["keyboard_notes"]),
            mouse_note_count=len(chart["mouse_notes"])))
    write_json(OUT_DIR / "info.json", info)

    sys.dont_write_bytecode = True
    sys.path.insert(0, str(Path(__file__).resolve().parent))
    from build_starter_album import cover  # reuse the built-in album cover style
    cover(OUT_DIR, (236, 150, 178), "06", TITLE)

    print_report(results)
    if args.preview_dir:
        args.preview_dir.mkdir(parents=True, exist_ok=True)
        for diff, chart, _ in results:
            render_preview(chart, an, diff, args.preview_dir / f"{CHART_ID}_{diff.name.lower()}.png")
        print(f"\npreviews written to {args.preview_dir}")
    print("\nSHA-256:")
    for name in ["info.json"] + [f"{d.name.lower()}.json" for d in DIFFICULTIES] + ["music.ogg", "cover.png"]:
        print(f"  {hashlib.sha256((OUT_DIR / name).read_bytes()).hexdigest()[:16]}  {name}")
    return 1 if any(errors for _, _, errors in results) else 0


if __name__ == "__main__":
    sys.exit(main())
