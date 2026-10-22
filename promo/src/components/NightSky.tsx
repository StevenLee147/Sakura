import React from "react";
import { AbsoluteFill, random, useCurrentFrame } from "remotion";
import { COLORS } from "../theme";

/** Night sky with a soft moon glow and faint twinkling stars (echoes the game's menu backdrop). */
export const NightSky: React.FC<{ moonX?: number; moonY?: number; moonSize?: number }> = ({
  moonX = 0.5,
  moonY = 0.44,
  moonSize = 1100,
}) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill
      style={{
        background: `linear-gradient(180deg, ${COLORS.night} 0%, ${COLORS.nightMid} 55%, ${COLORS.nightHigh} 100%)`,
      }}
    >
      <AbsoluteFill
        style={{
          background: `radial-gradient(circle at ${moonX * 100}% ${moonY * 100}%, rgba(247, 205, 214, 0.22) 0%, rgba(171, 150, 232, 0.10) 28%, rgba(12, 11, 24, 0) 58%)`,
        }}
      />
      <div
        style={{
          position: "absolute",
          left: `calc(${moonX * 100}% - ${moonSize / 2}px)`,
          top: `calc(${moonY * 100}% - ${moonSize / 2}px)`,
          width: moonSize,
          height: moonSize,
          borderRadius: "50%",
          background: "radial-gradient(circle at 42% 38%, #fbe3e6 0%, #f2c9d1 46%, #dca7b6 100%)",
          boxShadow: "0 0 180px 40px rgba(244, 196, 210, 0.28), 0 0 520px 160px rgba(173, 150, 232, 0.12)",
          opacity: 0.92,
        }}
      />
      <svg width="100%" height="100%" style={{ position: "absolute", inset: 0 }}>
        {new Array(90).fill(0).map((_, i) => {
          const x = random(`star-x-${i}`) * 100;
          const y = random(`star-y-${i}`) * 70;
          const twinkle = 0.25 + 0.75 * Math.abs(Math.sin(frame / 45 + random(`star-p-${i}`) * 6.283));
          return <circle key={i} cx={`${x}%`} cy={`${y}%`} r={2 + random(`star-r-${i}`) * 3.5} fill="#fdf1f4" opacity={0.15 + 0.55 * twinkle} />;
        })}
      </svg>
    </AbsoluteFill>
  );
};
