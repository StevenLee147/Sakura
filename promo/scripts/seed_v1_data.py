"""Generate v1.0.0 userdata_template (sakura.db) and realistic Chart v3 replay (.skr) for promo recording."""
from __future__ import annotations

import json
import math
import random
import sqlite3
import struct
import time
from pathlib import Path

PROMO = Path(__file__).resolve().parents[1]
REPO = PROMO.parent
CHARTS_DIR = REPO / "resources" / "charts"
TEMPLATE_DIR = PROMO / "record" / "userdata_template"
REPLAYS_DIR = PROMO / "record" / "replays"

NOTE_TYPE_MAP = {"tap": 0, "hold": 1, "drag": 1, "circle": 2, "slider": 3}
# InputKind: Tick=0, LaneDown=1, LaneUp=2, Pointer=3, MouseDown=4, MouseUp=5, Resume=6, PointerScale=7, RightMouseDown=8, RightMouseUp=9


def f32_bits(val: float) -> int:
    return struct.unpack("<I", struct.pack("<f", float(val)))[0]


def f32_round(val: float) -> float:
    return struct.unpack("<f", struct.pack("<f", float(val)))[0]


def compute_replay_hash(chart_path: Path, offset_ms: int = 0) -> tuple[str, dict]:
    raw = json.loads(chart_path.read_text(encoding="utf-8"))
    version = int(raw.get("version", 1))

    tps = []
    for tp in raw.get("timing_points", []):
        ts = tp.get("time_signature")
        if isinstance(ts, list) and len(ts) >= 2:
            num, den = int(ts[0]), int(ts[1])
        else:
            num = int(tp.get("numerator", 4))
            den = int(tp.get("denominator", 4))
        tps.append((int(tp.get("time", 0)), float(tp.get("bpm", 120.0)), num, den))
    if not tps:
        tps.append((0, 120.0, 4, 4))
    tps.sort(key=lambda x: x[0])

    svs = []
    for sv in raw.get("sv_points", []):
        svs.append((int(sv.get("time", 0)), float(sv.get("speed", 1.0)), str(sv.get("easing", "linear"))))
    svs.sort(key=lambda x: x[0])

    kbs = []
    for n in raw.get("keyboard_notes", []):
        ntype = NOTE_TYPE_MAP.get(str(n.get("type", "tap")).lower(), 0)
        kbs.append({
            "time": int(n.get("time", 0)),
            "lane": int(n.get("lane", 0)),
            "type": ntype,
            "duration": int(n.get("duration", 0)),
            "link_id": int(n.get("link_id", 0)),
        })
    kbs.sort(key=lambda x: x["time"])

    mns = []
    for n in raw.get("mouse_notes", []):
        ntype = NOTE_TYPE_MAP.get(str(n.get("type", "circle")).lower(), 2)
        path_pts = []
        for pt in n.get("slider_path", []):
            if isinstance(pt, list) and len(pt) >= 2:
                path_pts.append((f32_round(pt[0]), f32_round(pt[1])))
        mns.append({
            "time": int(n.get("time", 0)),
            "x": f32_round(n.get("x", 0.5)),
            "y": f32_round(n.get("y", 0.5)),
            "type": ntype,
            "slider_duration": int(n.get("slider_duration", 0)),
            "slider_path": path_pts,
            "link_id": int(n.get("link_id", 0)),
            "require_lane": int(n.get("require_lane", -1)),
        })
    mns.sort(key=lambda x: x["time"])

    h = 14695981039346656037

    def mix(val: int) -> None:
        nonlocal h
        val &= 0xFFFFFFFF
        for i in range(4):
            h ^= (val >> (i * 8)) & 0xFF
            h = (h * 1099511628211) & 0xFFFFFFFFFFFFFFFF

    mix(offset_ms)
    mix(len(tps))
    for t, bpm, num, den in tps:
        mix(t)
        mix(f32_bits(bpm))
        mix(num)
        mix(den)
    mix(len(svs))
    for t, spd, easing in svs:
        mix(t)
        mix(f32_bits(spd))
        for b in easing.encode("utf-8"):
            mix(b)
        mix(0)
    mix(len(kbs))
    for n in kbs:
        mix(n["time"])
        mix(n["lane"])
        mix(n["type"])
        mix(n["duration"])
        if version >= 3 or n["link_id"] != 0:
            mix(n["link_id"])
    mix(len(mns))
    for n in mns:
        mix(n["time"])
        mix(f32_bits(n["x"]))
        mix(f32_bits(n["y"]))
        mix(n["type"])
        mix(n["slider_duration"])
        if version >= 3 or n["link_id"] != 0 or n["require_lane"] >= 0:
            mix(n["link_id"])
            mix(n["require_lane"] + 1)
        mix(len(n["slider_path"]))
        for px, py in n["slider_path"]:
            mix(f32_bits(px))
            mix(f32_bits(py))

    parsed = {"version": version, "keyboard_notes": kbs, "mouse_notes": mns}
    return str(h), parsed


def build_database() -> None:
    db_path = TEMPLATE_DIR / "data" / "sakura.db"
    db_path.parent.mkdir(parents=True, exist_ok=True)
    if db_path.exists():
        db_path.unlink()

    rng = random.Random(20261124)
    conn = sqlite3.connect(str(db_path))
    cur = conn.cursor()
    cur.executescript("""
    PRAGMA user_version = 3;
    CREATE TABLE scores (
        id               INTEGER PRIMARY KEY AUTOINCREMENT,
        chart_id         TEXT    NOT NULL,
        chart_title      TEXT    NOT NULL DEFAULT '',
        difficulty       TEXT    NOT NULL DEFAULT '',
        difficulty_level REAL    NOT NULL DEFAULT 0.0,
        score            INTEGER NOT NULL DEFAULT 0,
        accuracy         REAL    NOT NULL DEFAULT 0.0,
        max_combo        INTEGER NOT NULL DEFAULT 0,
        grade            TEXT    NOT NULL DEFAULT 'D',
        perfect_count    INTEGER NOT NULL DEFAULT 0,
        great_count      INTEGER NOT NULL DEFAULT 0,
        good_count       INTEGER NOT NULL DEFAULT 0,
        bad_count        INTEGER NOT NULL DEFAULT 0,
        miss_count       INTEGER NOT NULL DEFAULT 0,
        is_full_combo    INTEGER NOT NULL DEFAULT 0,
        is_all_perfect   INTEGER NOT NULL DEFAULT 0,
        played_at        INTEGER NOT NULL DEFAULT 0,
        hit_errors_json  TEXT    NOT NULL DEFAULT '[]',
        chart_hash       TEXT    NOT NULL DEFAULT ''
    );
    CREATE TABLE statistics (
        key   TEXT PRIMARY KEY NOT NULL,
        value REAL NOT NULL DEFAULT 0.0
    );
    CREATE TABLE achievements (
        id          TEXT PRIMARY KEY NOT NULL,
        unlocked_at INTEGER NOT NULL DEFAULT 0
    );
    CREATE INDEX idx_scores_revision ON scores(chart_id, difficulty, chart_hash, score DESC);
    """)

    now_ts = int(time.time()) - 3600
    info_files = sorted(CHARTS_DIR.glob("*/info.json"))
    all_entries = []
    for idx, info_file in enumerate(info_files):
        info = json.loads(info_file.read_text(encoding="utf-8"))
        cid = info["id"]
        if cid in ("test-song", "tutorial_song"):
            continue
        offset = int(info.get("offset", 0))
        for d_idx, diff in enumerate(info["difficulties"]):
            cpath = info_file.parent / diff["chart_file"]
            chash, parsed = compute_replay_hash(cpath, offset)
            judgments = sum(2 if n["type"] == 1 else 1 for n in parsed["keyboard_notes"]) + sum(
                1 + (len(n["slider_path"]) if n["type"] == 3 else 0) for n in parsed["mouse_notes"]
            )
            all_entries.append((info, diff, chash, max(1, judgments)))

    # Insert best scores across the official song library + recent 20 upward accuracy trend
    base_ts = now_ts - 86400 * 5
    for i, (info, diff, chash, total_j) in enumerate(all_entries):
        lvl = float(diff["level"])
        is_ap = (info["id"] in ("sakura_breath", "sakura_awakening", "spring_breeze") and diff["name"] != "Expert") or (
            info["id"] == "sakura_breath" and diff["name"] == "Expert"
        )
        if is_ap:
            great, good, miss = 0, 0, 0
            is_fc = 1
            ap_flag = 1
            grade = "SS"
            acc = 100.0
            score = 1000000
        elif lvl <= 9.5 or i % 3 != 0:
            great = max(1, int(total_j * rng.uniform(0.008, 0.028)))
            good = 0 if i % 4 != 0 else 1
            miss = 0
            is_fc = 1 if good == 0 else 1
            ap_flag = 0
            perfect = total_j - great - good
            acc = round((perfect * 100.0 + great * 70.0 + good * 40.0) / total_j, 2)
            grade = "SS" if (acc >= 99.0 and good == 0) else "S"
            score = int(round(acc * 10000)) - rng.randint(120, 980)
        else:
            great = max(3, int(total_j * rng.uniform(0.035, 0.065)))
            good = rng.randint(1, 3)
            miss = rng.randint(0, 2)
            is_fc = 1 if miss == 0 else 0
            ap_flag = 0
            perfect = max(1, total_j - great - good - miss)
            acc = round((perfect * 100.0 + great * 70.0 + good * 40.0) / total_j, 2)
            grade = "S" if acc >= 95.0 else "A"
            score = int(round(acc * 10000)) - rng.randint(800, 3200)

        perfect = total_j - great - good - miss
        max_combo = total_j if miss == 0 else int(total_j * rng.uniform(0.58, 0.86))
        errs = [int(round(rng.gauss(-0.8, 5.5))) for _ in range(min(120, total_j))]
        cur.execute(
            """INSERT INTO scores (
                chart_id, chart_title, difficulty, difficulty_level,
                score, accuracy, max_combo, grade,
                perfect_count, great_count, good_count, bad_count, miss_count,
                is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
            ) VALUES (?,?,?,?, ?,?,?,?, ?,?,?,?,?, ?,?,?,?,?)""",
            (
                info["id"], info["title"], diff["name"], lvl,
                score, acc, max_combo, grade,
                perfect, great, good, 0, miss,
                is_fc, ap_flag, base_ts + i * 1800, json.dumps(errs), chash,
            ),
        )

    # Add 20 recent plays with smooth upward mastery curve (93.2% -> 99.92%) so Stats Ribbon looks gorgeous
    curve_accs = [
        93.4, 94.1, 93.8, 95.2, 95.9, 95.4, 96.6, 97.1, 96.8, 97.7,
        98.2, 97.9, 98.6, 98.9, 99.1, 99.3, 99.0, 99.5, 99.7, 99.92,
    ]
    for idx, target_acc in enumerate(curve_accs):
        info, diff, chash, total_j = all_entries[(idx * 2) % len(all_entries)]
        great = max(0, int(round((100.0 - target_acc) / 30.0 * total_j)))
        perfect = max(1, total_j - great)
        real_acc = round((perfect * 100.0 + great * 70.0) / total_j, 2)
        grade = "SS" if real_acc >= 99.0 else ("S" if real_acc >= 95.0 else "A")
        score = 1000000 if great == 0 else int(round(real_acc * 10000)) - rng.randint(80, 450)
        cur.execute(
            """INSERT INTO scores (
                chart_id, chart_title, difficulty, difficulty_level,
                score, accuracy, max_combo, grade,
                perfect_count, great_count, good_count, bad_count, miss_count,
                is_full_combo, is_all_perfect, played_at, hit_errors_json, chart_hash
            ) VALUES (?,?,?,?, ?,?,?,?, ?,?,?,?,?, ?,?,?,?,?)""",
            (
                info["id"], info["title"], diff["name"], float(diff["level"]),
                score, real_acc, total_j, grade,
                perfect, great, 0, 0, 0,
                1, 1 if great == 0 else 0, now_ts - (20 - idx) * 120, "[0,-2,3,-1,2]", chash,
            ),
        )

    stats = {
        "total_play_count": 128.0,
        "total_play_time_seconds": 16420.0,
        "total_synergy_hits": 1840.0,
        "total_thumb_hits": 462.0,
        "total_fever_triggers": 34.0,
    }
    for k, v in stats.items():
        cur.execute("INSERT INTO statistics (key, value) VALUES (?, ?)", (k, v))

    unlocked = [
        "first_play", "first_fc", "first_ap", "combo_100", "combo_500", "combo_1000",
        "play_10", "play_50", "play_100", "all_charts", "accuracy_99", "score_999k",
        "first_synergy", "fever_mankai", "thumb_master",
    ]
    for idx, aid in enumerate(unlocked):
        cur.execute("INSERT INTO achievements (id, unlocked_at) VALUES (?, ?)", (aid, base_ts + idx * 7200))

    conn.commit()
    conn.close()
    print(f"  seeded {db_path.relative_to(PROMO)} ({len(all_entries) + 20} scores, {len(unlocked)} achievements)")


def build_telemetry_replay() -> None:
    """Generate a realistic human-like Chart v3 Expert replay on sidechain_dream for Deep Telemetry showcase."""
    REPLAYS_DIR.mkdir(parents=True, exist_ok=True)
    chart_dir = CHARTS_DIR / "sidechain_dream"
    info = json.loads((chart_dir / "info.json").read_text(encoding="utf-8"))
    offset = int(info.get("offset", 0))
    chash, parsed = compute_replay_hash(chart_dir / "expert.json", offset)

    rng = random.Random(178178)
    kbs = parsed["keyboard_notes"]
    mns = parsed["mouse_notes"]

    raw_events: list[tuple[int, int, int, float, float]] = [(-10000, 7, 0, 1.3468, 1.0)]

    # Pre-plan human timing jitter: ~97% Perfect (±1..16ms), ~3% Great (±27..34ms) with slight section variation
    def jitter_ms(time_ms: int, idx: int) -> int:
        # Make Quarter 3 (30s-45s) slightly wider so 4-section diagnosis highlights a realistic weakest section!
        in_q3 = 30000 <= time_ms <= 45000
        if in_q3 and idx % 9 == 3:
            return rng.choice([-31, 29, -28, 32])
        if not in_q3 and idx % 29 == 7:
            return rng.choice([-28, 27])
        sigma = 7.2 if in_q3 else 4.6
        val = int(round(rng.gauss(-0.6, sigma)))
        return max(-18, min(18, val))

    # Track keyboard busy intervals per lane [down_ms, up_ms]
    lane_intervals: dict[int, list[tuple[int, int]]] = {i: [] for i in range(5)}
    for idx, n in enumerate(kbs):
        err = 0 if n["link_id"] > 0 else jitter_ms(n["time"], idx)
        down_t = n["time"] - err
        dur = n["duration"] if n["type"] == 1 else 0
        up_t = (n["time"] + dur + 18) if dur > 0 else (down_t + 28)
        lane_intervals[n["lane"]].append((down_t, up_t))

    # Merge/clamp overlapping intervals on same lane
    for lane, ivs in lane_intervals.items():
        ivs.sort()
        cleaned: list[tuple[int, int]] = []
        for d, u in ivs:
            if cleaned and d <= cleaned[-1][1] + 2:
                prev_d, prev_u = cleaned[-1]
                # Shorten previous release just before new press
                cleaned[-1] = (prev_d, max(prev_d + 4, d - 3))
            cleaned.append((d, max(d + 8, u)))
        for d, u in cleaned:
            raw_events.append((d, 1, lane, 0.5, 0.5))
            raw_events.append((u, 2, lane, 0.5, 0.5))

    # Mouse notes (Circle & Slider + require_lane modifier)
    kb_times_by_lane = {lane: [n["time"] for n in kbs if n["lane"] == lane] for lane in range(5)}
    for idx, n in enumerate(mns):
        err = 0 if n["link_id"] > 0 else jitter_ms(n["time"], idx + 500)
        hit_t = n["time"] - err
        req = n["require_lane"]
        use_rmb = (req == 5)
        if 0 <= req <= 4:
            # Check if lane is already held or pressed within 90ms; if not and no nearby kb note, tap modifier safely
            already = any(d <= hit_t <= u + 90 for d, u in lane_intervals[req])
            near_kb = any(abs(kt - hit_t) <= 170 for kt in kb_times_by_lane[req])
            if not already and not near_kb:
                raw_events.append((hit_t - 12, 1, req, 0.5, 0.5))
                raw_events.append((hit_t + 18, 2, req, 0.5, 0.5))

        raw_events.append((hit_t - 4, 3, 0, n["x"], n["y"]))
        if use_rmb:
            raw_events.append((hit_t, 8, 0, n["x"], n["y"]))
            raw_events.append((hit_t + 26, 9, 0, n["x"], n["y"]))
        elif n["type"] == 3 and n["slider_duration"] > 0:
            raw_events.append((hit_t, 4, 0, n["x"], n["y"]))
            pts = [(n["x"], n["y"])] + list(n["slider_path"])
            seg_cnt = max(1, len(pts) - 1)
            sdur = n["slider_duration"]
            step_ms = 14
            t_cur = hit_t + step_ms
            end_t = n["time"] + sdur
            while t_cur <= end_t:
                prog = max(0.0, min(1.0, (t_cur - n["time"]) / max(1, sdur)))
                f_idx = prog * seg_cnt
                s_i = min(seg_cnt - 1, int(f_idx))
                frac = f_idx - s_i
                px = pts[s_i][0] + (pts[s_i + 1][0] - pts[s_i][0]) * frac
                py = pts[s_i][1] + (pts[s_i + 1][1] - pts[s_i][1]) * frac
                raw_events.append((t_cur, 3, 0, round(px, 4), round(py, 4)))
                t_cur += step_ms
            last_x, last_y = pts[-1]
            raw_events.append((end_t + 16, 5, 0, round(last_x, 4), round(last_y, 4)))
        else:
            raw_events.append((hit_t, 4, 0, n["x"], n["y"]))
            raw_events.append((hit_t + 24, 5, 0, n["x"], n["y"]))

    max_t = max(
        max((n["time"] + n["duration"] for n in kbs), default=0),
        max((n["time"] + n["slider_duration"] for n in mns), default=0),
    ) + 600
    for t in range(0, max_t + 16, 16):
        raw_events.append((t, 0, 0, 0.5, 0.5))

    # Sort monotonically by time; PointerScale first, Pointer before Down, Down before Tick, Up after
    kind_order = {7: 0, 3: 1, 1: 2, 4: 3, 8: 3, 0: 4, 2: 5, 5: 6, 9: 6}
    raw_events.sort(key=lambda e: (e[0], kind_order.get(e[1], 4)))

    replay_obj = {
        "format": "sakura-replay",
        "version": 2,
        "chart_id": "sidechain_dream",
        "chart_hash": chash,
        "difficulty": 2,
        "rate": 1.0,
        "start_ms": 0,
        "pointer_scale_x": 1.3468,
        "pointer_scale_y": 1.0,
        "inputs": [[t, k, l, round(float(x), 5), round(float(y), 5)] for (t, k, l, x, y) in raw_events],
    }
    out_path = REPLAYS_DIR / "sidechain_expert.skr"
    out_path.write_text(json.dumps(replay_obj, separators=(",", ":")), encoding="utf-8")
    print(f"  wrote {out_path.relative_to(PROMO)} ({len(raw_events)} events, hash={chash})")


def tpt(nx: float, ny: float) -> list[float]:
    return [round(0.54 + nx * 0.34, 4), round(0.24 + ny * 0.56, 4)]


def tutorial_lesson6_actions() -> list[dict]:
    actions: list[dict] = [
        # Lesson 0: Keyboard Tap (-35.8s)
        {"at": -35.8, "key": "Space"},
        {"at": -34.6, "key": "A"},
        {"at": -33.2, "key": "S"},
        {"at": -31.8, "key": "D"},
        {"at": -30.4, "key": "F"},
        # Lesson 1: Keyboard Hold (-30.1s)
        {"at": -30.1, "key": "Space"},
        {"at": -29.9, "key": "Space"},
        {"at": -28.5, "key_down": "A"},
        {"at": -27.2, "key_up": "A"},
        {"at": -26.5, "key_down": "S"},
        {"at": -25.1, "key_up": "S"},
        {"at": -24.3, "key_down": "D"},
        {"at": -22.7, "key_up": "D"},
        # Lesson 2: Mouse Circle (-22.4s)
        {"at": -22.4, "key": "Space"},
        {"at": -22.2, "key": "Space"},
        {"at": -21.0, "click": tpt(0.20, 0.28)},
        {"at": -19.7, "click": tpt(0.74, 0.30)},
        {"at": -18.4, "click": tpt(0.48, 0.58)},
        {"at": -17.1, "click": tpt(0.28, 0.74)},
        {"at": -15.8, "click": tpt(0.78, 0.70)},
        # Lesson 3: Mouse Slide (-15.4s)
        {"at": -15.4, "key": "Space"},
        {"at": -15.2, "key": "Space"},
        {"at": -13.8, "mouse_down": tpt(0.18, 0.24)},
        {"at": -13.78, "move": tpt(0.40, 0.30), "duration": 0.56},
        {"at": -13.18, "move": tpt(0.68, 0.48), "duration": 0.56},
        {"at": -12.58, "move": tpt(0.80, 0.72), "duration": 0.56},
        {"at": -11.85, "mouse_up": tpt(0.80, 0.72)},
        {"at": -10.5, "mouse_down": tpt(0.78, 0.26)},
        {"at": -10.48, "move": tpt(0.64, 0.40), "duration": 0.53},
        {"at": -9.91, "move": tpt(0.40, 0.60), "duration": 0.53},
        {"at": -9.35, "move": tpt(0.22, 0.78), "duration": 0.53},
        {"at": -8.65, "mouse_up": tpt(0.22, 0.78)},
        # Lesson 4: Mixed (-8.3s)
        {"at": -8.3, "key": "Space"},
        {"at": -8.1, "key": "Space"},
        {"at": -6.9, "key": "A"},
        {"at": -5.9, "click": tpt(0.28, 0.32)},
        {"at": -4.8, "key": "D"},
        {"at": -3.8, "click": tpt(0.72, 0.38)},
        {"at": -2.7, "key": "S"},
        {"at": -1.6, "click": tpt(0.52, 0.68)},
        # Advance to Lesson 5 (6th lesson: 樱枝横梁与组合技)
        {"at": -1.0, "key": "Space"},
        # Recorded portion (t >= 0): start Lesson 6 at 0.5s and execute all 5 synergy notes
        {"at": 0.5, "key": "Space"},
        {"at": 1.9, "key": "Space"},
        {"at": 3.3, "key": "Space"},
        {"at": 4.9, "key": "S"},
        {"at": 4.9, "click": tpt(0.45, 0.42)},
        {"at": 6.55, "key_down": "Space"},
        {"at": 6.7, "click": tpt(0.56, 0.58)},
        {"at": 6.9, "key_up": "Space"},
    ]
    return actions


def build_shots_scripts() -> None:
    video_args = [
        "-vf", "scale=out_color_matrix=bt709:out_range=tv,setparams=color_primaries=bt709:color_trc=bt709",
        "-c:v", "h264_nvenc", "-preset", "p7", "-tune", "hq", "-rc", "vbr", "-cq", "14", "-b:v", "0",
        "-maxrate", "400M", "-bufsize", "800M", "-g", "60", "-bf", "0", "-pix_fmt", "yuv420p",
        "-colorspace", "bt709", "-color_primaries", "bt709", "-color_trc", "bt709", "-color_range", "tv",
        "-movflags", "+faststart",
    ]
    tutorial_shot = {
        "name": "tutorial",
        "scene": "tutorial",
        "warmup_seconds": 36.0,
        "seconds": 8.5,
        "actions": tutorial_lesson6_actions(),
    }
    telemetry_shot = {
        "name": "play_telemetry",
        "scene": "game",
        "chart": "sidechain_dream",
        "difficulty": 2,
        "mode": "replay",
        "replay": "replays/sidechain_expert.skr",
        "warmup_seconds": 60.5,
        "until": "result",
        "tail_seconds": 7.5,
        "max_seconds": 90,
        "actions": [
            {"at": 5.6, "key": "Tab"},
        ],
    }
    full_shots = {
        "width": 3840,
        "height": 2160,
        "fps": 60,
        "preview_width": 1280,
        "preview_height": 720,
        "ffmpeg": "ffmpeg",
        "userdata_template": "userdata_template",
        "video_args": video_args,
        "config": {
            "visual.bga_enabled": True,
            "graphics.background_blur": True,
        },
        "shots": [
            {
                "name": "menu",
                "scene": "menu",
                "seconds": 7.0,
                "actions": [
                    {"at": 1.0, "move": [0.20, 0.43], "duration": 0.5},
                    {"at": 2.1, "move": [0.20, 0.51], "duration": 0.5},
                    {"at": 3.2, "move": [0.20, 0.59], "duration": 0.5},
                    {"at": 4.3, "move": [0.20, 0.67], "duration": 0.5},
                    {"at": 5.4, "move": [0.20, 0.43], "duration": 0.5},
                ],
            },
            {
                "name": "select",
                "scene": "select",
                "chart": "sakura_breath",
                "difficulty": 2,
                "seconds": 8.0,
                "actions": [
                    {"at": 1.0, "key": "Up"},
                    {"at": 1.9, "key": "Up"},
                    {"at": 2.8, "key": "Up"},
                    {"at": 3.8, "key": "Down"},
                    {"at": 4.7, "key": "Down"},
                    {"at": 5.6, "key": "Down"},
                    {"at": 6.4, "key": "Left"},
                    {"at": 7.1, "key": "Right"},
                ],
            },
            {
                "name": "practice",
                "scene": "select",
                "chart": "sakura_breath",
                "difficulty": 2,
                "seconds": 6.0,
                "actions": [
                    {"at": 0.6, "click": [0.7275, 0.616]},
                    {"at": 1.6, "click": [0.8435, 0.6955]},
                    {"at": 2.2, "click": [0.8435, 0.6955]},
                    {"at": 3.1, "click": [0.883, 0.782]},
                ],
            },
            {
                "name": "editor",
                "scene": "editor",
                "chart": "instant_bloom",
                "difficulty": 2,
                "seconds": 7.0,
                "actions": [
                    {"at": 0.5, "click": [0.90, 0.74]},
                    {"at": 0.9, "key": "Space"},
                ],
            },
            {
                "name": "settings",
                "scene": "settings",
                "page": 0,
                "seconds": 6.0,
                "actions": [
                    {"at": 1.2, "key": "Tab"},
                    {"at": 2.4, "key": "Tab"},
                    {"at": 3.8, "key": "Tab"},
                    {"at": 3.9, "key": "Tab"},
                ],
            },
            tutorial_shot,
            {
                "name": "stats",
                "scene": "stats",
                "seconds": 7.0,
                "actions": [
                    {"at": 3.6, "click": [0.87, 0.955]},
                ],
            },
            {
                "name": "calibration",
                "scene": "calibration",
                "seconds": 5.5,
                "actions": [
                    {"at": 1.0, "key": "Space"},
                    {"at": 1.5, "key": "Space"},
                    {"at": 2.0, "key": "Space"},
                    {"at": 2.5, "key": "Space"},
                    {"at": 3.0, "key": "Space"},
                    {"at": 3.5, "key": "Space"},
                ],
            },
            {
                "name": "play_synergy",
                "scene": "game",
                "chart": "petal_storm_overdrive",
                "difficulty": 1,
                "mode": "auto",
                "start_ms": 176000,
                "end_ms": 192000,
                "seconds": 12.0,
            },
            telemetry_shot,
            {
                "name": "play_expert",
                "scene": "game",
                "chart": "sakura_breath",
                "difficulty": 2,
                "mode": "auto",
                "until": "result",
                "tail_seconds": 6.0,
                "max_seconds": 260,
            },
            {
                "name": "play_hard",
                "scene": "game",
                "chart": "sakura_breath",
                "difficulty": 1,
                "mode": "auto",
                "start_ms": 0,
                "end_ms": 52000,
                "seconds": 50.0,
            },
        ],
    }
    shots_path = PROMO / "record" / "shots.json"
    shots_path.write_text(json.dumps(full_shots, indent=2, ensure_ascii=False), encoding="utf-8")
    verify_shots = dict(full_shots)
    verify_shots["width"] = 1280
    verify_shots["height"] = 720
    verify_shots["shots"] = [tutorial_shot, telemetry_shot]
    verify_path = PROMO / "record" / "shots_verify.json"
    verify_path.write_text(json.dumps(verify_shots, indent=2, ensure_ascii=False), encoding="utf-8")
    print(f"  wrote {shots_path.relative_to(PROMO)} (12 shots) and {verify_path.relative_to(PROMO)}")


def main() -> None:
    build_database()
    build_telemetry_replay()
    build_shots_scripts()


if __name__ == "__main__":
    main()
