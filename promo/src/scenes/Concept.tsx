import React from "react";
import { AbsoluteFill, Sequence } from "remotion";
import { FootageTag, Headline } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";

/** Beats 32-64 (build-up): the two play zones, each in close-up and locked to the music. */
export const Concept: React.FC<{ at: number }> = ({ at }) => (
  <AbsoluteFill>
    <Sequence name="Left hand - keys" durationInFrames={324}>
      <Footage
        src={SHOTS.playExpert.file}
        trimBefore={syncTrim(SHOTS.playExpert, at)}
        duration={324}
        zoomFrom={1.62}
        zoomTo={1.74}
        centerX={0.22}
        centerY={0.6}
      />
      <AbsoluteFill style={{ background: "linear-gradient(270deg, rgba(8,7,18,0.86) 0%, rgba(8,7,18,0.5) 34%, rgba(8,7,18,0) 58%)" }} />
      <Headline zh="左手 · 节奏" en="LEFT HAND  ·  RHYTHM" detail="A S D F 四轨下落 · 单击 / 长按" x={3620} y={760} align="right" enter={8} exit={318} />
    </Sequence>
    <Sequence name="Right hand - mouse" from={324} durationInFrames={323}>
      <Footage
        src={SHOTS.playExpert.file}
        trimBefore={syncTrim(SHOTS.playExpert, at + 324)}
        duration={323}
        zoomFrom={1.5}
        zoomTo={1.6}
        centerX={0.71}
        centerY={0.54}
      />
      <AbsoluteFill style={{ background: "linear-gradient(90deg, rgba(8,7,18,0.86) 0%, rgba(8,7,18,0.5) 34%, rgba(8,7,18,0) 58%)" }} />
      <Headline zh="右手 · 旋律" en="RIGHT HAND  ·  MELODY" detail="鼠标点击 · 拖拽滑动 · Circle / Slider" x={220} y={760} align="left" enter={6} exit={317} />
    </Sequence>
    <Sequence name="Phrase flash" from={324} durationInFrames={12}>
      <Flash peak={0.28} length={12} />
    </Sequence>
    <FootageTag />
  </AbsoluteFill>
);
