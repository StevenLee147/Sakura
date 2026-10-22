import React from "react";
import { AbsoluteFill, Sequence } from "remotion";
import { FootageTag } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";

/** Beats 484-488 (last bar of drop C, after the splice): the final phrase of the chart. */
export const Finale: React.FC<{ at: number }> = ({ at }) => (
  <AbsoluteFill style={{ backgroundColor: "#000" }}>
    <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at)} duration={82} zoomFrom={1.0} zoomTo={1.1} />
    <Sequence durationInFrames={12}>
      <Flash peak={0.35} length={12} />
    </Sequence>
    <FootageTag />
  </AbsoluteFill>
);

/** Outro, first two bars: the real result screen of the autoplay run. */
export const ResultShot: React.FC = () => (
  <AbsoluteFill style={{ backgroundColor: "#000" }}>
    <Footage src={SHOTS.playExpert.file} trimBefore={SHOTS.playExpert.resultFrame} duration={161} zoomFrom={1.04} zoomTo={1.0} />
    <Sequence durationInFrames={26}>
      <Flash peak={0.75} length={26} />
    </Sequence>
    <FootageTag />
  </AbsoluteFill>
);
