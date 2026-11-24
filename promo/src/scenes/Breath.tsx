import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;

/** Beats 76-80: the near-silent bar before the drop. Contracting gilded ring + camera lean-in. */
export const Breath: React.FC<{ at: number }> = ({ at }) => {
  const frame = useCurrentFrame();
  const ringSize = interpolate(frame, [0, 80], [1540, 860], { ...clamp, easing: Easing.in(Easing.cubic) });
  const ringAlpha = interpolate(frame, [4, 28, 68, 80], [0, 0.65, 0.8, 0], clamp);
  return (
    <AbsoluteFill>
      <Footage
        src={SHOTS.playExpert.file}
        trimBefore={syncTrim(SHOTS.playExpert, at)}
        duration={81}
        zoomFrom={1.035}
        zoomTo={1.17}
        dimFrom={0.12}
        dimTo={0.68}
        easing={Easing.in(Easing.cubic)}
      />
      <AbsoluteFill style={{ justifyContent: "center", alignItems: "center", fontFamily: FONT_FAMILY }}>
        <div
          style={{
            position: "absolute",
            width: ringSize,
            height: ringSize,
            borderRadius: "50%",
            border: `3px solid rgba(244, 213, 138, ${ringAlpha.toFixed(3)})`,
            boxShadow: `0 0 90px rgba(241, 167, 190, ${(ringAlpha * 0.45).toFixed(3)})`,
          }}
        />
        <div
          style={{
            fontSize: 420,
            fontWeight: 700,
            color: COLORS.sakuraLight,
            letterSpacing: `${interpolate(frame, [0, 80], [0.15, 0.72], clamp)}em`,
            opacity: interpolate(frame, [4, 22, 66, 80], [0, 0.96, 0.96, 0], clamp),
            textShadow: "0 0 90px rgba(241,167,190,0.6)",
          }}
        >
          呼吸
        </div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
