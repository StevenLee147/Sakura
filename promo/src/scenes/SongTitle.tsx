import React from "react";
import { AbsoluteFill, Easing, Img, interpolate, staticFile, useCurrentFrame } from "remotion";
import { COVERS } from "../audio";
import { AudioSpectrumBar } from "../components/AudioVisualizer";
import { FootageTag } from "../components/Captions";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.16, 1, 0.3, 1);

/** Beats 64-76 (drop lead-in): featured song card + live 24-band spectrum + 13-song v1.0.0 library strip. */
export const SongTitle: React.FC<{ at: number }> = ({ at }) => {
  const frame = useCurrentFrame();
  const appear = interpolate(frame, [4, 34], [0, 1], { ...clamp, easing: ease });
  const stripIn = interpolate(frame, [26, 62], [0, 1], { ...clamp, easing: ease });
  const leave = interpolate(frame, [222, 242], [1, 0], { ...clamp, easing: Easing.in(Easing.cubic) });
  return (
    <AbsoluteFill>
      <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at)} duration={243} zoomFrom={1.0} zoomTo={1.038} />
      <AbsoluteFill
        style={{
          background: "linear-gradient(90deg, rgba(8,7,18,0.92) 0%, rgba(8,7,18,0.76) 44%, rgba(8,7,18,0.08) 76%)",
          opacity: leave * appear,
        }}
      />
      <div
        style={{
          position: "absolute",
          left: 210,
          top: 470,
          display: "flex",
          alignItems: "center",
          gap: 100,
          fontFamily: FONT_FAMILY,
          opacity: leave,
          translate: `${(1 - appear) * -140}px 0px`,
        }}
      >
        <Img
          src={staticFile("img/sakura_breath_cover.png")}
          style={{
            width: 720,
            height: 720,
            borderRadius: 32,
            boxShadow: "0 40px 140px rgba(0,0,0,0.65), 0 0 0 2px rgba(244,213,138,0.45)",
            opacity: appear,
            scale: interpolate(frame, [4, 243], [1.04, 1.0], clamp),
          }}
        />
        <div>
          <div style={{ fontSize: 64, fontWeight: 700, color: COLORS.gold, letterSpacing: "0.32em", opacity: appear }}>
            主打曲目 · FEATURED TRACK
          </div>
          <div
            style={{
              marginTop: 26,
              fontSize: 316,
              fontWeight: 700,
              color: COLORS.text,
              letterSpacing: "0.08em",
              lineHeight: 1.08,
              clipPath: `inset(0 ${(1 - appear) * 100}% 0 0)`,
              textShadow: "0 16px 70px rgba(6,5,14,0.85)",
            }}
          >
            樱落呼吸
          </div>
          <div style={{ marginTop: 32, fontSize: 72, color: COLORS.dim, letterSpacing: "0.14em", opacity: appear }}>
            EXPERT · 178 BPM · 原生动态视频背景
          </div>
          <div style={{ marginTop: 44, opacity: appear }}>
            <AudioSpectrumBar at={at} width={780} height={82} />
          </div>
        </div>
      </div>
      <div
        style={{
          position: "absolute",
          left: 210,
          bottom: 130,
          display: "flex",
          alignItems: "center",
          gap: 32,
          fontFamily: FONT_FAMILY,
          opacity: stripIn * leave,
          translate: `0px ${(1 - stripIn) * 48}px`,
        }}
      >
        <div style={{ display: "flex", gap: 18 }}>
          {COVERS.slice(0, 8).map((c) => (
            <Img
              key={c.id}
              src={staticFile(c.cover)}
              style={{
                width: 118,
                height: 118,
                borderRadius: 16,
                boxShadow: "0 14px 40px rgba(0,0,0,0.55), 0 0 0 2px rgba(241,167,190,0.32)",
              }}
            />
          ))}
        </div>
        <div style={{ marginLeft: 18 }}>
          <div style={{ fontSize: 52, fontWeight: 700, color: COLORS.sakuraLight, letterSpacing: "0.14em" }}>
            全 13 首原创曲目收录
          </div>
          <div style={{ marginTop: 10, fontSize: 42, color: COLORS.dim, letterSpacing: "0.12em" }}>
            NORMAL – EXPERT Lv.14.5 · 支持自制谱包导入
          </div>
        </div>
      </div>
      <FootageTag />
    </AbsoluteFill>
  );
};
