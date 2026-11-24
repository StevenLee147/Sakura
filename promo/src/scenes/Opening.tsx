import React from "react";
import { AbsoluteFill, Easing, Img, interpolate, Sequence, staticFile, useCurrentFrame } from "remotion";
import { AudioSpectrumBar, BassHalo } from "../components/AudioVisualizer";
import { Fade } from "../components/Effects";
import { Footage } from "../components/Footage";
import { NightSky } from "../components/NightSky";
import { Petals } from "../components/Petals";
import { SHOTS } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.16, 1, 0.3, 1);

/** Beats 16-32 (quiet intro): moonlit crest & v1.0.0 release mark, then the game's real 4K60 title screen. */
export const Opening: React.FC = () => {
  const frame = useCurrentFrame();
  return (
    <AbsoluteFill style={{ backgroundColor: COLORS.night }}>
      <Sequence name="Moon title" durationInFrames={204}>
        <AbsoluteFill style={{ scale: interpolate(frame, [0, 204], [1.0, 1.05], clamp) }}>
          <NightSky moonX={0.5} moonY={0.4} moonSize={1040} />
        </AbsoluteFill>
        <AbsoluteFill style={{ opacity: 0.24, mixBlendMode: "screen" }}>
          <Footage src={SHOTS.bga.file} trimBefore={0} duration={204} zoomFrom={1.02} zoomTo={1.08} />
        </AbsoluteFill>
        <BassHalo at={0} strength={0.32} />
        <Petals count={32} seed="open-far" speed={0.58} minSize={22} maxSize={52} opacity={0.65} blur={2} />
        <AbsoluteFill style={{ fontFamily: FONT_FAMILY }}>
          <div
            style={{
              position: "absolute",
              left: 0,
              right: 0,
              top: 176,
              display: "flex",
              justifyContent: "center",
              alignItems: "center",
              gap: 28,
              opacity: interpolate(frame, [18, 56], [0, 1], { ...clamp, easing: ease }),
            }}
          >
            <Img src={staticFile("img/sakura_crest.png")} style={{ width: 72, height: 72, opacity: 0.92 }} />
            <div
              style={{
                fontSize: 46,
                fontWeight: 700,
                color: COLORS.gold,
                letterSpacing: "0.32em",
                padding: "12px 34px 12px 48px",
                border: "2px solid rgba(244, 213, 138, 0.52)",
                backgroundColor: "rgba(12, 11, 24, 0.64)",
              }}
            >
              OFFICIAL RELEASE · v1.0.0 正式版
            </div>
          </div>
          <div
            style={{
              position: "absolute",
              left: 0,
              right: 0,
              top: 490,
              textAlign: "center",
              fontSize: 660,
              lineHeight: "720px",
              fontWeight: 700,
              color: "#241d3d",
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
              top: 1450,
              textAlign: "center",
              fontSize: 154,
              fontWeight: 700,
              color: COLORS.text,
              letterSpacing: `${interpolate(frame, [66, 128], [1.08, 0.62], { ...clamp, easing: ease })}em`,
              paddingLeft: `${interpolate(frame, [66, 128], [1.08, 0.62], { ...clamp, easing: ease })}em`,
              opacity: interpolate(frame, [66, 110], [0, 1], { ...clamp, easing: ease }),
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
              top: 1690,
              textAlign: "center",
              fontSize: 84,
              color: COLORS.sakuraLight,
              letterSpacing: "0.34em",
              paddingLeft: "0.34em",
              opacity: interpolate(frame, [104, 142], [0, 1], { ...clamp, easing: ease }),
            }}
          >
            指尖落樱，双手成诗。
          </div>
          <div
            style={{
              position: "absolute",
              left: 0,
              right: 0,
              bottom: 130,
              display: "flex",
              justifyContent: "center",
              opacity: interpolate(frame, [90, 140], [0, 0.72], clamp),
            }}
          >
            <AudioSpectrumBar at={0} width={720} height={58} mirror />
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
