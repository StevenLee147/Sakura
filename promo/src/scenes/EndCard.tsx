import React from "react";
import { AbsoluteFill, Easing, Img, interpolate, staticFile, useCurrentFrame } from "remotion";
import { Fade } from "../components/Effects";
import { NightSky } from "../components/NightSky";
import { Petals } from "../components/Petals";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.16, 1, 0.3, 1);

/** Last bars of the outro: logo, platform, where to get it, honest credits. */
export const EndCard: React.FC = () => {
  const frame = useCurrentFrame();
  const appear = (start: number) => interpolate(frame, [start, start + 30], [0, 1], { ...clamp, easing: ease });
  return (
    <AbsoluteFill style={{ fontFamily: FONT_FAMILY }}>
      <NightSky moonX={0.5} moonY={0.3} moonSize={760} />
      <Petals count={36} seed="end" speed={0.6} minSize={28} maxSize={86} opacity={0.8} />
      <AbsoluteFill style={{ background: "radial-gradient(ellipse at 50% 62%, rgba(8,7,18,0.55) 0%, rgba(8,7,18,0) 60%)" }} />
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 1040,
          display: "flex",
          justifyContent: "center",
          alignItems: "center",
          gap: 70,
          opacity: appear(0),
          translate: `0px ${(1 - appear(0)) * 50}px`,
        }}
      >
        <Img src={staticFile("img/app_icon.png")} style={{ width: 260, height: 260, borderRadius: 56, boxShadow: "0 24px 80px rgba(0,0,0,0.5)" }} />
        <div style={{ fontSize: 300, fontWeight: 400, color: COLORS.text, letterSpacing: "0.04em", lineHeight: 1 }}>Sakura</div>
        <div style={{ fontSize: 210, fontWeight: 700, color: COLORS.sakura, lineHeight: 1 }}>樱</div>
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 1400,
          textAlign: "center",
          fontSize: 96,
          color: COLORS.sakuraLight,
          letterSpacing: "0.4em",
          opacity: appear(14),
        }}
      >
        左手节奏 · 右手旋律
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          top: 1600,
          textAlign: "center",
          fontSize: 80,
          color: COLORS.text,
          letterSpacing: "0.12em",
          opacity: appear(26),
        }}
      >
        Windows 10 / 11 · 免费下载
        <span style={{ color: COLORS.sakura, marginLeft: 60 }}>github.com/StevenLee147/Sakura</span>
      </div>
      <div
        style={{
          position: "absolute",
          left: 0,
          right: 0,
          bottom: 130,
          textAlign: "center",
          fontSize: 52,
          color: COLORS.dim,
          letterSpacing: "0.12em",
          opacity: appear(40) * 0.9,
        }}
      >
        v0.6.1-alpha · 画面为开发中版本实机录制 · BGM《樱落呼吸》由 Suno AI 生成
      </div>
      <Fade from={170} to={210} />
    </AbsoluteFill>
  );
};
