import React from "react";
import { AbsoluteFill } from "remotion";
import { Footage } from "./components/Footage";
import { Petals } from "./components/Petals";
import { SHOTS } from "./footage";
import { COLORS, FONT_FAMILY } from "./theme";

/** Bilibili cover (16:10). Background: a dense Expert moment from the real recording (music time ≈ 33 s). */
export const Cover: React.FC = () => (
  <AbsoluteFill style={{ backgroundColor: COLORS.night, fontFamily: FONT_FAMILY }}>
    <Footage src={SHOTS.playExpert.file} trimBefore={SHOTS.playExpert.musicZeroFrame + 33 * 60} duration={1} zoomFrom={1.08} centerX={0.6} />
    <AbsoluteFill
      style={{ background: "linear-gradient(90deg, rgba(10,9,22,0.94) 0%, rgba(10,9,22,0.78) 38%, rgba(10,9,22,0.1) 72%)" }}
    />
    <Petals count={14} seed="cover" minSize={18} maxSize={46} opacity={0.85} />
    <div style={{ position: "absolute", left: 110, top: 236 }}>
      <div
        style={{
          display: "inline-block",
          fontSize: 32,
          fontWeight: 700,
          color: COLORS.gold,
          letterSpacing: "0.28em",
          padding: "10px 26px",
          border: "2px solid rgba(244, 213, 138, 0.58)",
          backgroundColor: "rgba(12, 11, 24, 0.72)",
          marginBottom: 30,
        }}
      >
        OFFICIAL RELEASE · v1.0.0 正式版
      </div>
      <div style={{ display: "flex", alignItems: "baseline", gap: 34 }}>
        <div style={{ fontSize: 204, fontWeight: 700, color: COLORS.text, lineHeight: 1, letterSpacing: "0.02em" }}>Sakura</div>
        <div style={{ fontSize: 154, fontWeight: 700, color: COLORS.sakura, lineHeight: 1 }}>樱</div>
      </div>
      <div style={{ marginTop: 42, fontSize: 70, fontWeight: 700, color: COLORS.text, letterSpacing: "0.08em" }}>左手五轨节奏 · 右手空间旋律</div>
      <div style={{ marginTop: 24, fontSize: 50, color: COLORS.sakuraLight, letterSpacing: "0.1em" }}>《樱落呼吸》4K60 正式版宣传片 · 双域协同音游</div>
      <div
        style={{
          display: "inline-block",
          marginTop: 52,
          fontSize: 38,
          fontWeight: 700,
          color: COLORS.text,
          letterSpacing: "0.16em",
          padding: "14px 36px",
          background: "rgba(22, 20, 42, 0.78)",
          border: "2px solid rgba(241, 167, 190, 0.62)",
        }}
      >
        Windows 免费游玩 · C++20 开源独立音游
      </div>
    </div>
  </AbsoluteFill>
);
