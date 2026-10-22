import { Video } from "@remotion/media";
import React from "react";
import { AbsoluteFill, Easing, interpolate, staticFile, useCurrentFrame } from "remotion";

type Props = {
  src: string;
  trimBefore: number;
  /** Length of the clip in frames (drives the camera move). */
  duration: number;
  zoomFrom?: number;
  zoomTo?: number;
  /** Desired centre of the view in source coordinates (0..1); clamped so the view never leaves the frame. */
  centerX?: number;
  centerY?: number;
  centerXTo?: number;
  centerYTo?: number;
  dimFrom?: number;
  dimTo?: number;
  blur?: number;
  /** Optional perspective tilt in degrees (rotateY), animated from -> to. */
  tiltFrom?: number;
  tiltTo?: number;
  easing?: (t: number) => number;
};

const viewOrigin = (center: number, zoom: number): number => {
  if (zoom <= 1.0001) return 0.5;
  const span = 1 / zoom;
  const start = Math.min(Math.max(center - span / 2, 0), 1 - span);
  // transform-origin o keeps source point o fixed, so the visible window starts at o - o/zoom.
  return start / (1 - span);
};

/** A recorded 4K60 game clip with a virtual camera (push-in, punch-in, pan, tilt). */
export const Footage: React.FC<Props> = ({
  src,
  trimBefore,
  duration,
  zoomFrom = 1,
  zoomTo,
  centerX = 0.5,
  centerY = 0.5,
  centerXTo,
  centerYTo,
  dimFrom = 0,
  dimTo,
  blur = 0,
  tiltFrom = 0,
  tiltTo,
  easing = Easing.bezier(0.22, 1, 0.36, 1),
}) => {
  const frame = useCurrentFrame();
  const p = interpolate(frame, [0, Math.max(1, duration - 1)], [0, 1], {
    extrapolateLeft: "clamp",
    extrapolateRight: "clamp",
    easing,
  });
  const mix = (a: number, b: number | undefined) => a + ((b ?? a) - a) * p;
  const zoom = mix(zoomFrom, zoomTo);
  const ox = viewOrigin(mix(centerX, centerXTo), zoom);
  const oy = viewOrigin(mix(centerY, centerYTo), zoom);
  const tilt = mix(tiltFrom, tiltTo);
  const dim = mix(dimFrom, dimTo);
  return (
    <AbsoluteFill style={{ overflow: "hidden", backgroundColor: "#07060e" }}>
      <AbsoluteFill
        style={{
          transform: tilt ? `perspective(5200px) rotateY(${tilt}deg) scale(${zoom + Math.abs(tilt) * 0.012})` : `scale(${zoom})`,
          transformOrigin: `${ox * 100}% ${oy * 100}%`,
          filter: blur ? `blur(${blur}px)` : undefined,
        }}
      >
        <Video src={staticFile(src)} trimBefore={trimBefore} muted objectFit="cover" style={{ width: "100%", height: "100%" }} />
      </AbsoluteFill>
      {dim > 0 ? <AbsoluteFill style={{ backgroundColor: `rgba(7, 6, 16, ${dim})` }} /> : null}
    </AbsoluteFill>
  );
};
