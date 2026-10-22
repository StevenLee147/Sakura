import React from "react";
import { AbsoluteFill, random, useCurrentFrame, useVideoConfig } from "remotion";

// Five-lobed sakura petal with the characteristic notch at the outer edge (100 x 100 units, base at bottom).
const PETAL_PATH =
  "M 0 50 C -15 36 -42 4 -35 -26 C -31 -44 -15 -52 -5 -44 L 0 -35 L 5 -44 C 15 -52 31 -44 35 -26 C 42 4 15 36 0 50 Z";

type Props = {
  count?: number;
  seed?: string;
  /** Multiplier for fall speed. */
  speed?: number;
  /** Petal size range in px at 4K. */
  minSize?: number;
  maxSize?: number;
  blur?: number;
  opacity?: number;
};

/** Deterministic falling petals: every position is a pure function of the frame. */
export const Petals: React.FC<Props> = ({
  count = 40,
  seed = "petals",
  speed = 1,
  minSize = 34,
  maxSize = 96,
  blur = 0,
  opacity = 1,
}) => {
  const frame = useCurrentFrame();
  const { width, height, fps } = useVideoConfig();
  const t = frame / fps;
  const margin = 260;
  const span = height + margin * 2;

  return (
    <AbsoluteFill style={{ pointerEvents: "none", opacity, filter: blur ? `blur(${blur}px)` : undefined }}>
      <svg width={width} height={height} viewBox={`0 0 ${width} ${height}`}>
        <defs>
          <radialGradient id={`${seed}-fill`} cx="50%" cy="65%" r="65%">
            <stop offset="0%" stopColor="#fff1f5" />
            <stop offset="55%" stopColor="#f7c3d2" />
            <stop offset="100%" stopColor="#e58aa8" />
          </radialGradient>
        </defs>
        {new Array(count).fill(0).map((_, i) => {
          const r = (key: string) => random(`${seed}-${i}-${key}`);
          const size = minSize + r("size") * (maxSize - minSize);
          const fall = (110 + r("fall") * 170) * speed;
          const y = ((r("y0") * span + t * fall) % span) - margin;
          const wind = (30 + r("wind") * 50) * speed;
          const sway = Math.sin(t * (0.5 + r("swayF") * 0.9) * Math.PI + r("swayP") * 6.283) * (50 + r("swayA") * 130);
          const x = ((((r("x0") * (width + margin * 2) - t * wind + sway) % (width + margin * 2)) + width + margin * 2) % (width + margin * 2)) - margin;
          const spin = (r("spin") - 0.5) * 160;
          const angle = r("a0") * 360 + t * spin;
          const flutter = 0.3 + 0.7 * Math.abs(Math.cos(t * (1.1 + r("flF") * 1.8) + r("flP") * 6.283));
          return (
            <path
              key={i}
              d={PETAL_PATH}
              fill={`url(#${seed}-fill)`}
              opacity={0.55 + r("op") * 0.4}
              transform={`translate(${x.toFixed(1)} ${y.toFixed(1)}) rotate(${angle.toFixed(1)}) scale(${(size / 100).toFixed(3)} ${((size / 100) * flutter).toFixed(3)})`}
            />
          );
        })}
      </svg>
    </AbsoluteFill>
  );
};
