import React from "react";
import { AbsoluteFill, Sequence } from "remotion";
import { BassHalo } from "../components/AudioVisualizer";
import { FootageTag } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";

/** Beats 484-488 (last bar of drop C, after the splice): the final phrase of the chart. */
export const Finale: React.FC<{ at: number }> = ({ at }) => (
  <AbsoluteFill style={{ backgroundColor: "#000" }}>
    <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at)} duration={82} zoomFrom={1.0} zoomTo={1.11} />
    <BassHalo at={at} strength={0.42} />
    <Sequence durationInFrames={12}>
      <Flash peak={0.38} length={12} />
    </Sequence>
    <FootageTag />
  </AbsoluteFill>
);

/** Outro, first two bars: the real ALL PERFECT 1,000,000 result screen of 樱落呼吸 Expert. */
export const ResultShot: React.FC = () => (
  <AbsoluteFill style={{ backgroundColor: "#000" }}>
    <Footage src={SHOTS.playExpert.file} trimBefore={SHOTS.playExpert.resultFrame} duration={161} zoomFrom={1.045} zoomTo={1.0} />
    <Sequence durationInFrames={28}>
      <Flash color="#fff7e2" peak={0.78} length={28} />
    </Sequence>
    <FootageTag text="实机结算 · ALL PERFECT 1,000,000" />
  </AbsoluteFill>
);
