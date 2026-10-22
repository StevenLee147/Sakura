import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";

/** Beat flash: peaks on the first frame, then decays. Place inside a short <Sequence>. */
export const Flash: React.FC<{ color?: string; peak?: number; length?: number }> = ({
  color = "#fff3f7",
  peak = 0.8,
  length = 14,
}) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill
      style={{
        backgroundColor: color,
        mixBlendMode: "screen",
        opacity: interpolate(frame, [0, 1, length], [peak, peak, 0], {
          extrapolateLeft: "clamp",
          extrapolateRight: "clamp",
          easing: Easing.out(Easing.quad),
        }),
      }}
    />
  );
};

/** Soft vignette that keeps the eye in the centre of the frame. */
export const Vignette: React.FC<{ strength?: number }> = ({ strength = 0.55 }) => (
  <AbsoluteFill
    style={{
      pointerEvents: "none",
      background: `radial-gradient(ellipse at 50% 50%, rgba(0,0,0,0) 55%, rgba(5,4,12,${strength}) 100%)`,
    }}
  />
);

/** Fade to/from black over the given local frame range. */
export const Fade: React.FC<{ from: number; to: number; inverse?: boolean }> = ({ from, to, inverse }) => {
  const frame = useCurrentFrame();
  const value = interpolate(frame, [from, to], inverse ? [1, 0] : [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing: Easing.inOut(Easing.cubic),
  });
  return <AbsoluteFill style={{ backgroundColor: "#000", opacity: value, pointerEvents: "none" }} />;
};
