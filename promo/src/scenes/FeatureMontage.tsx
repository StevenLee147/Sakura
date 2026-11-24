import React from "react";
import { AbsoluteFill, Sequence } from "remotion";
import { FootageTag } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { Showcase } from "../components/Showcase";
import { SHOTS, syncTrim } from "../footage";

/** Beats 128-180: six two-bar v1.0.0 feature showcases, then a bar of gameplay back into the climax. */
export const FeatureMontage: React.FC<{ at: number }> = ({ at }) => (
  <AbsoluteFill style={{ backgroundColor: "#000" }}>
    <Sequence name="Library" durationInFrames={161}>
      <Showcase
        src={SHOTS.select.file}
        trimBefore={160}
        duration={161}
        at={at}
        badge="13 TRACKS"
        index="01 / 06"
        zh="精选曲库"
        en="SONG LIBRARY"
        detail={"13 首多风格曲目 · 原生动态 BGA\n二十局准度趋势 · 自制谱包导入"}
      />
    </Sequence>
    <Sequence name="Deep telemetry" from={161} durationInFrames={162}>
      <Showcase
        src={SHOTS.playTelemetry.file}
        trimBefore={318}
        duration={162}
        at={at + 161}
        badge="V1.0 NEW"
        index="02 / 06"
        zh="深度分析"
        en="DEEP TELEMETRY"
        detail={"UR 稳定度 · 全曲时间偏差散点\n七维双区准度 · 薄弱段一键特训"}
      />
    </Sequence>
    <Sequence name="Chart studio" from={323} durationInFrames={162}>
      <Showcase
        src={SHOTS.editor.file}
        trimBefore={54}
        duration={162}
        at={at + 323}
        badge="CHART V3"
        index="03 / 06"
        zh="谱面工房"
        en="CHART STUDIO"
        detail={"双域可视化编辑 · 组合技编排\n撤销重做 · 任意小节实机试玩"}
      />
    </Sequence>
    <Sequence name="Synergy tutorial" from={485} durationInFrames={162}>
      <Showcase
        src={SHOTS.tutorial.file}
        trimBefore={278}
        duration={162}
        at={at + 485}
        badge="6 LESSONS"
        index="04 / 06"
        zh="双域教程"
        en="SYNERGY GUIDE"
        detail={"六课交互式教学 · 零门槛上手\n从单键下落到樱枝横梁组合技"}
      />
    </Sequence>
    <Sequence name="Practice & sync" from={647} durationInFrames={162}>
      <Showcase
        src={SHOTS.practice.file}
        trimBefore={30}
        duration={162}
        at={at + 647}
        badge="0.5–2.0×"
        index="05 / 06"
        zh="区间特训"
        en="PRACTICE & SYNC"
        detail={"A–B 区间循环 · 自由倍速精练\n毫秒级音画节拍引导校准"}
      />
    </Sequence>
    <Sequence name="Stats & achievements" from={809} durationInFrames={161}>
      <Showcase
        src={SHOTS.stats.file}
        trimBefore={30}
        duration={161}
        at={at + 809}
        badge="15 BADGES"
        index="06 / 06"
        zh="生涯成就"
        en="STATS & THEMES"
        detail={"全曲目评级统计 · 十五项生涯成就\n多套月夜主题与键位自由定制"}
      />
    </Sequence>
    <Sequence name="Back to the stage" from={970} durationInFrames={81}>
      <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at + 970)} duration={81} zoomFrom={1.12} zoomTo={1.0} />
      <FootageTag />
    </Sequence>
    <Sequence name="Feature flash 2" from={161} durationInFrames={9}>
      <Flash peak={0.22} length={9} />
    </Sequence>
    <Sequence name="Feature flash 3" from={323} durationInFrames={9}>
      <Flash peak={0.22} length={9} />
    </Sequence>
    <Sequence name="Feature flash 4" from={485} durationInFrames={9}>
      <Flash peak={0.22} length={9} />
    </Sequence>
    <Sequence name="Feature flash 5" from={647} durationInFrames={9}>
      <Flash peak={0.22} length={9} />
    </Sequence>
    <Sequence name="Feature flash 6" from={809} durationInFrames={9}>
      <Flash peak={0.22} length={9} />
    </Sequence>
    <Sequence name="Stage flash" from={970} durationInFrames={14}>
      <Flash peak={0.55} length={14} />
    </Sequence>
  </AbsoluteFill>
);
