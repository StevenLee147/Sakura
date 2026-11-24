import React from "react";
import { AbsoluteFill, Easing, Img, interpolate, staticFile, useCurrentFrame } from "remotion";
import { AudioSpectrumBar } from "../components/AudioVisualizer";
import { Fade } from "../components/Effects";
import { Footage } from "../components/Footage";
import { NightSky } from "../components/NightSky";
import { Petals } from "../components/Petals";
import { SHOTS } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.16, 1, 0.3, 1);

/** Last bars of the outro: v1.0.0 official release emblem, logo, platform, download link & credits. */
export const EndCard: React.FC = () => {
  const frame = useCurrentFrame();
  const appear = (start: number) => interpolate(frame, [start, start + 30], [0, 1], { ...clamp, easing: ease });
  return (
    <AbsoluteFill style={{ fontFamily: FONT_FAMILY }}>
      <NightSky moonX={0.5} moonY={0.28} moonSize={780} />
      <AbsoluteFill style={{ opacity: 0.18, mixBlendMode: "screen" }}>
        <Footage src={SHOTS.bga.file} trimBefore={120} duration={210} zoomFrom={1.04} zoomTo={1.1} />
      </AbsoluteFill>
      <Petals count={40} seed="end" speed={0.62} minSize={28} maxSize={88} opacity={0.82} />
      <AbsoluteFill style={{ background: "radial-gradient(ellipse at 50% 60%, rgba(8,7,18,0.64) 0%, rgba(8,7,18,0.12) 64%)" }} />
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 765,
          display: "flex",
          justifyContent: "center",
          opacity: appear(0),
        }}
      >
        <div
          style={{
            fontSize: 48,
            fontWeight: 700,
            color: COLORS.gold,
            letterSpacing: "0.36em",
            padding: "12px 38px 12px 54px",
            border: "2px solid rgba(244, 213, 138, 0.56)",
            backgroundColor: "rgba(12, 11, 24, 0.68)",
          }}
        >
          OFFICIAL RELEASE · v1.0.0 正式版现已发布
        </div>
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 920,
          display: "flex",
          justifyContent: "center",
          alignItems: "center",
          gap: 68,
          opacity: appear(6),
          translate: `0px ${(1 - appear(6)) * 46}px`,
        }}
      >
        <Img src={staticFile("img/app_icon.png")} style={{ width: 256, height: 256, borderRadius: 56, boxShadow: "0 24px 80px rgba(0,0,0,0.58)" }} />
        <div style={{ fontSize: 296, fontWeight: 700, color: COLORS.text, letterSpacing: "0.04em", lineHeight: 1 }}>Sakura</div>
        <div style={{ fontSize: 214, fontWeight: 700, color: COLORS.sakura, lineHeight: 1 }}>樱</div>
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 1290,
          textAlign: "center",
          fontSize: 92,
          color: COLORS.sakuraLight,
          letterSpacing: "0.38em",
          paddingLeft: "0.38em",
          opacity: appear(16),
        }}
      >
        左手节奏 · 右手旋律 · 双域成诗
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 1475,
          textAlign: "center",
          fontSize: 76,
          color: COLORS.text,
          letterSpacing: "0.11em",
          opacity: appear(28),
        }}
      >
        Windows 10 / 11 免费下载
        <span style={{ color: COLORS.gold, marginLeft: 56 }}>github.com/StevenLee147/Sakura</span>
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 1650,
          display: "flex",
          justifyContent: "center",
          opacity: appear(34) * 0.78,
        }}
      >
        <AudioSpectrumBar at={3560} width={680} height={54} mirror />
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          bottom: 118,
          textAlign: "center",
          fontSize: 48,
          color: COLORS.dim,
          letterSpacing: "0.12em",
          opacity: appear(42) * 0.92,
        }}
      >
        v1.0.0 正式版 · 全片 4K60 原生实机录制 · C++20 & SDL3 独立开源引擎 · BGM《樱落呼吸》
      </div>
      <Fade from={172} to={210} />
    </AbsoluteFill>
  );
};
