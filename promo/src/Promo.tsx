import { Audio } from "@remotion/media";
import React from "react";
import { AbsoluteFill, Sequence, staticFile } from "remotion";
import { Vignette } from "./components/Effects";
import { Breath } from "./scenes/Breath";
import { Concept } from "./scenes/Concept";
import { DropMontage } from "./scenes/DropMontage";
import { EndCard } from "./scenes/EndCard";
import { FeatureMontage } from "./scenes/FeatureMontage";
import { Finale, ResultShot } from "./scenes/Finale";
import { Opening } from "./scenes/Opening";
import { SongTitle } from "./scenes/SongTitle";

/**
 * Sakura · 樱 — 4K60 promo. Every cut sits on a bar line of the 樱落呼吸 edit (see edit.json / src/edit.ts);
 * the `at` prop is the scene's absolute edit frame so gameplay clips can be locked to the soundtrack.
 */
export const Promo: React.FC = () => (
  <AbsoluteFill style={{ backgroundColor: "#000" }}>
    <Sequence name="Opening" durationInFrames={324}>
      <Opening />
    </Sequence>
    <Sequence name="Concept" from={324} durationInFrames={647}>
      <Concept at={324} />
    </Sequence>
    <Sequence name="Song title" from={971} durationInFrames={243}>
      <SongTitle at={971} />
    </Sequence>
    <Sequence name="Breath" from={1214} durationInFrames={81}>
      <Breath at={1214} />
    </Sequence>
    <Sequence name="Drop montage" from={1295} durationInFrames={971}>
      <DropMontage at={1295} />
    </Sequence>
    <Sequence name="Features" from={2266} durationInFrames={1051}>
      <FeatureMontage at={2266} />
    </Sequence>
    <Sequence name="Finale" from={3317} durationInFrames={82}>
      <Finale at={3317} />
    </Sequence>
    <Sequence name="Result" from={3399} durationInFrames={161}>
      <ResultShot />
    </Sequence>
    <Sequence name="End card" from={3560} durationInFrames={210}>
      <EndCard />
    </Sequence>
    <Vignette strength={0.42} />
    <Audio name="Soundtrack" src={staticFile("audio/soundtrack.wav")} />
  </AbsoluteFill>
);
