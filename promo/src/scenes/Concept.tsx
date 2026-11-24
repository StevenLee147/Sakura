import React from "react";
import { AbsoluteFill, Sequence } from "remotion";
import { FootageTag, Headline } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";
import { COLORS } from "../theme";

/** Beats 32-64 (build-up): 3-part breakdown — Left 5-Lane + Thumb Bar, Right Melody Zone, and Chart v3 Synergy. */
export const Concept: React.FC<{ at: number }> = ({ at }) => (
  <AbsoluteFill>
    <Sequence name="Left hand - 5 lanes" durationInFrames={243}>
      <Footage
        src={SHOTS.playExpert.file}
        trimBefore={syncTrim(SHOTS.playExpert, at)}
        duration={243}
        zoomFrom={1.58}
        zoomTo={1.72}
        centerX={0.23}
        centerY={0.62}
      />
      <AbsoluteFill style={{ background: "linear-gradient(270deg, rgba(8,7,18,0.88) 0%, rgba(8,7,18,0.54) 36%, rgba(8,7,18,0) 60%)" }} />
      <Headline
        zh="左手 · 五轨节奏"
        en="LEFT HAND  ·  5-LANE RHYTHM"
        detail="A S D F 四轨下落 · SPACE 樱枝横梁拇指轨"
        x={3620}
        y={730}
        align="right"
        zhSize={246}
        enter={8}
        exit={237}
      />
    </Sequence>
    <Sequence name="Right hand - melody" from={243} durationInFrames={243}>
      <Footage
        src={SHOTS.playExpert.file}
        trimBefore={syncTrim(SHOTS.playExpert, at + 243)}
        duration={243}
        zoomFrom={1.48}
        zoomTo={1.6}
        centerX={0.71}
        centerY={0.54}
      />
      <AbsoluteFill style={{ background: "linear-gradient(90deg, rgba(8,7,18,0.88) 0%, rgba(8,7,18,0.54) 36%, rgba(8,7,18,0) 60%)" }} />
      <Headline
        zh="右手 · 空间旋律"
        en="RIGHT HAND  ·  SPATIAL MELODY"
        detail="自由光标点击 · 曲线滑条 · 原生动态 BGA"
        x={220}
        y={730}
        align="left"
        zhSize={246}
        enter={6}
        exit={237}
      />
    </Sequence>
    <Sequence name="Dual-domain synergy" from={486} durationInFrames={161}>
      <Footage
        src={SHOTS.playSynergy.file}
        trimBefore={392}
        duration={161}
        zoomFrom={1.12}
        zoomTo={1.03}
        centerX={0.54}
        centerY={0.52}
      />
      <AbsoluteFill style={{ background: "linear-gradient(90deg, rgba(8,7,18,0.9) 0%, rgba(8,7,18,0.52) 40%, rgba(8,7,18,0.12) 72%)" }} />
      <Headline
        zh="双域协同 · 组合技"
        en="CHART V3  ·  SYNERGY & MANKAI"
        detail="键鼠修饰联动 · 双区同步判定 · FEVER 满开爆发"
        x={220}
        y={730}
        align="left"
        zhSize={236}
        enter={5}
        exit={156}
        accent={COLORS.gold}
      />
    </Sequence>
    <Sequence name="Cut flash 1" from={243} durationInFrames={12}>
      <Flash peak={0.28} length={12} />
    </Sequence>
    <Sequence name="Cut flash 2" from={486} durationInFrames={14}>
      <Flash color="#fff5dc" peak={0.36} length={14} />
    </Sequence>
    <FootageTag />
  </AbsoluteFill>
);
