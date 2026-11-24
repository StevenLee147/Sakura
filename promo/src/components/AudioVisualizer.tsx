import React from "react";
import { AbsoluteFill, useCurrentFrame } from "remotion";
import { sampleSpectrum } from "../audio";
import { COLORS } from "../theme";

type SpectrumProps = {
  at: number;
  width?: number;
  height?: number;
  barWidth?: number;
  gap?: number;
  opacity?: number;
  mirror?: boolean;
};

/** Deterministic 60fps 24-band FFT spectrum ribbon driven by soundtrack.wav. */
export const AudioSpectrumBar: React.FC<SpectrumProps> = ({
  at,
  width = 620,
  height = 84,
  barWidth = 14,
  gap = 11,
  opacity = 0.9,
  mirror = false,
}) => {
  const frame = useCurrentFrame();
  const { bands, bass } = sampleSpectrum(at + frame);
  const count = bands.length;
  const totalW = count * barWidth + (count - 1) * gap;

  return (
    <svg width={width} height={height} viewBox={`0 0 ${totalW} ${height}`} style={{ opacity, overflow: "visible" }}>
      <defs>
        <linearGradient id={`spec-grad-${at}`} x1="0%" y1="100%" x2="0%" y2="0%">
          <stop offset="0%" stopColor={COLORS.sakura} />
          <stop offset="62%" stopColor={COLORS.lavender} />
          <stop offset="100%" stopColor={COLORS.goldBright} />
        </linearGradient>
      </defs>
      {bands.map((val, i) => {
        const v = Math.min(1, Math.max(0.08, val * (0.88 + bass * 0.24)));
        const h = Math.max(6, v * (height - 6));
        const x = i * (barWidth + gap);
        const y = mirror ? (height - h) / 2 : height - h;
        return (
          <rect
            key={i}
            x={x}
            y={y}
            width={barWidth}
            height={h}
            rx={barWidth / 2}
            fill={`url(#spec-grad-${at})`}
            opacity={0.48 + v * 0.52}
          />
        );
      })}
    </svg>
  );
};

/** Subtle radial & edge glow reactive to sub-bass kicks (45-165 Hz). */
export const BassHalo: React.FC<{ at: number; strength?: number }> = ({ at, strength = 0.34 }) => {
  const frame = useCurrentFrame();
  const { bass } = sampleSpectrum(at + frame);
  const alpha = Math.min(0.65, bass * strength);
  return (
    <AbsoluteFill
      style={{
        pointerEvents: "none",
        mixBlendMode: "screen",
        background: `radial-gradient(ellipse at 50% 52%, rgba(241,167,190,${(alpha * 0.42).toFixed(3)}) 0%, rgba(124,232,255,${(alpha * 0.16).toFixed(3)}) 45%, rgba(244,213,138,${(alpha * 0.28).toFixed(3)}) 100%)`,
      }}
    />
  );
};
