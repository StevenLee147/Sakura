"""Build Sakura's original starter album, authored patterns and vector cover art.
Deterministic, no downloaded samples. Requires numpy, scipy, Pillow. Run from repo root.
The 40-bar arrangement: introduction, A, B, interlude, return, coda.
"""
from pathlib import Path
import json, math, wave
import numpy as np
from scipy.signal import lfilter
from PIL import Image, ImageDraw, ImageFont

ROOT = Path(__file__).resolve().parents[1]
SR = 44100
SONGS = [
    ("spring_breeze", 130, 62, [0, 5, 7, 2], (117, 186, 189), "01", "Spring Breeze"),
    ("cherry_blossom", 155, 57, [0, 5, 3, 7], (227, 157, 182), "02", "Cherry Blossom"),
    ("digital_dream", 175, 54, [0, 3, 5, 7], (135, 163, 224), "03", "Digital Dream"),
    ("sakura_storm", 200, 57, [0, 7, 5, 3], (211, 128, 162), "04", "Sakura Storm"),
]

def tone(midi, duration, voice):
    t=np.arange(int(duration*SR),dtype=np.float32)/SR
    f=440*2**((midi-69)/12)
    if voice=="pad":
        wave=(np.sin(2*np.pi*f*t)+0.45*np.sin(2*np.pi*f*1.003*t)+0.22*np.sin(4*np.pi*f*t))/1.67
        env=np.minimum(t/0.18,1)*np.minimum((duration-t)/0.5,1)
    elif voice=="bass":
        wave=np.sin(2*np.pi*f*t)+0.23*np.sin(4*np.pi*f*t)+0.08*np.sin(6*np.pi*f*t)
        env=np.minimum(t/0.008,1)*np.exp(-t*3)*np.minimum((duration-t)/0.025,1)
    else:
        wave=np.sin(2*np.pi*f*t+0.38*np.sin(2*np.pi*f*2*t)*np.exp(-t*7))+0.15*np.sin(4*np.pi*f*t)*np.exp(-t*6)
        env=np.minimum(t/0.004,1)*np.exp(-t*(5 if voice=="pluck" else 2.5))*np.minimum((duration-t)/0.035,1)
    return (wave*env).astype(np.float32)

def build_audio(folder,bpm,root,progression,seed):
    beat=60/bpm; duration=160*beat+2.5
    output=np.zeros((int(duration*SR),2),np.float32)
    rng=np.random.default_rng(seed); cache={}
    def add(at,samples,level=1,pan=0):
        start=int(at*SR); n=min(len(samples),len(output)-start)
        if n<=0:return
        output[start:start+n,0]+=samples[:n]*level*math.sqrt((1-pan)/2)
        output[start:start+n,1]+=samples[:n]*level*math.sqrt((1+pan)/2)
    def note(at,midi,length,voice,level,pan=0):
        key=(midi,round(length,5),voice)
        if key not in cache:cache[key]=tone(*key)
        add(at,cache[key],level,pan)
    t=np.arange(int(SR*0.38),dtype=np.float32)/SR
    kick=np.sin(2*np.pi*(49*t+8*(1-np.exp(-t*36))))*np.exp(-t*13)+rng.normal(0,0.09,len(t))*np.exp(-t*240)
    t=np.arange(int(SR*0.19),dtype=np.float32)/SR
    noise=rng.normal(0,1,len(t)).astype(np.float32)
    snare=(noise-lfilter([0.1],[1,-0.9],noise))*np.exp(-t*29)*0.30+np.sin(2*np.pi*190*t)*np.exp(-t*34)*0.20
    t=np.arange(int(SR*0.065),dtype=np.float32)/SR
    noise=rng.normal(0,1,len(t)).astype(np.float32)
    hat=(noise-lfilter([0.18],[1,-0.82],noise))*np.exp(-t*75)*0.15
    motif=[0,7,12,10,7,3,5,7,12,14,12,7,5,3,7,2]
    for bar in range(40):
        at=bar*4*beat; base=root+progression[(bar//2)%4]
        section=0 if bar<4 else 1 if bar<12 else 2 if bar<20 else 3 if bar<24 else 4 if bar<36 else 5
        bright=section in (2,4)
        for k,interval in enumerate([0,3 if (bar//2)%4!=1 else 4,7,14]):
            note(at,base+interval,beat*4.8,"pad",0.058,(-0.55+k*0.36))
        for step in range(8):
            pitch=base+12+[0,7,12,15,14,7,10,7][(step+bar%2*2)%8]
            note(at+step*beat/2,pitch,beat*1.8,"pluck",0.067 if section!=3 else 0.04,math.sin(step*1.1)*0.6)
        if section not in (0,3,5):
            for step in range(8 if bright else 4):
                pos=step*(0.5 if bright else 1)
                pitch=root+24+motif[(bar*3+step)%16]
                note(at+pos*beat,pitch,beat*1.5,"bell",0.12 if bright else 0.085,math.sin(bar*0.4)*0.2)
        if section!=3:
            for step in range(4):
                if section==0 and bar<2 and step%2:continue
                note(at+step*beat,base-24,beat*0.9,"bass",0.19)
                if step%2==0 or bright:add(at+step*beat,kick,0.48)
                if step%2 and section not in (0,5):add(at+step*beat,snare,0.55)
            for step in range(8 if bright else 4):
                add(at+step*beat*(0.5 if bright else 1),hat,0.32 if step%2 else 0.48,(-1 if step%2 else 1)*0.35)
            if bright and bar%4==3:
                for step in range(4):add(at+(3+step/4)*beat,snare,0.10+step*0.035,step*0.15-0.2)
    # A quiet stereo echo and short diffuse reflections retain clear percussion transients.
    dry=output.copy()
    for delay,gain in [(beat*0.75,0.12),(beat*1.5,0.065),(0.043,0.035),(0.073,0.025)]:
        n=int(delay*SR);output[n:]+=dry[:-n,::-1]*gain
    output*=np.minimum(np.arange(len(output))/SR/0.06,1)[:,None]
    output*=np.minimum((len(output)-np.arange(len(output)))/SR/2.3,1)[:,None]
    output=np.tanh(output*1.22)
    output*=0.89/max(0.89,float(np.max(np.abs(output))))
    with wave.open(str(folder/"music.wav"),"wb") as file:
        file.setnchannels(2);file.setsampwidth(2);file.setframerate(SR);file.writeframes((output*32767).astype("<i2").tobytes())
    return duration

import argparse

STARTER_DIFFS = {
    "spring_breeze": [("Normal", 3.0, "normal.json"), ("Hard", 5.5, "hard.json")],
    "cherry_blossom": [("Normal", 4.0, "normal.json"), ("Hard", 7.0, "hard.json"), ("Expert", 10.0, "expert.json")],
    "digital_dream": [("Hard", 8.5, "hard.json"), ("Expert", 12.0, "expert.json")],
    "sakura_storm": [("Expert", 14.0, "expert.json")],
}

MOTIF = [0, 7, 12, 10, 7, 3, 5, 7, 12, 14, 12, 7, 5, 3, 7, 2]
X_MIN, X_MAX = 0.16, 0.84
Y_MIN, Y_MAX = 0.18, 0.82
MIN_MOUSE_SEP = 0.18

def target_mouse_jump(dt_ms: float) -> float:
    val = 0.15 + 0.50 * (1.0 - math.exp(-max(0.0, float(dt_ms)) / 620.0))
    return min(0.68, max(0.15, val))

def _in_mouse_box(x: float, y: float) -> bool:
    return X_MIN <= x <= X_MAX and Y_MIN <= y <= Y_MAX

def _max_box_reach(x: float, y: float) -> float:
    return max(math.hypot(cx - x, cy - y) for cx in (X_MIN, X_MAX) for cy in (Y_MIN, Y_MAX))

def _section_of(bar: int) -> str:
    if bar < 4: return "intro"
    if bar < 12: return "verse"
    if bar < 20: return "chorus"
    if bar < 24: return "bridge"
    if bar < 36: return "climax"
    return "coda"

def _plan_keyboard(song_idx: int, bpm: float, diff_name: str, level: float) -> list[dict]:
    beat = 60000.0 / bpm
    events = []
    for bar in range(2, 40):
        sec = _section_of(bar)
        b0 = bar * 4.0
        if diff_name == "Normal":
            if sec == "intro":
                if bar == 2:
                    events.extend([(b0, 1.5, False), (b0 + 2.0, 0.0, False), (b0 + 3.0, 0.0, False)])
                else:
                    pts = [0.0, 1.0, 2.0, 3.0] if level >= 4.0 else [0.0, 2.0, 3.0]
                    events.extend((b0 + p, 0.0, False) for p in pts)
            elif sec == "bridge":
                events.extend([(b0, 2.0 if bar % 2 == 0 else 1.5, False), (b0 + 3.0, 0.0, False)])
            elif sec == "coda":
                if bar == 36:
                    events.extend([(b0, 2.0, False), (b0 + 3.0, 0.0, False)])
                elif bar < 39:
                    events.extend([(b0, 0.0, False), (b0 + 2.0, 0.0, False)])
                else:
                    events.append((b0, 1.5, False))
            elif bar % 4 == 0:
                tail = [2.0, 3.0, 3.5] if (sec in ("chorus", "climax") and level >= 4.0) else [2.0, 3.0]
                events.append((b0, 1.5, False))
                events.extend((b0 + p, 0.0, False) for p in tail)
            else:
                if sec == "verse":
                    pts = [0.0, 1.0, 2.0, 3.0, 3.5] if (level >= 4.0 and bar % 2 == 1) else [0.0, 1.0, 2.0, 3.0]
                else:
                    pts = [0.0, 1.0, 1.5, 2.0, 3.0, 3.5] if (level >= 4.0 or bar % 2 == 1) else [0.0, 1.0, 2.0, 2.5, 3.0]
                events.extend((b0 + p, 0.0, False) for p in pts)
        elif diff_name == "Hard":
            if sec == "intro":
                if bar == 2:
                    events.extend([(b0, 1.5, False), (b0 + 2.0, 0.0, False), (b0 + 3.0, 0.0, False)])
                else:
                    events.extend((b0 + p, 0.0, p == 0.0) for p in [0.0, 1.0, 2.0, 3.0, 3.5])
            elif sec == "bridge":
                events.append((b0, 1.5, False))
                events.extend((b0 + p, 0.0, False) for p in [2.0, 2.5, 3.0, 3.5])
            elif sec == "coda":
                if bar in (36, 38):
                    events.extend([(b0, 1.5, False), (b0 + 2.0, 0.0, False), (b0 + 3.0, 0.0, False)])
                elif bar == 37:
                    events.extend((b0 + p, 0.0, p == 0.0) for p in [0.0, 1.0, 2.0, 3.0])
                else:
                    events.append((b0, 2.0, False))
            elif sec == "verse":
                if bar % 4 == 0:
                    events.append((b0, 1.5, False))
                    pts = [1.0, 2.0, 2.5, 3.0, 3.5] if level >= 7.0 else [2.0, 2.5, 3.0, 3.5]
                    events.extend((b0 + p, 0.0, False) for p in pts)
                else:
                    pts = [0.0, 0.5, 1.0, 2.0, 2.5, 3.0, 3.5] if level >= 8.0 else ([0.0, 1.0, 1.5, 2.0, 3.0, 3.5] if level >= 6.5 else [0.0, 1.0, 2.0, 2.5, 3.0])
                    events.extend((b0 + p, 0.0, p == 0.0 and bar % 2 == 0) for p in pts)
            else:
                if bar % 4 == 0:
                    events.append((b0, 1.0, False))
                    pts = [0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5] if level >= 6.8 else [1.5, 2.0, 2.5, 3.0, 3.5]
                    events.extend((b0 + p, 0.0, p == 2.0) for p in pts)
                elif bar % 4 == 2:
                    events.extend([(b0, 0.0, True), (b0 + 0.5, 0.0, False), (b0 + 1.0, 0.0, False), (b0 + 1.5, 0.0, False), (b0 + 2.0, 1.0, False), (b0 + 3.5, 0.0, False)])
                else:
                    pts = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5]
                    if level < 6.5:
                        pts = [0.0, 0.5, 1.0, 2.0, 2.5, 3.0, 3.5]
                    elif level >= 8.0 and bar % 4 == 3:
                        pts = [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.25, 3.5, 3.75]
                    events.extend((b0 + p, 0.0, p == 0.0) for p in pts)
        else:
            if sec == "intro":
                if bar == 2:
                    events.append((b0, 1.5, False))
                    events.extend((b0 + p, 0.0, False) for p in [0.5, 1.0, 2.0, 2.5, 3.0, 3.5])
                else:
                    events.extend((b0 + p, 0.0, p == 0.0) for p in [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.25, 3.5, 3.75])
            elif sec == "bridge":
                events.append((b0, 1.5, False))
                events.extend((b0 + p, 0.0, False) for p in [0.5, 1.0, 2.0, 2.5, 3.0, 3.5])
            elif sec == "coda":
                if bar in (36, 38):
                    events.append((b0, 1.5, False))
                    events.extend((b0 + p, 0.0, False) for p in [2.0, 2.5, 3.0, 3.5])
                elif bar == 37:
                    events.extend((b0 + p, 0.0, p == 0.0) for p in [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5])
                else:
                    events.append((b0, 2.0, False))
            elif sec == "verse":
                if bar % 4 == 0:
                    events.append((b0, 1.5, False))
                    events.extend((b0 + p, 0.0, False) for p in [0.5, 1.0, 2.0, 2.5, 2.75, 3.0, 3.5])
                elif bar % 4 == 2:
                    events.extend([(b0, 0.0, True), (b0 + 0.5, 0.0, False), (b0 + 0.75, 0.0, False), (b0 + 1.0, 0.0, False), (b0 + 1.5, 0.0, False), (b0 + 2.0, 1.0, False), (b0 + 3.0, 0.0, False), (b0 + 3.5, 0.0, False)])
                else:
                    pts = [0.0, 0.5, 0.75, 1.0, 1.5, 2.0, 2.5, 2.75, 3.0, 3.5] if level >= 11.5 else [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 2.75, 3.0, 3.5]
                    if level >= 13.5 and bar % 2 == 1:
                        pts = [0.0, 0.5, 0.75, 1.0, 1.5, 1.75, 2.0, 2.5, 3.0, 3.25, 3.5, 3.75]
                    events.extend((b0 + p, 0.0, p == 0.0) for p in pts)
            else:
                if bar % 4 == 0:
                    events.append((b0, 1.0, False))
                    pts = [0.5, 1.0, 1.5, 2.0, 2.5, 2.75, 3.0, 3.5, 3.75] if level >= 11.5 else [0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.5]
                    events.extend((b0 + p, 0.0, p == 2.0) for p in pts)
                elif bar % 4 == 3:
                    pts = [0.0, 0.5, 0.75, 1.0, 1.5, 2.0, 2.5, 3.0, 3.25, 3.5, 3.75] if level >= 11.5 else [0.0, 0.5, 1.0, 1.5, 2.0, 2.5, 3.0, 3.25, 3.5, 3.75]
                    if level >= 13.5:
                        pts = [0.0, 0.5, 0.75, 1.0, 1.25, 1.5, 2.0, 2.5, 2.75, 3.0, 3.25, 3.5, 3.75]
                    events.extend((b0 + p, 0.0, p == 0.0) for p in pts)
                elif bar % 4 == 2 and bar % 8 == 6:
                    events.extend([(b0, 0.0, True), (b0 + 0.5, 0.0, False), (b0 + 1.0, 0.0, False), (b0 + 1.5, 0.0, False), (b0 + 2.0, 1.0, False), (b0 + 3.0, 0.0, False), (b0 + 3.5, 0.0, False)])
                else:
                    if level >= 13.5:
                        pts = [0.0, 0.25, 0.5, 1.0, 1.5, 1.75, 2.0, 2.5, 2.75, 3.0, 3.5, 3.75] if bar % 2 == 1 else [0.0, 0.5, 0.75, 1.0, 1.5, 2.0, 2.25, 2.5, 3.0, 3.25, 3.5, 3.75]
                    elif level >= 11.5:
                        pts = [0.0, 0.5, 0.75, 1.0, 1.5, 1.75, 2.0, 2.5, 2.75, 3.0, 3.5, 3.75]
                    else:
                        pts = [0.0, 0.5, 0.75, 1.0, 1.5, 2.0, 2.5, 2.75, 3.0, 3.5]
                    events.extend((b0 + p, 0.0, p == 0.0 or (sec == "climax" and p == 2.0 and level >= 11.5)) for p in pts)

    events.sort(key=lambda e: (round(e[0] * beat), -e[1]))
    kb = []
    free_at = [-10000] * 4
    hold_end = [-10000] * 4
    tap_gap = max(125, round(0.45 * beat))
    hold_gap = max(185, round(0.50 * beat))
    chord_pairs = [(0, 2), (1, 3), (0, 3), (1, 2)]
    stairs = [[0, 1, 2, 3], [3, 2, 1, 0], [0, 1, 3, 2], [3, 2, 0, 1]]
    prev_lane = (song_idx + 1) % 4
    chord_idx = song_idx

    for step_i, (b, hold_beats, want_chord) in enumerate(events):
        t = round(b * beat)
        dur = round(hold_beats * beat) if hold_beats > 0 else 0
        active_holds = sum(1 for end_t in hold_end if end_t > t)
        same_time = [n["lane"] for n in kb if n["time"] == t]
        if active_holds + len(same_time) >= 2:
            continue
        if want_chord and dur == 0 and active_holds == 0 and not same_time:
            placed_pair = None
            for k in range(len(chord_pairs)):
                c0, c1 = chord_pairs[(chord_idx + k) % len(chord_pairs)]
                if t >= free_at[c0] and t >= free_at[c1]:
                    placed_pair = (c0, c1)
                    chord_idx += 1
                    break
            if placed_pair is not None:
                for ln in placed_pair:
                    kb.append(dict(time=t, lane=ln, type="tap", duration=0))
                    free_at[ln] = t + tap_gap
                prev_lane = placed_pair[1]
                continue
        pat = stairs[(int(b // 4) + song_idx) % 4]
        pref = pat[step_i % 4]
        order = [l for l in [pref, (pref + 1) % 4, (pref + 3) % 4, (pref + 2) % 4] if l != prev_lane and l not in same_time] + [prev_lane]
        chosen = next((l for l in order if l not in same_time and t >= free_at[l]), None)
        if chosen is None:
            continue
        kb.append(dict(time=t, lane=chosen, type="hold" if dur > 0 else "tap", duration=dur))
        free_at[chosen] = t + (dur + hold_gap if dur > 0 else tap_gap)
        if dur > 0:
            hold_end[chosen] = t + dur
        prev_lane = chosen

    kb.sort(key=lambda n: (n["time"], n["lane"]))
    return kb

def _plan_mouse(song_idx: int, bpm: float, diff_name: str, level: float) -> list[dict]:
    beat = 60000.0 / bpm
    blueprints = []
    for bar in range(2, 39):
        sec = _section_of(bar)
        b0 = bar * 4.0
        raw = []
        if sec == "intro":
            if bar == 2:
                raw.append((0.0, 0.0))
            else:
                raw.append((0.0, 1.5 if diff_name != "Normal" else 0.0))
        elif sec == "bridge":
            raw.append((0.0, 1.5 if bar % 2 == 0 else 0.0))
        elif sec == "coda":
            raw.append((0.0, 1.5 if bar == 36 else 0.0))
        elif diff_name == "Normal":
            if bar % 3 == 1:
                raw.append((0.0, 1.5))
            elif sec == "verse" and level < 4.0 and bar % 2 == 1:
                raw.append((2.0, 0.0))
            else:
                raw.extend([(0.0, 0.0), (2.0, 0.0)])
        elif diff_name == "Hard":
            if bar % 3 == 1:
                raw.extend([(0.0, 1.0), (2.5, 0.0)])
            elif sec in ("chorus", "climax") and (bar % 2 == 0 or level >= 8.0):
                raw.extend([(0.0, 0.0), (1.5, 0.0), (3.0, 0.0)])
            else:
                raw.extend([(0.0, 0.0), (2.0, 0.0)])
        else:
            if bar % 3 == 1 or (sec == "climax" and bar % 6 == 5):
                raw.extend([(0.0, 1.0), (2.0, 0.0), (3.0, 0.0)])
            elif sec == "verse" and level < 11.5:
                raw.extend([(0.0, 0.0), (1.5, 0.0), (3.0, 0.0)])
            elif bar % 2 == 0:
                raw.extend([(0.0, 0.0), (1.0, 0.0), (2.0, 0.0), (3.0, 0.0)])
            else:
                raw.extend([(0.0, 0.0), (1.5, 0.0), (2.5, 0.0), (3.5, 0.0)])
        for pos, sl_beats in raw:
            t = round((b0 + pos) * beat)
            sdur = round(sl_beats * beat) if sl_beats > 0 else 0
            m_pitch = MOTIF[(bar * 3 + int(round(pos * 2)) + song_idx) % len(MOTIF)] / 14.0
            target_y = round(0.76 - 0.52 * m_pitch, 4)
            blueprints.append((t, sdur, sl_beats, target_y))

    sep_guard = 0.186
    angles_180 = [k * math.tau / 180.0 for k in range(180)]
    angles_36 = [k * math.tau / 36.0 for k in range(36)]

    def clear_of_recent(px: float, py: float, cur_t: int, placed: list[dict]) -> bool:
        for m in reversed(placed):
            m_end = m["time"] + m["slider_duration"]
            if cur_t - m_end > 1200:
                break
            if math.hypot(px - m["x"], py - m["y"]) < sep_guard:
                return False
            for wp in m["slider_path"]:
                if math.hypot(px - wp[0], py - wp[1]) < sep_guard:
                    return False
        return True

    def build_slider_arc(nx: float, ny: float, cur_t: int, sdur: int, sl_beats: float, next_req: float, placed: list[dict], pref_dir: float):
        chord = min(0.32, max(0.24, 0.18 + 0.08 * sl_beats))
        best = []
        for a in angles_36:
            x2 = round(nx + chord * math.cos(a), 4)
            y2 = round(ny + chord * math.sin(a), 4)
            if not _in_mouse_box(x2, y2) or _max_box_reach(x2, y2) < next_req:
                continue
            ux, uy = (x2 - nx) / chord, (y2 - ny) / chord
            px_n, py_n = -uy, ux
            if (0.5 - 0.5 * (nx + x2)) * px_n + (0.5 - 0.5 * (ny + y2)) * py_n < 0:
                px_n, py_n = -px_n, -py_n
            bulge = 0.14 * chord
            x1 = round(0.5 * (nx + x2) + bulge * px_n, 4)
            y1 = round(0.5 * (ny + y2) + bulge * py_n, 4)
            if not _in_mouse_box(x1, y1):
                continue
            if not clear_of_recent(x1, y1, cur_t, placed) or not clear_of_recent(x2, y2, cur_t, placed):
                continue
            score = abs((x2 - nx) - pref_dir * 0.22) + 0.35 * abs(y2 - 0.5)
            best.append((score, [[x1, y1], [x2, y2]]))
        best.sort(key=lambda item: item[0])
        return [item[1] for item in best[:3]]

    placed: list[dict] = []

    def dfs(idx: int) -> bool:
        if idx >= len(blueprints):
            return True
        t, sdur, sl_beats, target_y = blueprints[idx]
        next_dt = (blueprints[idx + 1][0] - (t + sdur)) if idx + 1 < len(blueprints) else 450
        next_req = min(0.68, target_mouse_jump(next_dt)) + 0.024
        pref_dir = 1.0 if (not placed or (placed[-1]["slider_path"][-1][0] if placed[-1]["type"] == "slider" else placed[-1]["x"]) < 0.5) else -1.0

        cands = []
        if idx == 0:
            starts = [(0.24, 0.30), (0.24, 0.68), (0.76, 0.32), (0.76, 0.68)]
            sx, sy = starts[song_idx % 4]
            if sdur > 0:
                for path in build_slider_arc(sx, sy, t, sdur, sl_beats, next_req, placed, pref_dir):
                    cands.append((0.0, sx, sy, path))
            else:
                cands.append((0.0, sx, sy, []))
        else:
            prev = placed[-1]
            prev_end_t = prev["time"] + prev["slider_duration"]
            ax, ay = prev["slider_path"][-1] if prev["type"] == "slider" else (prev["x"], prev["y"])
            req_d = target_mouse_jump(t - prev_end_t)
            head_d = target_mouse_jump(t - prev["time"])
            for theta in angles_180:
                nx = round(ax + req_d * math.cos(theta), 4)
                ny = round(ay + req_d * math.sin(theta), 4)
                if not _in_mouse_box(nx, ny):
                    continue
                if abs(math.hypot(nx - ax, ny - ay) - req_d) > 0.003:
                    continue
                if not clear_of_recent(nx, ny, t, placed):
                    continue
                base_cost = abs(ny - target_y) + (0.18 if (nx - ax) * pref_dir < 0 else 0.0)
                if prev["type"] == "slider":
                    base_cost += 0.45 * abs(math.hypot(nx - prev["x"], ny - prev["y"]) - head_d)
                if sdur > 0:
                    arcs = build_slider_arc(nx, ny, t, sdur, sl_beats, next_req, placed, pref_dir)
                    if arcs:
                        cands.append((base_cost, nx, ny, arcs[0]))
                else:
                    if _max_box_reach(nx, ny) < next_req:
                        continue
                    cands.append((base_cost, nx, ny, []))
            cands.sort(key=lambda c: c[0])

        for _, nx, ny, path in cands[:8]:
            placed.append(dict(
                time=t, x=nx, y=ny,
                type="slider" if sdur > 0 else "circle",
                slider_duration=sdur, slider_path=path,
            ))
            if dfs(idx + 1):
                return True
            placed.pop()
        return False

    if not dfs(0):
        raise RuntimeError(f"Failed to solve mouse trajectory for song={song_idx} {diff_name}")
    return placed

def _build_sv_points(song_id: str, bpm: float, diff_name: str) -> list[dict]:
    if diff_name != "Expert":
        return [dict(time=0, speed=1.0, easing="linear")]
    beat = 60000.0 / bpm
    if song_id == "sakura_storm":
        spec = [(0, 1.0), (4, 1.08), (12, 1.16), (16, 1.22), (20, 0.84), (24, 1.18), (30, 1.25), (36, 0.90)]
    elif song_id == "digital_dream":
        spec = [(0, 1.0), (4, 1.08), (12, 1.16), (20, 0.85), (24, 1.20), (36, 0.92)]
    else:
        spec = [(0, 1.0), (4, 1.06), (12, 1.14), (20, 0.86), (24, 1.18), (36, 0.92)]
    return [dict(time=round(bar * 4 * beat), speed=spd, easing="linear") for bar, spd in spec]

def build_chart(bpm: float, difficulty=1, *, song_id: str = "spring_breeze", song_idx: int = 0, diff_name: str | None = None, level: float | None = None) -> dict:
    if diff_name is None:
        diff_name = {0: "Normal", 1: "Hard", 2: "Expert"}.get(difficulty, "Hard")
    if level is None:
        level = {"Normal": 3.5, "Hard": 7.0, "Expert": 11.0}.get(diff_name, 7.0)
    kb = _plan_keyboard(song_idx, bpm, diff_name, level)
    mouse = _plan_mouse(song_idx, bpm, diff_name, level)
    sv = _build_sv_points(song_id, bpm, diff_name)
    return dict(
        version=2,
        timing_points=[dict(time=0, bpm=bpm, time_signature=[4, 4])],
        sv_points=sv,
        keyboard_notes=kb,
        mouse_notes=mouse,
    )

def cover(folder,color,index,title):
    n=1200;y,x=np.mgrid[0:n,0:n];k=(x+y)/(2*n)
    low=np.array([15,23,39]);high=np.array(color)*0.36
    pixels=low[None,None,:]*(1-k[:,:,None])+high[None,None,:]*k[:,:,None]
    rng=np.random.default_rng(int(index));pixels+=rng.normal(0,0.65,(n,n,1))
    img=Image.fromarray(np.uint8(np.clip(pixels,0,255))).convert("RGB");d=ImageDraw.Draw(img)
    cx,cy,r=n*.57,n*.43,n*.235
    for radius in [r+38,r+60]:d.ellipse([cx-radius,cy-radius,cx+radius,cy+radius],outline=tuple(int(c*.46) for c in color),width=2)
    d.ellipse([cx-r,cy-r,cx+r,cy+r],fill=tuple(min(255,int(c*.65+88)) for c in color))
    for layer in range(3):
        coords=[(xx,n*(.69+layer*.058)+math.sin(xx/n*10+layer*2)*n*.025+math.sin(xx/n*22)*n*.012) for xx in range(0,n+1,4)]
        d.polygon(coords+[(n,n),(0,n)],fill=(25-layer*5,34-layer*5,51-layer*6))
    def branch(x,y,angle,length,depth):
        ex=x+math.cos(angle)*length;ey=y+math.sin(angle)*length
        d.line([(x,y),(ex,ey)],fill=(79,60,77),width=2+depth*3)
        if depth:
            branch(ex,ey,angle-.36,length*.73,depth-1);branch(ex,ey,angle+.58,length*.61,depth-1)
        if depth<3:
            for i in range(3):
                fx=ex+math.sin(angle*8+i*4)*25;fy=ey+math.cos(angle*4+i*3)*20;size=8+i*2
                for j in range(5):
                    a=j*math.tau/5;px=fx+math.cos(a)*size*.65;py=fy+math.sin(a)*size*.65
                    d.ellipse([px-size*.65,py-size*.65,px+size*.65,py+size*.65],fill=tuple(min(255,int(c*.6+92+i*7)) for c in color))
                d.ellipse([fx-2,fy-2,fx+2,fy+2],fill=(250,226,188))
    branch(n*1.06,n*.04,2.75,n*.31,4)
    font=ROOT/"resources/fonts/NotoSansSC-Regular.ttf"
    d.text((n*.08,n*.08),"SAKURA   /   ORIGINALS",font=ImageFont.truetype(str(font),24),fill=color)
    d.text((n*.08,n*.73),index,font=ImageFont.truetype(str(font),96),fill=color)
    d.text((n*.08,n*.86),title,font=ImageFont.truetype(str(font),47),fill=(238,229,236))
    img.resize((600,600),Image.Resampling.LANCZOS).save(folder/"cover.png")

def build_training_tracks():
    for identifier,title,color in [("tutorial_song","First Steps",(156,200,182)),("test-song","Timing Garden",(174,161,211))]:
        folder=ROOT/"resources/charts"/identifier
        info=json.loads((folder/"info.json").read_text(encoding="utf-8"))
        chart=json.loads((folder/info["difficulties"][0]["chart_file"]).read_text(encoding="utf-8"))
        notes=sorted(chart.get("keyboard_notes",[])+chart.get("mouse_notes",[]),key=lambda n:n["time"])
        duration=max(n["time"]+max(n.get("duration",0),n.get("slider_duration",0)) for n in notes)/1000+2
        audio=np.zeros((int(duration*SR),2),dtype=np.float64)
        for i,note in enumerate(notes):
            sound=tone(62+[0,2,3,7,5,3,2,0][i%8],0.60,"bell")*0.21
            start=round(note["time"]/1000*SR);end=min(len(audio),start+len(sound));pan=(i%4-1.5)*0.10
            audio[start:end,0]+=sound[:end-start]*(0.7-pan);audio[start:end,1]+=sound[:end-start]*(0.7+pan)
        beat=60/info["bpm"]
        for i in range(int(duration/(beat*4))):
            start=round(i*4*beat*SR)
            for interval in [0,3,7]:
                sound=tone(50+interval,beat*4,"pad")*0.045;end=min(len(audio),start+len(sound))
                audio[start:end,:]+=sound[:end-start,None]
        fade=min(len(audio),SR);audio[-fade:]*=np.linspace(1,0,fade)[:,None]
        with wave.open(str(folder/"music.wav"),"wb") as wav:
            wav.setnchannels(2);wav.setsampwidth(2);wav.setframerate(SR);wav.writeframes((np.tanh(audio)*32700).astype("<i2").tobytes())
        info.update(title=title,artist="Sakura Soundworks",charter="Sakura",source="Sakura Practice Collection",background_file="",tags=["original","practice","easy"])
        (folder/"info.json").write_text(json.dumps(info,ensure_ascii=False,indent=2)+"\n",encoding="utf-8")
        cover(folder,color,"00" if identifier=="tutorial_song" else "05",title)

def validate_chart(song_id: str, diff_name: str, chart: dict) -> dict:
    if chart.get("version") != 2:
        raise ValueError(f"{song_id}/{diff_name}: version != 2")
    tps = chart.get("timing_points", [])
    svs = chart.get("sv_points", [])
    kb = chart.get("keyboard_notes", [])
    ms = chart.get("mouse_notes", [])
    if not tps or any(tp["time"] < 0 or tp["bpm"] <= 0 or tp.get("time_signature") != [4, 4] for tp in tps):
        raise ValueError(f"{song_id}/{diff_name}: invalid timing_points")
    if [tp["time"] for tp in tps] != sorted(tp["time"] for tp in tps):
        raise ValueError(f"{song_id}/{diff_name}: unsorted timing_points")
    if not svs or any(sv["time"] < 0 or not (0 < sv["speed"] <= 10) for sv in svs):
        raise ValueError(f"{song_id}/{diff_name}: invalid sv_points")
    if [sv["time"] for sv in svs] != sorted(sv["time"] for sv in svs) or len({sv["time"] for sv in svs}) != len(svs):
        raise ValueError(f"{song_id}/{diff_name}: unsorted/duplicate sv_points")
    min_sv = {("cherry_blossom", "Expert"): 4, ("digital_dream", "Expert"): 3, ("sakura_storm", "Expert"): 7}.get((song_id, diff_name), 1)
    if len(svs) < min_sv:
        raise ValueError(f"{song_id}/{diff_name}: expected >= {min_sv} sv_points, got {len(svs)}")

    if [(n["time"], n["lane"]) for n in kb] != sorted((n["time"], n["lane"]) for n in kb):
        raise ValueError(f"{song_id}/{diff_name}: keyboard_notes not sorted")
    lane_end = [-10000] * 4
    for n in kb:
        l, t, d = n["lane"], n["time"], n["duration"]
        if l not in (0, 1, 2, 3) or n["type"] not in ("tap", "hold"):
            raise ValueError(f"{song_id}/{diff_name}: bad keyboard note {n}")
        if (n["type"] == "tap" and d != 0) or (n["type"] == "hold" and d <= 0):
            raise ValueError(f"{song_id}/{diff_name}: bad duration {n}")
        if t <= lane_end[l] + 60:
            raise ValueError(f"{song_id}/{diff_name}: same-lane overlap at {t} lane {l}")
        lane_end[l] = t + d

    holds = [n for n in kb if n["type"] == "hold"]
    kb_times = sorted({n["time"] for n in kb})
    chords = sum(1 for t in kb_times if sum(1 for n in kb if n["time"] == t) == 2)
    for t in kb_times:
        starting = sum(1 for n in kb if n["time"] == t)
        spanning = sum(1 for h in holds if h["time"] < t < h["time"] + h["duration"])
        if starting + spanning > 2:
            raise ValueError(f"{song_id}/{diff_name}: >2 active keys at {t}")

    if [n["time"] for n in ms] != sorted(n["time"] for n in ms) or len({n["time"] for n in ms}) != len(ms):
        raise ValueError(f"{song_id}/{diff_name}: mouse_notes not strictly sorted")
    prev_end_t = -10000
    max_jump_err = 0.0
    min_sep_1200 = 9.9
    for i, n in enumerate(ms):
        t, sdur, path = n["time"], n["slider_duration"], n["slider_path"]
        if not _in_mouse_box(n["x"], n["y"]):
            raise ValueError(f"{song_id}/{diff_name}: mouse out of bounds at {t}")
        if n["type"] == "circle" and (sdur != 0 or path != []):
            raise ValueError(f"{song_id}/{diff_name}: invalid circle at {t}")
        if n["type"] == "slider":
            if sdur <= 0 or len(path) != 2 or any(not _in_mouse_box(p[0], p[1]) for p in path):
                raise ValueError(f"{song_id}/{diff_name}: invalid 2-point slider at {t}")
        if t <= prev_end_t + 120:
            raise ValueError(f"{song_id}/{diff_name}: mouse temporal overlap at {t}")
        if i > 0:
            p = ms[i - 1]
            ax, ay = p["slider_path"][-1] if p["type"] == "slider" else (p["x"], p["y"])
            dt = t - (p["time"] + p["slider_duration"])
            err = abs(math.hypot(n["x"] - ax, n["y"] - ay) - target_mouse_jump(dt))
            max_jump_err = max(max_jump_err, err)
            if err > 0.015:
                raise ValueError(f"{song_id}/{diff_name}: jump error {err:.4f} at {t}")
        for j in range(i):
            o = ms[j]
            if t - (o["time"] + o["slider_duration"]) <= 1200:
                pts_o = [(o["x"], o["y"])] + [tuple(wp) for wp in o["slider_path"]]
                pts_n = [(n["x"], n["y"])] + [tuple(wp) for wp in n["slider_path"]]
                for pa in pts_n:
                    for pb in pts_o:
                        dist = math.hypot(pa[0] - pb[0], pa[1] - pb[1])
                        min_sep_1200 = min(min_sep_1200, dist)
                        if dist < MIN_MOUSE_SEP:
                            raise ValueError(f"{song_id}/{diff_name}: mouse sep {dist:.4f} < {MIN_MOUSE_SEP} within 1200ms ({o['time']}->{t})")
        prev_end_t = t + sdur

    sliders = sum(1 for n in ms if n["type"] == "slider")
    sync_hits = sum(1 for n in ms if n["time"] in set(kb_times))
    if len(holds) < 8 or sliders < 8 or sync_hits < 12:
        raise ValueError(f"{song_id}/{diff_name}: insufficient holds/sliders/sync ({len(holds)}/{sliders}/{sync_hits})")
    if diff_name in ("Hard", "Expert") and chords < 10:
        raise ValueError(f"{song_id}/{diff_name}: insufficient 2-key chords ({chords})")
    return dict(kb=len(kb), holds=len(holds), chords=chords, mouse=len(ms), sliders=sliders, sync=sync_hits, sv=len(svs), max_jump_err=round(max_jump_err, 4), min_sep_1200=round(min_sep_1200, 4))

def _wav_duration(wav_path: Path, bpm: float) -> float:
    if wav_path.is_file():
        with wave.open(str(wav_path), "rb") as wf:
            return wf.getnframes() / float(wf.getframerate())
    return 160 * (60.0 / bpm) + 2.5

def main(argv=None):
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--charts-only", action="store_true", help="Regenerate starter song charts and info.json while preserving existing music.wav and cover.png")
    args = parser.parse_args(argv)

    if not args.charts_only:
        build_training_tracks()

    report = []
    for song_idx, (identifier, bpm, root, progression, color, index, title) in enumerate(SONGS):
        folder = ROOT / "resources/charts" / identifier
        info_path = folder / "info.json"
        info = json.loads(info_path.read_text(encoding="utf-8"))
        if args.charts_only:
            duration = _wav_duration(folder / "music.wav", bpm)
        else:
            duration = build_audio(folder, bpm, root, progression, int(index))
            cover(folder, color, index, title)

        diff_entries = []
        for diff_name, level, chart_file in STARTER_DIFFS[identifier]:
            chart = build_chart(bpm, song_id=identifier, song_idx=song_idx, diff_name=diff_name, level=level)
            stats = validate_chart(identifier, diff_name, chart)
            (folder / chart_file).write_text(json.dumps(chart, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
            diff_entries.append(dict(
                name=diff_name,
                level=float(level),
                chart_file=chart_file,
                note_count=len(chart["keyboard_notes"]),
                hold_count=sum(1 for n in chart["keyboard_notes"] if n["type"] == "hold"),
                mouse_note_count=len(chart["mouse_notes"]),
            ))
            print(f"  [{identifier}] {diff_name} (Lv.{level}): {stats}", flush=True)

        info.update(
            version=2,
            id=identifier,
            title=title,
            artist="Sakura Soundworks",
            charter="Sakura",
            source="Sakura Originals · Vol. 01",
            tags=["original", "mixed", "electronic"],
            music_file="music.wav",
            cover_file="cover.png",
            background_file="",
            preview_time=round(32 * 60000 / bpm),
            bpm=float(bpm),
            offset=0,
            difficulties=diff_entries,
        )
        info_path.write_text(json.dumps(info, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        report.append(dict(id=identifier, seconds=round(duration, 2), sample_rate=SR, channels=2, difficulties=diff_entries))
        print(identifier, round(duration, 1), "seconds", flush=True)

    if not args.charts_only:
        album_path = ROOT / "resources/charts/album.json"
        existing = json.loads(album_path.read_text(encoding="utf-8")) if album_path.is_file() else []
        starter_ids = {s[0] for s in SONGS}
        merged = report + [e for e in existing if e.get("id") not in starter_ids]
        album_path.write_text(json.dumps(merged, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")

if __name__ == "__main__":
    main()
