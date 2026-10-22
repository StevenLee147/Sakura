import React from "react";
import { Easing, interpolate, useCurrentFrame } from "remotion";
import { COLORS, FONT_FAMILY } from "../theme";

const ease = Easing.bezier(0.16, 1, 0.3, 1);
const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;

type HeadlineProps = {
  zh: string;
  en: string;
  detail?: string;
  /** Anchor position in px (4K canvas). */
  x: number;
  y: number;
  align?: "left" | "right" | "center";
  zhSize?: number;
  /** Local frame where the headline starts to appear / disappear. */
  enter?: number;
  exit: number;
  accent?: string;
};

/** Large Chinese headline with a letter-spaced English line and an optional detail line. */
export const Headline: React.FC<HeadlineProps> = ({
  zh,
  en,
  detail,
  x,
  y,
  align = "left",
  zhSize = 280,
  enter = 0,
  exit,
  accent = COLORS.sakura,
}) => {
  const frame = useCurrentFrame();
  const appear = interpolate(frame, [enter, enter + 26], [0, 1], { ...clamp, easing: ease });
  const leave = interpolate(frame, [exit - 18, exit], [1, 0], { ...clamp, easing: Easing.in(Easing.cubic) });
  const lineIn = interpolate(frame, [enter + 8, enter + 40], [0, 1], { ...clamp, easing: ease });
  const detailIn = interpolate(frame, [enter + 16, enter + 44], [0, 1], { ...clamp, easing: ease });
  const shift = (1 - appear) * 70;
  const horizontal = align === "right" ? { right: 3840 - x } : align === "center" ? { left: x - 1600, width: 3200 } : { left: x };
  return (
    <div
      style={{
        position: "absolute",
        top: y,
        ...horizontal,
        textAlign: align,
        fontFamily: FONT_FAMILY,
        opacity: leave,
        translate: `0px ${shift}px`,
      }}
    >
      <div
        style={{
          fontSize: zhSize,
          fontWeight: 700,
          color: COLORS.text,
          letterSpacing: "0.08em",
          lineHeight: 1.05,
          opacity: appear,
          clipPath: `inset(0 0 ${(1 - appear) * 100}% 0)`,
          textShadow: "0 12px 60px rgba(6, 5, 14, 0.75)",
        }}
      >
        {zh}
      </div>
      <div
        style={{
          display: "inline-block",
          marginTop: 34,
          height: 8,
          width: 520 * lineIn,
          background: `linear-gradient(90deg, ${accent}, rgba(241, 167, 190, 0))`,
          ...(align === "right" ? { background: `linear-gradient(270deg, ${accent}, rgba(241, 167, 190, 0))` } : null),
        }}
      />
      <div
        style={{
          marginTop: 30,
          fontSize: Math.round(zhSize * 0.27),
          fontWeight: 400,
          color: accent,
          letterSpacing: "0.42em",
          opacity: lineIn,
          textShadow: "0 6px 30px rgba(6, 5, 14, 0.8)",
        }}
      >
        {en}
      </div>
      {detail ? (
        <div
          style={{
            marginTop: 26,
            fontSize: Math.round(zhSize * 0.3),
            fontWeight: 400,
            color: COLORS.dim,
            letterSpacing: "0.12em",
            opacity: detailIn,
            textShadow: "0 6px 30px rgba(6, 5, 14, 0.85)",
          }}
        >
          {detail}
        </div>
      ) : null}
    </div>
  );
};

/** Persistent label on gameplay shots: honest about what the viewer is seeing. */
export const FootageTag: React.FC<{ text?: string }> = ({ text = "实机录制 · 自动演示   IN-GAME · AUTOPLAY" }) => (
  <div
    style={{
      position: "absolute",
      right: 150,
      bottom: 120,
      fontFamily: FONT_FAMILY,
      fontSize: 52,
      letterSpacing: "0.2em",
      color: "rgba(247, 239, 241, 0.72)",
      padding: "16px 34px",
      borderRadius: 999,
      border: "2px solid rgba(241, 167, 190, 0.45)",
      background: "rgba(10, 9, 22, 0.45)",
    }}
  >
    {text}
  </div>
);
