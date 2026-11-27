"""Audio-analysis Chart Format v4 & bespoke mirror-sakura cover generator for Sakura Official Pack · Vol. 05 (镜花里章).

STRICT ZERO SYNTHETIC PLACEHOLDER AUDIO:
Reads ONLY the 6 unused genuine 2nd-Take Suno MP3 master tracks in download/songs/, transcodes
reproducible music.ogg via ffmpeg (libvorbis, 44.1kHz stereo), separates harmonic/percussive stems
via STFT HPSS, authors multi-difficulty Chart Format v4 files including Master 15.0
(shattered_threshold_omega/master.json) + bilingual synchronized lyrics, renders bespoke
mirror-gold & crimson-sakura cover.png artwork, and updates resources/charts/album.json (21 songs / 61 charts).
"""
from __future__ import annotations

import argparse
from dataclasses import dataclass
import json
import math
from pathlib import Path
import sys

import numpy as np
from PIL import Image, ImageDraw, ImageFilter

sys.path.insert(0, str(Path(__file__).resolve().parent))
import build_pack_vol4 as vol4  # noqa: E402

ROOT = vol4.ROOT
SONGS_DIR = vol4.SONGS_DIR
CHARTS_DIR = vol4.CHARTS_DIR


@dataclass(frozen=True)
class Vol5TrackSpec:
    chart_id: str
    source_file: str
    title: str
    artist: str
    motif_style: str
    tags: tuple[str, ...]
    palette: tuple[tuple[int, int, int], tuple[int, int, int], tuple[int, int, int]]
    lyrics: tuple[tuple[str, str], ...]
    diffs: tuple[vol4.DiffSpec, ...]


PACK_VOL5_SPECS: tuple[Vol5TrackSpec, ...] = (
    Vol5TrackSpec(
        "petal_storm_vip",
        "Petal Storm_ Overdrive (1).mp3",
        "Petal Storm: Overdrive [VIP]",
        "Sakura Soundworks",
        "gale_stream",
        ("original", "vip", "speedcore", "v4-stage", "vol5"),
        ((28, 12, 44), (255, 138, 186), (255, 224, 148)),
        (
            ("狂岚卷起千重绯樱，弦音撕裂长夜寂静", "Crimson gale sweeps the blossoms through the silent night"),
            ("镜面映出疾驰残影，双翼共鸣直抵极星", "Mirrored afterimages race in resonance toward the zenith star"),
            ("在风暴之眼绽放吧，超越极限的终极回响", "Bloom inside the eye of the storm, beyond the final overdrive"),
            ("万瓣流光化作雷鸣，镌刻今夜不灭之名", "Ten thousand luminous petals thunder into eternity"),
        ),
        (
            vol4.DiffSpec("Normal", "normal.json", 6.5, 2, 2.45, 0.75, 0.17, False, False),
            vol4.DiffSpec("Hard", "hard.json", 10.5, 2, 4.15, 1.25, 0.23, True, True),
            vol4.DiffSpec("Expert", "expert.json", 14.5, 1, 6.30, 1.82, 0.28, True, True),
        ),
    ),
    Vol5TrackSpec(
        "instant_bloom_vip",
        "一瞬花绽 (1).mp3",
        "一瞬花绽 · 里绽",
        "Sakura Soundworks",
        "ribbons",
        ("original", "vip", "artcore", "v4-stage", "vol5"),
        ((20, 14, 48), (252, 168, 214), (162, 236, 255)),
        (
            ("刹那芳华凝于指尖，镜湖微澜倒映月弦", "A fleeting bloom rests on fingertips over the mirrored lake"),
            ("里章琴音悄然苏醒，唤醒沉睡百年的花影", "The inner movement awakens hundred-year slumbering petals"),
            ("一瞬满开即为永恒，与月光同频呼吸", "One instant in full bloom becomes eternity in moonlight"),
            ("落樱归于琉璃幻梦，余音仍在星河回荡", "Falling blossoms return to glass dreams across the stars"),
        ),
        (
            vol4.DiffSpec("Normal", "normal.json", 5.0, 2, 2.05, 0.62, 0.16, False, False),
            vol4.DiffSpec("Hard", "hard.json", 8.5, 2, 3.55, 1.06, 0.21, True, True),
            vol4.DiffSpec("Expert", "expert.json", 12.0, 1, 5.45, 1.60, 0.25, True, True),
        ),
    ),
    Vol5TrackSpec(
        "distant_star_vip",
        "遥远的星 (1).mp3",
        "遥远的星 · 回响",
        "Sakura Soundworks",
        "crescent",
        ("original", "vip", "melodic", "starlight", "v4-stage", "vol5"),
        ((12, 18, 52), (156, 212, 255), (255, 218, 156)),
        (
            ("跨越亿万光年的微光，在夜樱枝头轻轻停驻", "Starlight from light-years away rests upon night sakura boughs"),
            ("每一次遥远的回响，都是宿命重逢的序章", "Every distant echo writes the prelude of our reunion"),
            ("循着银月铺展的轨迹，伸手触碰星海彼岸", "Follow the silver-moon trail to touch the far shore of stars"),
            ("当黎明染亮天际线，星辉仍守护着誓言", "As dawn gilds the horizon, starlight still guards our vow"),
        ),
        (
            vol4.DiffSpec("Easy", "easy.json", 3.5, 4, 1.50, 0.46, 0.14, False, False),
            vol4.DiffSpec("Normal", "normal.json", 6.0, 2, 2.30, 0.70, 0.17, False, False),
            vol4.DiffSpec("Hard", "hard.json", 9.5, 2, 3.85, 1.16, 0.22, True, True),
        ),
    ),
    Vol5TrackSpec(
        "sakura_breath_vip",
        "樱花落下的呼吸 (1).mp3",
        "樱花落下的呼吸 · 终章",
        "Sakura Soundworks",
        "pulse_rings",
        ("original", "vip", "finale", "sakura", "v4-stage", "vol5"),
        ((22, 14, 46), (250, 174, 208), (252, 222, 162)),
        (
            ("聆听最后一片花瓣，在月色中轻柔吐息", "Listen as the final petal breathes softly in the moonlight"),
            ("四季流转汇成终章，墨色夜空燃起流光", "Turning seasons converge into the finale beneath ink skies"),
            ("左手的脉搏与右手的轨迹，在此刻完全重叠", "Pulse of the left hand and arc of the right become one"),
            ("愿这首樱之终曲，伴你走过漫长星夜", "May this sakura coda accompany you through the starry night"),
        ),
        (
            vol4.DiffSpec("Normal", "normal.json", 5.5, 2, 2.15, 0.66, 0.16, False, False),
            vol4.DiffSpec("Hard", "hard.json", 9.0, 2, 3.70, 1.12, 0.22, True, True),
            vol4.DiffSpec("Expert", "expert.json", 12.5, 1, 5.70, 1.66, 0.26, True, True),
        ),
    ),
    Vol5TrackSpec(
        "sakura_sprint_vip",
        "樱语疾行 (1).mp3",
        "樱语疾行 · 极速",
        "Sakura Soundworks",
        "prism_drive",
        ("original", "vip", "drum-and-bass", "high-speed", "v4-stage", "vol5"),
        ((24, 12, 44), (255, 148, 198), (144, 236, 255)),
        (
            ("疾风掠过绯色长街，心跳加速突破音障", "Gale sweeps the crimson avenue as heartbeats break sound"),
            ("碎拍鼓点如骤雨倾落，化作指尖金色闪电", "Breakbeat rain cascades into golden lightning at your fingers"),
            ("不留退路全速推进，冲破镜花构筑的迷宫", "Full-throttle ahead, shattering the labyrinth of mirror blooms"),
            ("极速尽头唯有澄明，见证属于胜者的拂晓", "Beyond top speed lies clarity and the dawn of victors"),
        ),
        (
            vol4.DiffSpec("Normal", "normal.json", 6.5, 2, 2.45, 0.74, 0.18, False, False),
            vol4.DiffSpec("Hard", "hard.json", 10.0, 2, 4.05, 1.22, 0.23, True, True),
            vol4.DiffSpec("Expert", "expert.json", 13.5, 1, 6.10, 1.76, 0.27, True, True),
        ),
    ),
    Vol5TrackSpec(
        "shattered_threshold_omega",
        "破碎阈值 (1).mp3",
        "破碎阈值 · Ω",
        "Sakura Soundworks",
        "shatter_core",
        ("original", "omega", "boss", "hardcore", "master-15", "v4-stage", "vol5"),
        ((34, 8, 42), (255, 102, 158), (158, 240, 255)),
        (
            ("当最后一道界限崩裂，深渊绽放极星神乐", "When the final threshold shatters, zenith kagura blooms"),
            ("镜花万象尽归于零，重构超越凡俗的律动", "Mirrored illusions collapse to zero, forging transcendent rhythm"),
            ("以双域共鸣之剑，斩开通往十五极境的王座", "Wield the twin-zone blade to cleave the throne of Level 15"),
            ("破碎之后方见真章，此刻加冕樱华绝巅", "Truth emerges past shatter—be crowned atop Sakura's apex"),
        ),
        (
            vol4.DiffSpec("Hard", "hard.json", 11.0, 2, 4.25, 1.28, 0.24, True, True),
            vol4.DiffSpec("Expert", "expert.json", 13.8, 1, 6.20, 1.80, 0.27, True, True),
            vol4.DiffSpec("Master", "master.json", 15.0, 1, 6.85, 1.96, 0.30, True, True),
        ),
    ),
)


def build_lyrics(an: dict, spec: Vol5TrackSpec) -> list[dict]:
    dur_ms = int(round(float(an["duration"]) * 1000.0))
    span_start = max(2200, int(dur_ms * 0.14))
    span_end = max(span_start + 8000, int(dur_ms * 0.84))
    n = len(spec.lyrics)
    step = (span_end - span_start) // max(1, n)
    out: list[dict] = []
    for idx, (cn, en) in enumerate(spec.lyrics):
        t0 = span_start + idx * step
        t1 = min(dur_ms - 400, t0 + min(4800, int(step * 0.82)))
        out.append({"time": t0, "end_time": max(t0 + 1200, t1), "text": cn, "sub_text": en})
    return out


def render_vol5_cover(dest: Path, spec: Vol5TrackSpec, seed_val: int) -> None:
    size = 512
    bg, primary, accent = spec.palette
    y_grid, x_grid = np.mgrid[0:size, 0:size].astype(np.float32) / size
    radial = np.sqrt((x_grid - 0.5) ** 2 + (y_grid - 0.42) ** 2)
    glow = np.clip(1.0 - radial * 1.38, 0.0, 1.0) ** 1.7
    mirror_seam = np.clip(1.0 - np.abs(x_grid + y_grid - 1.0) * 2.2, 0.0, 1.0) ** 2.4

    arr = np.zeros((size, size, 3), dtype=np.float32)
    for c in range(3):
        arr[:, :, c] = bg[c] * (1.06 - 0.36 * y_grid) + primary[c] * glow * 0.54 + accent[c] * (glow ** 3) * 0.36 + accent[c] * mirror_seam * 0.22
    img = Image.fromarray(np.clip(arr, 0, 255).astype(np.uint8), mode="RGB").convert("RGBA")
    overlay = Image.new("RGBA", (size, size), (0, 0, 0, 0))
    draw = ImageDraw.Draw(overlay)
    cx, cy = size // 2, int(size * 0.44)
    rng = np.random.default_rng(seed_val)

    oct_pts = [(cx + math.cos(k * math.pi / 4 + 0.39) * 176, cy + math.sin(k * math.pi / 4 + 0.39) * 176) for k in range(8)]
    draw.polygon(oct_pts, outline=(*accent, 165))
    draw.ellipse((cx - 134, cy - 134, cx + 134, cy + 134), fill=(*primary, 32), outline=(*accent, 212), width=2)

    shard_n = 16 if spec.motif_style == "shatter_core" else 10
    for s_i in range(shard_n):
        ang = s_i * (math.tau / shard_n) + 0.17
        r_in = 34 + (s_i % 3) * 16
        r_out = 128 + (s_i % 4) * 22
        p0 = (cx + math.cos(ang) * r_in, cy + math.sin(ang) * r_in)
        p1 = (cx + math.cos(ang + 0.14) * r_out, cy + math.sin(ang + 0.14) * r_out)
        p2 = (cx + math.cos(ang - 0.12) * (r_out * 0.85), cy + math.sin(ang - 0.12) * (r_out * 0.85))
        col = accent if s_i % 2 == 0 else primary
        draw.polygon([p0, p1, p2], fill=(*col, 96), outline=(*accent, 195))

    for k in range(5):
        ang = k * (2.0 * math.pi / 5.0) - math.pi / 2.0 + (seed_val % 7) * 0.06
        px, py = cx + math.cos(ang) * 60, cy + math.sin(ang) * 60
        pts = [
            (cx + math.cos(ang) * 14, cy + math.sin(ang) * 14),
            (px + math.cos(ang - 0.55) * 28, py + math.sin(ang - 0.55) * 28),
            (cx + math.cos(ang - 0.11) * 98, cy + math.sin(ang - 0.11) * 98),
            (cx + math.cos(ang) * 82, cy + math.sin(ang) * 82),
            (cx + math.cos(ang + 0.11) * 98, cy + math.sin(ang + 0.11) * 98),
            (px + math.cos(ang + 0.55) * 28, py + math.sin(ang + 0.55) * 28),
        ]
        draw.polygon(pts, fill=(*primary, 200), outline=(*accent, 236))
    draw.ellipse((cx - 14, cy - 14, cx + 14, cy + 14), fill=(*accent, 242))

    for _ in range(38):
        bx, by = int(rng.integers(24, size - 24)), int(rng.integers(24, size - 24))
        br = int(rng.integers(3, 12))
        col = primary if rng.random() < 0.6 else accent
        draw.ellipse((bx - br, by - br, bx + br, by + br), fill=(*col, int(rng.integers(42, 128))))

    img = Image.alpha_composite(img, overlay.filter(ImageFilter.GaussianBlur(radius=8.0)))
    img = Image.alpha_composite(img, overlay.filter(ImageFilter.GaussianBlur(radius=3.0)))
    img = Image.alpha_composite(img, overlay)
    frame = ImageDraw.Draw(img)
    frame.rounded_rectangle((16, 16, size - 16, size - 16), radius=22, outline=(*accent, 190), width=2)
    frame.rounded_rectangle((24, 24, size - 24, size - 24), radius=18, outline=(*primary, 105), width=1)
    img.convert("RGB").save(dest, format="PNG", optimize=True)


def build_track_vol5(spec: Vol5TrackSpec, skip_audio: bool = False) -> dict:
    out_dir = CHARTS_DIR / spec.chart_id
    out_dir.mkdir(parents=True, exist_ok=True)
    ogg_path = out_dir / "music.ogg"
    if not (skip_audio and ogg_path.is_file()):
        src = SONGS_DIR / spec.source_file
        if not src.is_file():
            raise FileNotFoundError(f"Missing real Suno MP3 source audio: {src}")
        vol4.encode_ogg(src, ogg_path, spec.title, spec.artist)

    y = vol4.decode_mono(ogg_path)
    if len(y) / vol4.AN_SR < 15.0:
        raise ValueError(f"{spec.chart_id}: refusing stub/synthetic audio shorter than 15s")
    an = vol4.analyze_audio(y)
    seed_val = sum((i + 1) * ord(ch) for i, ch in enumerate(spec.chart_id)) + 505
    cover_path = out_dir / "cover.png"
    if not (skip_audio and cover_path.is_file()):
        render_vol5_cover(cover_path, spec, seed_val)

    base_track = vol4.TrackSpec(spec.chart_id, spec.source_file, spec.title, spec.artist, spec.motif_style, spec.tags, spec.palette, spec.diffs)
    diff_entries = []
    for dspec in spec.diffs:
        gen_spec = (
            vol4.DiffSpec("Expert", dspec.chart_file, dspec.level, dspec.grid, dspec.kb_density, dspec.ms_density, dspec.thumb_ratio, dspec.synergy, dspec.sv)
            if dspec.name == "Master"
            else dspec
        )
        chart_doc = vol4.build_chart_v4(an, gen_spec, base_track)
        if dspec.name == "Master":
            for sv in chart_doc.get("sv_points", []):
                if float(sv.get("speed", 1.0)) > 1.0:
                    sv["speed"] = 1.24
        chart_doc["lyrics"] = build_lyrics(an, spec)
        valid_links = {n.get("link_id", 0) for n in chart_doc["keyboard_notes"]} - {0}
        for mn in chart_doc["mouse_notes"]:
            if mn.get("link_id", 0) not in valid_links:
                mn.pop("link_id", None)
        vol4.validate_chart_v4(chart_doc, gen_spec, spec.chart_id)
        (out_dir / dspec.chart_file).write_text(json.dumps(chart_doc, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        kb, ms = chart_doc["keyboard_notes"], chart_doc["mouse_notes"]
        diff_entries.append({
            "name": dspec.name,
            "level": dspec.level,
            "chart_file": dspec.chart_file,
            "note_count": len(kb),
            "hold_count": sum(1 for n in kb if n["type"] == "hold"),
            "mouse_note_count": len(ms),
            "thumb_count": sum(1 for n in kb if n["lane"] == 4),
            "synergy_count": sum(1 for n in ms if n.get("link_id", 0) > 0 or n.get("require_lane", -1) >= 0),
        })

    info_doc = {
        "version": 4,
        "id": spec.chart_id,
        "title": spec.title,
        "artist": spec.artist,
        "charter": "Sakura Chart Studio",
        "source": "Sakura Official Pack · Vol. 05 · 镜花里章",
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
    print(f"[pack-vol5] Built {spec.chart_id} ({spec.title}): {an['duration']:.2f}s @ {an['bpm']} BPM, {len(diff_entries)} diffs")
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
    built = [build_track_vol5(spec, skip_audio=args.skip_audio) for spec in PACK_VOL5_SPECS]
    album_path = CHARTS_DIR / "album.json"
    existing = json.loads(album_path.read_text(encoding="utf-8")) if album_path.is_file() else []
    pack_ids = {s.chart_id for s in PACK_VOL5_SPECS}
    kept = [entry for entry in existing if entry.get("id") not in pack_ids] + built
    album_path.write_text(json.dumps(kept, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    print(f"[pack-vol5] Updated {album_path} with {len(kept)} total authentic Suno tracks.")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
