import React from "react";
import { AbsoluteFill, Easing, interpolate, useCurrentFrame } from "remotion";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;

/** Beats 76-80: the near-silent bar before the drop. The camera leans in and the frame holds its breath. */
export const Breath: React.FC<{ at: number }> = ({ at }) => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill>
      <Footage
        src={SHOTS.playExpert.file}
        trimBefore={syncTrim(SHOTS.playExpert, at)}
        duration={81}
        zoomFrom={1.035}
        zoomTo={1.16}
        dimFrom={0.1}
        dimTo={0.62}
        easing={Easing.in(Easing.cubic)}
      />
      <AbsoluteFill style={{ justifyContent: "center", alignItems: "center", fontFamily: FONT_FAMILY }}>
        <div
          style={{
            fontSize: 420,
            fontWeight: 700,
            color: COLORS.sakuraLight,
            letterSpacing: `${interpolate(frame, [0, 80], [0.15, 0.7], clamp)}em`,
            opacity: interpolate(frame, [4, 22, 66, 80], [0, 0.95, 0.95, 0], clamp),
            textShadow: "0 0 90px rgba(241,167,190,0.55)",
          }}
        >
          呼吸
        </div>
      </AbsoluteFill>
    </AbsoluteFill>
  );
};
