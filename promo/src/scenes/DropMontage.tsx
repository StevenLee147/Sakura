import React from "react";
import { AbsoluteFill, Sequence, useCurrentFrame } from "remotion";
import { sampleSpectrum } from "../audio";
import { AudioSpectrumBar, BassHalo } from "../components/AudioVisualizer";
import { FootageTag } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";
import { COLORS, FONT_FAMILY } from "../theme";

const FRAMES_PER_BEAT = 3600 / 178;

/** Beat-synced + real sub-bass audio-reactive camera kick. */
const useAudioPulse = (at: number) => {
  const frame = useCurrentFrame();
  const { bass } = sampleSpectrum(at + frame);
  const sinceBeat = frame % FRAMES_PER_BEAT;
  const beatIndex = Math.floor(frame / FRAMES_PER_BEAT);
  const beatKick = (beatIndex % 4 === 0 ? 0.016 : 0.006) * Math.exp(-sinceBeat / 5);
  return 1 + beatKick + bass * 0.014;
};

/** Beats 80-128: high-impact drop cuts locked to 樱落呼吸 + Chart v3 Lv.14.5 boss showcase. */
export const DropMontage: React.FC<{ at: number }> = ({ at }) => {
  const pulse = useAudioPulse(at);
  return (
    <AbsoluteFill style={{ backgroundColor: "#000" }}>
      <AbsoluteFill style={{ scale: pulse }}>
        <Sequence name="Wide drop" durationInFrames={162}>
          <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at)} duration={162} zoomFrom={1.09} zoomTo={1.0} />
        </Sequence>
        <Sequence name="Keys + thumb bar close-up" from={162} durationInFrames={161}>
          <Footage
            src={SHOTS.playExpert.file}
            trimBefore={syncTrim(SHOTS.playExpert, at + 162)}
            duration={161}
            zoomFrom={1.78}
            zoomTo={1.66}
            centerX={0.23}
            centerY={0.62}
          />
        </Sequence>
        <Sequence name="Mouse melody close-up" from={323} durationInFrames={162}>
          <Footage
            src={SHOTS.playExpert.file}
            trimBefore={syncTrim(SHOTS.playExpert, at + 323)}
            duration={162}
            zoomFrom={1.48}
            zoomTo={1.62}
            centerX={0.7}
            centerY={0.55}
            centerXTo={0.73}
          />
        </Sequence>
        <Sequence name="Chart v3 boss storm" from={485} durationInFrames={162}>
          <Footage
            src={SHOTS.playSynergy.file}
            trimBefore={535}
            duration={162}
            zoomFrom={1.08}
            zoomTo={1.02}
            tiltFrom={-9}
            tiltTo={-3}
          />
          <div
            style={{
              position: "absolute",
              left: 140,
              bottom: 120,
              fontFamily: FONT_FAMILY,
              fontSize: 48,
              fontWeight: 700,
              color: COLORS.gold,
              letterSpacing: "0.2em",
              padding: "14px 32px",
              border: "2px solid rgba(244, 213, 138, 0.52)",
              backgroundColor: "rgba(10, 9, 22, 0.68)",
            }}
          >
            PETAL STORM: OVERDRIVE · EXPERT LV.14.5
          </div>
        </Sequence>
        <Sequence name="Pan: keys to mouse" from={647} durationInFrames={162}>
          <Footage
            src={SHOTS.playExpert.file}
            trimBefore={syncTrim(SHOTS.playExpert, at + 647)}
            duration={162}
            zoomFrom={1.34}
            zoomTo={1.3}
            centerX={0.26}
            centerXTo={0.74}
            centerY={0.56}
          />
        </Sequence>
        <Sequence name="Hard perspective" from={809} durationInFrames={162}>
          <Footage
            src={SHOTS.playHard.file}
            trimBefore={syncTrim(SHOTS.playHard, at + 809)}
            duration={162}
            zoomFrom={1.0}
            zoomTo={1.07}
            tiltFrom={5}
            tiltTo={0}
          />
        </Sequence>
      </AbsoluteFill>
      <BassHalo at={at} strength={0.38} />
      <div style={{ position: "absolute", left: 150, top: 108, pointerEvents: "none" }}>
        <AudioSpectrumBar at={at} width={480} height={54} opacity={0.76} />
      </div>
      <Sequence name="Drop flash" durationInFrames={16}>
        <Flash peak={0.76} length={16} />
      </Sequence>
      <Sequence name="Cut flash 2" from={162} durationInFrames={10}>
        <Flash peak={0.3} length={10} />
      </Sequence>
      <Sequence name="Cut flash 3" from={323} durationInFrames={10}>
        <Flash peak={0.3} length={10} />
      </Sequence>
      <Sequence name="Cut flash 4" from={485} durationInFrames={12}>
        <Flash color="#fff5dc" peak={0.36} length={12} />
      </Sequence>
      <Sequence name="Cut flash 5" from={647} durationInFrames={10}>
        <Flash peak={0.3} length={10} />
      </Sequence>
      <Sequence name="Cut flash 6" from={809} durationInFrames={10}>
        <Flash peak={0.3} length={10} />
      </Sequence>
      <FootageTag />
    </AbsoluteFill>
  );
};
