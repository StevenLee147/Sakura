import React from "react";
import { AbsoluteFill, Easing, interpolate, Sequence, useCurrentFrame } from "remotion";
import { Fade } from "../components/Effects";
import { Footage } from "../components/Footage";
import { NightSky } from "../components/NightSky";
import { Petals } from "../components/Petals";
import { SHOTS } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.16, 1, 0.3, 1);

/** Beats 16-32 (quiet intro): a night-sky title, then the game's real title screen. */
export const Opening: React.FC = () => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill style={{ backgroundColor: COLORS.night }}>
      <Sequence name="Moon title" durationInFrames={204}>
        <AbsoluteFill style={{ scale: interpolate(frame, [0, 204], [1.0, 1.05], clamp) }}>
          <NightSky moonX={0.5} moonY={0.4} moonSize={1040} />
        </AbsoluteFill>
        <Petals count={30} seed="open-far" speed={0.55} minSize={22} maxSize={48} opacity={0.6} blur={2} />
        <AbsoluteFill style={{ fontFamily: FONT_FAMILY }}>
          <div
            style={{
              position: "absolute",
              left: 0,
              right: 0,
              top: 864 - 360,
              textAlign: "center",
              fontSize: 660,
              lineHeight: "720px",
              fontWeight: 700,
              color: "#211b38",
              opacity: interpolate(frame, [26, 74], [0, 1], { ...clamp, easing: ease }),
              filter: `blur(${interpolate(frame, [26, 84], [26, 0], { ...clamp, easing: ease })}px)`,
              scale: interpolate(frame, [26, 204], [1.06, 1], { ...clamp, easing: ease }),
            }}
          >
            樱
          </div>
          <div
            style={{
              position: "absolute",
              left: 0,
              right: 0,
              top: 1500,
              textAlign: "center",
              fontSize: 150,
              fontWeight: 400,
              color: COLORS.text,
              letterSpacing: `${interpolate(frame, [70, 130], [1.1, 0.62], { ...clamp, easing: ease })}em`,
              opacity: interpolate(frame, [70, 112], [0, 1], { ...clamp, easing: ease }),
              textShadow: "0 10px 60px rgba(6, 5, 14, 0.9)",
            }}
          >
            SAKURA
          </div>
          <div
            style={{
              position: "absolute",
              left: 0,
              right: 0,
              top: 1730,
              textAlign: "center",
              fontSize: 88,
              color: COLORS.dim,
              letterSpacing: "0.32em",
              opacity: interpolate(frame, [110, 146], [0, 1], { ...clamp, easing: ease }),
            }}
          >
            指尖落樱，双手成诗。
          </div>
        </AbsoluteFill>
      </Sequence>
      <Sequence name="Title screen" from={162} durationInFrames={162}>
        <AbsoluteFill style={{ opacity: interpolate(frame, [162, 204], [0, 1], { ...clamp, easing: Easing.inOut(Easing.cubic) }) }}>
          <Footage src={SHOTS.menu.file} trimBefore={60} duration={162} zoomFrom={1.0} zoomTo={1.07} centerX={0.6} centerY={0.46} />
          <Petals count={22} seed="open-near" speed={0.8} minSize={50} maxSize={110} opacity={0.7} />
        </AbsoluteFill>
      </Sequence>
      <Fade from={0} to={40} inverse />
    </AbsoluteFill>
  );
};
