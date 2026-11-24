import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { COLORS, FONT_FAMILY } from "../theme";
import { AudioSpectrumBar } from "./AudioVisualizer";
import { Footage } from "./Footage";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.16, 1, 0.3, 1);

type Props = {
  src: string;
  trimBefore: number;
  duration: number;
  index: string;
  zh: string;
  en: string;
  detail: string;
  at?: number;
  badge?: string;
};

/** Feature card: the real 4K60 screen in a gilded-hairline stage frame with editorial typography & spectrum. */
export const Showcase: React.FC<Props> = ({ src, trimBefore, duration, index, zh, en, detail, at, badge }) => {
  const frame = useCurrentFrame();
  const appear = interpolate(frame, [0, 24], [0, 1], { ...clamp, easing: ease });
  const captionIn = interpolate(frame, [6, 32], [0, 1], { ...clamp, easing: ease });
  const leave = interpolate(frame, [duration - 8, duration], [1, 0.0], clamp);
  return (
    <AbsoluteFill style={{ backgroundColor: COLORS.night, fontFamily: FONT_FAMILY }}>
      <AbsoluteFill style={{ opacity: 0.55 }}>
        <Footage src={src} trimBefore={trimBefore} duration={duration} zoomFrom={1.18} zoomTo={1.24} blur={34} dimFrom={0.3} />
      </AbsoluteFill>
      <AbsoluteFill
        style={{
          background: "linear-gradient(90deg, rgba(10,9,22,0.95) 0%, rgba(10,9,22,0.74) 28%, rgba(10,9,22,0.28) 100%)",
        }}
      />
      <div
        style={{
          position: "absolute",
          left: 1040,
          top: 338,
          width: 2640,
          height: 1485,
          borderRadius: 28,
          overflow: "hidden",
          boxShadow: "0 50px 160px rgba(0,0,0,0.68), 0 0 0 2px rgba(244,213,138,0.38)",
          opacity: appear,
          translate: `${(1 - appear) * 180}px 0px`,
        }}
      >
        <Footage src={src} trimBefore={trimBefore} duration={duration} zoomFrom={1.0} zoomTo={1.035} />
      </div>
      <div
        style={{
          position: "absolute",
          left: 190,
          top: 560,
          width: 790,
          opacity: captionIn * leave,
          translate: `${(1 - captionIn) * -110}px 0px`,
        }}
      >
        <div style={{ display: "flex", alignItems: "center", gap: 28 }}>
          <div style={{ fontSize: 60, color: COLORS.sakura, letterSpacing: "0.28em" }}>{index}</div>
          {badge ? (
            <div
              style={{
                fontSize: 38,
                fontWeight: 700,
                color: COLORS.gold,
                letterSpacing: "0.2em",
                padding: "8px 24px",
                border: "2px solid rgba(244, 213, 138, 0.55)",
                backgroundColor: "rgba(22, 20, 42, 0.72)",
              }}
            >
              {badge}
            </div>
          ) : null}
        </div>
        <div style={{ marginTop: 26, fontSize: 164, fontWeight: 700, color: COLORS.text, letterSpacing: "0.05em", lineHeight: 1.12, whiteSpace: "nowrap" }}>{zh}</div>
        <div
          style={{
            marginTop: 38,
            height: 6,
            width: 360 * captionIn,
            background: `linear-gradient(90deg, ${COLORS.gold}, ${COLORS.sakura}, rgba(241,167,190,0))`,
          }}
        />
        <div style={{ marginTop: 34, fontSize: 56, color: COLORS.sakura, letterSpacing: "0.3em" }}>{en}</div>
        <div style={{ marginTop: 40, fontSize: 62, color: COLORS.dim, letterSpacing: "0.07em", lineHeight: 1.56, whiteSpace: "pre-line" }}>{detail}</div>
        {at !== undefined ? (
          <div style={{ marginTop: 58 }}>
            <AudioSpectrumBar at={at} width={520} height={66} opacity={0.78} />
          </div>
        ) : null}
      </div>
    </AbsoluteFill>
  );
};
