"""Prepare generated assets for the Sakura promo (everything written here is gitignored).

    python scripts/prepare.py assets
    python scripts/prepare.py soundtrack [--music <audio file>]
    python scripts/prepare.py footage --record-out <dir written by Sakura.exe --record>

Requires Python 3.10+, numpy and ffmpeg on PATH.
"""
from __future__ import annotations

import argparse
import csv
import json
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

import numpy as np

PROMO = Path(__file__).resolve().parents[1]
REPO = PROMO.parent
PUBLIC = PROMO / "public"
EDIT = json.loads((PROMO / "edit.json").read_text(encoding="utf-8"))
MASTER_MP3 = REPO / "download/songs/樱花落下的呼吸 (2).mp3"
DEFAULT_MUSIC = MASTER_MP3 if MASTER_MP3.exists() else (REPO / "resources/charts/sakura_breath/music.ogg")


def run(cmd: list[str], **kw) -> subprocess.CompletedProcess:
    return subprocess.run(cmd, check=True, capture_output=True, **kw)


def copy(src: Path, dst: Path, link: bool = False) -> None:
    if not src.exists():
        raise FileNotFoundError(src)
    dst.parent.mkdir(parents=True, exist_ok=True)
    if dst.exists():
        dst.unlink()
    if link:
        try:
            os.link(src, dst)  # recordings are several GB; a hardlink avoids a second copy
            print(f"  {src} => {dst.relative_to(PROMO)} (hardlink)")
            return
        except OSError:
            pass
    shutil.copy2(src, dst)
    print(f"  {src} -> {dst.relative_to(PROMO)}")


def cmd_assets(_: argparse.Namespace) -> None:
    print("assets:")
    for name in ("NotoSansSC-Regular.ttf", "NotoSansSC-Bold.ttf", "OFL.txt"):
        copy(REPO / "resources/fonts" / name, PUBLIC / "fonts" / name)
    copy(REPO / "resources/images/app_icon.png", PUBLIC / "img/app_icon.png")
    copy(REPO / "resources/images/ui/sakura_crest.png", PUBLIC / "img/sakura_crest.png")
    copy(REPO / "resources/images/backgrounds/moonlit_sakura_bg.png", PUBLIC / "img/moonlit_sakura_bg.png")
    copy(REPO / "resources/charts/sakura_breath/cover.png", PUBLIC / "img/sakura_breath_cover.png")
    bga = REPO / "resources/charts/sakura_breath/bg.mp4"
    if bga.exists():
        copy(bga, PUBLIC / "footage/sakura_breath_bga.mp4", link=True)
    covers_manifest = []
    for info_path in sorted((REPO / "resources/charts").glob("*/info.json")):
        info = json.loads(info_path.read_text(encoding="utf-8"))
        cid = info["id"]
        if cid in ("test-song", "tutorial_song"):
            continue
        cover_src = info_path.parent / info.get("cover_file", "cover.png")
        if cover_src.exists():
            copy(cover_src, PUBLIC / "img/covers" / f"{cid}.png")
            max_lvl = max((float(d["level"]) for d in info.get("difficulties", [])), default=0.0)
            covers_manifest.append({
                "id": cid,
                "title": info["title"],
                "artist": info.get("artist", "Sakura Soundworks"),
                "bpm": float(info.get("bpm", 120.0)),
                "maxLevel": max_lvl,
                "version": int(info.get("version", 2)),
                "cover": f"img/covers/{cid}.png",
            })
    (PUBLIC / "img/covers/manifest.json").write_text(
        json.dumps(covers_manifest, indent=2, ensure_ascii=False), encoding="utf-8"
    )


def decode(path: Path, rate: int) -> np.ndarray:
    raw = run(["ffmpeg", "-v", "error", "-i", str(path), "-f", "f32le", "-ac", "2", "-ar", str(rate), "-"]).stdout
    return np.frombuffer(raw, dtype=np.float32).reshape(-1, 2).astype(np.float64)


def measure(path: Path) -> tuple[float, float]:
    """Integrated loudness (LUFS) and true peak (dBTP) via ffmpeg ebur128."""
    err = subprocess.run(["ffmpeg", "-hide_banner", "-nostats", "-i", str(path), "-af", "ebur128=peak=true",
                          "-f", "null", "-"], capture_output=True, text=True, encoding="utf-8", errors="replace").stderr
    summary = err[err.rfind("Summary:"):]
    lufs = float(re.search(r"I:\s+(-?[\d.]+) LUFS", summary).group(1))
    peak = float(re.search(r"Peak:\s+(-?[\d.]+) dBFS", summary).group(1))
    return lufs, peak


def write_wav(path: Path, audio: np.ndarray, rate: int) -> None:
    path.parent.mkdir(parents=True, exist_ok=True)
    proc = subprocess.run(["ffmpeg", "-v", "error", "-y", "-f", "f32le", "-ac", "2", "-ar", str(rate), "-i", "-",
                           "-c:a", "pcm_s24le", str(path)], input=audio.astype(np.float32).tobytes(), capture_output=True)
    if proc.returncode:
        raise RuntimeError(proc.stderr.decode(errors="replace"))


def cmd_soundtrack(args: argparse.Namespace) -> None:
    rate, fps = EDIT["sampleRate"], EDIT["fps"]
    spf = rate // fps  # 800 samples per video frame
    music = Path(args.music)
    print(f"soundtrack: decoding {music}")
    src = decode(music, rate)
    out = np.zeros((EDIT["durationInFrames"] * spf, 2))
    xf = int(round(EDIT["crossfadeMs"] / 1000 * rate))
    segments = EDIT["segments"]
    for i, seg in enumerate(segments):
        e0, s0, n = seg["editFrom"] * spf, seg["sourceFrom"] * spf, seg["duration"] * spf
        # Edit sample e maps to source sample e - e0 + s0 everywhere in the segment (including the
        # crossfade pre-roll), so 60 fps gameplay footage stays frame-locked to the music after a splice.
        pre = xf if i > 0 else 0
        post = xf if i + 1 < len(segments) else 0
        a, b = e0 - pre, e0 + n
        chunk = np.zeros((b - a, 2))
        sa = s0 - pre
        lo, hi = max(sa, 0), min(sa + (b - a), len(src))
        if hi > lo:
            chunk[lo - sa:hi - sa] = src[lo:hi]
        gain = np.ones(b - a)
        if pre:
            gain[:pre] = np.sin(np.linspace(0, np.pi / 2, pre, endpoint=False))
        if post:
            gain[-post:] *= np.cos(np.linspace(0, np.pi / 2, post, endpoint=False))
        out[a:b] += chunk * gain[:, None]
    fade = EDIT["fadeOutFrames"] * spf
    out[-fade:] *= (np.cos(np.linspace(0, np.pi / 2, fade)) ** 2)[:, None]
    ramp = int(0.01 * rate)
    out[:ramp] *= np.linspace(0, 1, ramp)[:, None]

    tmp = PUBLIC / "audio/_unnormalized.wav"
    write_wav(tmp, out, rate)
    lufs, peak = measure(tmp)
    gain_db = EDIT["targetLufs"] - lufs
    if peak + gain_db > EDIT["maxTruePeakDb"]:
        gain_db = EDIT["maxTruePeakDb"] - peak
    out *= 10 ** (gain_db / 20)
    dst = PUBLIC / "audio/soundtrack.wav"
    write_wav(dst, out, rate)
    tmp.unlink()
    lufs2, peak2 = measure(dst)
    print(f"  edit loudness {lufs:.1f} LUFS / {peak:.1f} dBTP -> gain {gain_db:+.2f} dB")
    print(f"  wrote {dst.relative_to(PROMO)}: {len(out) / rate:.3f} s, {lufs2:.1f} LUFS, true peak {peak2:.1f} dBTP")

    mono = out.mean(axis=1)
    n_frames = EDIT["durationInFrames"]
    win_len = 2048
    window = np.hanning(win_len)
    freqs = np.fft.rfftfreq(win_len, 1.0 / rate)
    edges = np.geomspace(45.0, 12000.0, 25)
    bands_out = np.zeros((n_frames, 24), dtype=np.float32)
    bass_out = np.zeros(n_frames, dtype=np.float32)
    rms_out = np.zeros(n_frames, dtype=np.float32)
    bass_mask = (freqs >= 45.0) & (freqs <= 165.0)
    for f_idx in range(n_frames):
        center = f_idx * spf + spf // 2
        lo_i = max(0, center - win_len // 2)
        hi_i = min(len(mono), lo_i + win_len)
        buf = np.zeros(win_len, dtype=np.float64)
        buf[: hi_i - lo_i] = mono[lo_i:hi_i]
        rms_out[f_idx] = float(np.sqrt(np.mean(buf * buf)))
        mag = np.abs(np.fft.rfft(buf * window))
        bass_out[f_idx] = float(np.mean(mag[bass_mask]))
        for b_idx in range(24):
            m = (freqs >= edges[b_idx]) & (freqs < edges[b_idx + 1])
            bands_out[f_idx, b_idx] = float(np.mean(mag[m])) if np.any(m) else 0.0
    bands_db = np.log1p(bands_out * 18.0)
    p99 = float(np.percentile(bands_db, 99.2)) or 1.0
    bands_norm = np.clip(bands_db / p99, 0.0, 1.0)
    for f_idx in range(1, n_frames):
        bands_norm[f_idx] = np.maximum(bands_norm[f_idx], bands_norm[f_idx - 1] * 0.80)
    bass_p99 = float(np.percentile(bass_out, 99.0)) or 1.0
    rms_p99 = float(np.percentile(rms_out, 99.0)) or 1.0
    spec_payload = {
        "fps": fps,
        "frames": n_frames,
        "bass": [round(float(min(1.0, v / bass_p99)), 3) for v in bass_out],
        "rms": [round(float(min(1.0, v / rms_p99)), 3) for v in rms_out],
        "bands": [[round(float(x), 3) for x in row] for row in bands_norm],
    }
    spec_path = PUBLIC / "audio/spectrum.json"
    spec_path.write_text(json.dumps(spec_payload, separators=(",", ":")), encoding="utf-8")
    print(f"  wrote {spec_path.relative_to(PROMO)} ({n_frames} frames x 24 bands)")


def cmd_footage(args: argparse.Namespace) -> None:
    rec = Path(args.record_out)
    manifest = json.loads((rec / "record-manifest.json").read_text(encoding="utf-8"))
    dst = PUBLIC / "footage"
    dst.mkdir(parents=True, exist_ok=True)
    fps = EDIT["fps"]
    index = {}
    for shot in manifest["shots"]:
        name = shot["name"]
        video = rec / Path(shot["file"]).name
        copy(video, dst / video.name, link=True)
        with (rec / f"{name}.frames.csv").open(encoding="utf-8", newline="") as f:
            rows = list(csv.DictReader(f))
        info: dict = {"file": f"footage/{video.name}", "frames": len(rows)}
        playing = [r for r in rows if r.get("music_playing", "").lower() in ("1", "true")]
        if playing:
            first = playing[0]
            # Video frame that shows music time 0 (negative if the recording started mid-song).
            info["musicZeroFrame"] = int(first["frame"]) - round(float(first["music_pos_s"]) * fps)
            # Only the first continuous playback run is the song itself (a result screen may start a preview).
            run_rows = [first]
            for r in playing[1:]:
                if int(r["frame"]) != int(run_rows[-1]["frame"]) + 1:
                    break
                run_rows.append(r)
            drift = max(abs(float(r["music_pos_s"]) * fps - (int(r["frame"]) - info["musicZeroFrame"])) for r in run_rows)
            info["maxDriftFrames"] = round(drift, 4)
            info["musicRunFrames"] = len(run_rows)
        result = [r for r in rows if r.get("scene") == "result"]
        if result:
            info["resultFrame"] = int(result[0]["frame"])
        index[name] = info
        print(f"  {name}: {info}")
    (dst / "index.json").write_text(json.dumps(index, indent=2, ensure_ascii=False), encoding="utf-8")


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = parser.add_subparsers(dest="command", required=True)
    sub.add_parser("assets")
    p = sub.add_parser("soundtrack")
    p.add_argument("--music", default=str(DEFAULT_MUSIC))
    p = sub.add_parser("footage")
    p.add_argument("--record-out", required=True)
    args = parser.parse_args()
    {"assets": cmd_assets, "soundtrack": cmd_soundtrack, "footage": cmd_footage}[args.command](args)
    return 0


if __name__ == "__main__":
    sys.exit(main())
