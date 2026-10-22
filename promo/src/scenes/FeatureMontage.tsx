import React from "react";
import { AbsoluteFill, Sequence } from "remotion";
import { FootageTag } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { Showcase } from "../components/Showcase";
import { SHOTS, syncTrim } from "../footage";

/** Beats 128-180: one two-bar card per feature, then a bar of gameplay back into the climax. */
export const FeatureMontage: React.FC<{ at: number }> = ({ at }) => (
  <AbsoluteFill style={{ backgroundColor: "#000" }}>
    <Sequence name="Library" durationInFrames={161}>
      <Showcase src={SHOTS.select.file} trimBefore={280} duration={161} index="01 / 06" zh="曲库" en="LIBRARY" detail={"搜索 · 收藏 · 排序\n导入自制谱面"} />
    </Sequence>
    <Sequence name="Practice" from={161} durationInFrames={162}>
      <Showcase src={SHOTS.practice.file} trimBefore={90} duration={162} index="02 / 06" zh="自由练习" en="PRACTICE" detail={"任选区间 · 循环练习\n0.5–2.0× 变速"} />
    </Sequence>
    <Sequence name="Chart editor" from={323} durationInFrames={162}>
      <Showcase src={SHOTS.editor.file} trimBefore={120} duration={162} index="03 / 06" zh="谱面工房" en="CHART EDITOR" detail={"四种音符 · 撤销 / 重做\n完整双区试玩"} />
    </Sequence>
    <Sequence name="Settings" from={485} durationInFrames={162}>
      <Showcase src={SHOTS.settings.file} trimBefore={60} duration={162} index="04 / 06" zh="偏好设置" en="SETTINGS" detail={"六页设置 · 自定义键位\n特效强度随心调节"} />
    </Sequence>
    <Sequence name="Tutorial" from={647} durationInFrames={162}>
      <Showcase src={SHOTS.tutorial.file} trimBefore={120} duration={162} index="05 / 06" zh="新手教程" en="TUTORIAL" detail={"五课循序渐进\n从单击到双区混合"} />
    </Sequence>
    <Sequence name="Calibration" from={809} durationInFrames={161}>
      <Showcase src={SHOTS.calibration.file} trimBefore={120} duration={161} index="06 / 06" zh="节拍校准" en="CALIBRATION" detail={"跟随听到的节拍\n自动建议音频偏移"} />
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
