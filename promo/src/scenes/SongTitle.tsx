import React from "react";
import { AbsoluteFill, Easing, Img, interpolate, staticFile, useCurrentFrame } from "remotion";
import { FootageTag } from "../components/Captions";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const clamp = { extrapolateLeft: "clamp", extrapolateRight: "clamp" } as const;
const ease = Easing.bezier(0.16, 1, 0.3, 1);

/** Beats 64-76 (drop lead-in): full gameplay with the new song's title card. */
export const SongTitle: React.FC<{ at: number }> = ({ at }) => {
  const frame = useCurrentFrame();
  const appear = interpolate(frame, [4, 34], [0, 1], { ...clamp, easing: ease });
  const leave = interpolate(frame, [222, 242], [1, 0], { ...clamp, easing: Easing.in(Easing.cubic) });
  return (
    <AbsoluteFill>
      <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at)} duration={243} zoomFrom={1.0} zoomTo={1.035} />
      <AbsoluteFill
        style={{
          background: "linear-gradient(90deg, rgba(8,7,18,0.9) 0%, rgba(8,7,18,0.72) 40%, rgba(8,7,18,0) 70%)",
          opacity: leave * appear,
        }}
      />
      <div
        style={{
          position: "absolute",
          left: 220,
          top: 640,
          display: "flex",
          alignItems: "center",
          gap: 110,
          fontFamily: FONT_FAMILY,
          opacity: leave,
          translate: `${(1 - appear) * -140}px 0px`,
        }}
      >
        <Img
          src={staticFile("img/sakura_breath_cover.png")}
          style={{
            width: 760,
            height: 760,
            borderRadius: 36,
            boxShadow: "0 40px 140px rgba(0,0,0,0.6), 0 0 0 3px rgba(241,167,190,0.35)",
            opacity: appear,
            scale: interpolate(frame, [4, 243], [1.04, 1.0], clamp),
          }}
        />
        <div>
          <div style={{ fontSize: 72, color: COLORS.sakura, letterSpacing: "0.34em", opacity: appear }}>新曲收录 · NEW SONG</div>
          <div
            style={{
              marginTop: 30,
              fontSize: 330,
              fontWeight: 700,
              color: COLORS.text,
              letterSpacing: "0.1em",
              lineHeight: 1.1,
              clipPath: `inset(0 ${(1 - appear) * 100}% 0 0)`,
              textShadow: "0 16px 70px rgba(6,5,14,0.8)",
            }}
          >
            樱落呼吸
          </div>
          <div style={{ marginTop: 40, fontSize: 82, color: COLORS.dim, letterSpacing: "0.16em", opacity: appear }}>
            EXPERT · 178 BPM
          </div>
        </div>
      </div>
      <FootageTag />
    </AbsoluteFill>
  );
};
