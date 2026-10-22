import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { COLORS, FONT_FAMILY } from "../theme";
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
};

/** Feature card: the real screen in a framed window, its blurred copy as backdrop, caption in a clean column. */
export const Showcase: React.FC<Props> = ({ src, trimBefore, duration, index, zh, en, detail }) => {
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
          background: "linear-gradient(90deg, rgba(10,9,22,0.94) 0%, rgba(10,9,22,0.72) 27%, rgba(10,9,22,0.3) 100%)",
        }}
      />
      <div
        style={{
          position: "absolute",
          left: 1040,
          top: 338,
          width: 2640,
          height: 1485,
          borderRadius: 30,
          overflow: "hidden",
          boxShadow: "0 50px 160px rgba(0,0,0,0.65), 0 0 0 3px rgba(241,167,190,0.3)",
          opacity: appear,
          translate: `${(1 - appear) * 180}px 0px`,
        }}
      >
        <Footage src={src} trimBefore={trimBefore} duration={duration} zoomFrom={1.0} zoomTo={1.035} />
      </div>
      <div
        style={{
          position: "absolute",
          left: 200,
          top: 640,
          width: 780,
          opacity: captionIn * leave,
          translate: `${(1 - captionIn) * -110}px 0px`,
        }}
      >
        <div style={{ fontSize: 64, color: COLORS.sakura, letterSpacing: "0.3em" }}>{index}</div>
        <div style={{ marginTop: 26, fontSize: 168, fontWeight: 700, color: COLORS.text, letterSpacing: "0.06em", lineHeight: 1.12, whiteSpace: "nowrap" }}>{zh}</div>
        <div
          style={{
            marginTop: 40,
            height: 7,
            width: 340 * captionIn,
            background: `linear-gradient(90deg, ${COLORS.sakura}, rgba(241,167,190,0))`,
          }}
        />
        <div style={{ marginTop: 38, fontSize: 60, color: COLORS.sakura, letterSpacing: "0.34em" }}>{en}</div>
        <div style={{ marginTop: 44, fontSize: 64, color: COLORS.dim, letterSpacing: "0.08em", lineHeight: 1.55, whiteSpace: "pre-line" }}>{detail}</div>
      </div>
    </AbsoluteFill>
  );
};
