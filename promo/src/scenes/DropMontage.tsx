import React from "react";
import { AbsoluteFill, Sequence, useCurrentFrame } from "remotion";
import { FootageTag } from "../components/Captions";
import { Flash } from "../components/Effects";
import { Footage } from "../components/Footage";
import { SHOTS, syncTrim } from "../footage";

const FRAMES_PER_BEAT = 3600 / 178;

/** Beat-synced camera pulse: a light kick on every beat and a stronger one on each downbeat. */
const usePulse = () => {
  const frame = useCurrentFrame();
  const sinceBeat = frame % FRAMES_PER_BEAT;
  const beatIndex = Math.floor(frame / FRAMES_PER_BEAT);
  const amount = beatIndex % 4 === 0 ? 0.018 : 0.007;
  return 1 + amount * Math.exp(-sinceBeat / 5);
};

/** Beats 80-128: the drop. Two-bar shots cut on downbeats, every clip locked to the soundtrack. */
export const DropMontage: React.FC<{ at: number }> = ({ at }) => {
  const pulse = usePulse();
  return (
    <AbsoluteFill style={{ backgroundColor: "#000" }}>
      <AbsoluteFill style={{ scale: pulse }}>
        <Sequence name="Wide" durationInFrames={162}>
          <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at)} duration={162} zoomFrom={1.09} zoomTo={1.0} />
        </Sequence>
        <Sequence name="Keys close-up" from={162} durationInFrames={161}>
          <Footage
            src={SHOTS.playExpert.file}
            trimBefore={syncTrim(SHOTS.playExpert, at + 162)}
            duration={161}
            zoomFrom={1.85}
            zoomTo={1.72}
            centerX={0.22}
            centerY={0.6}
          />
        </Sequence>
        <Sequence name="Mouse close-up" from={323} durationInFrames={162}>
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
        <Sequence name="Hard, tilted" from={485} durationInFrames={162}>
          <Footage
            src={SHOTS.playHard.file}
            trimBefore={syncTrim(SHOTS.playHard, at + 485)}
            duration={162}
            zoomFrom={1.06}
            zoomTo={1.02}
            tiltFrom={-11}
            tiltTo={-5}
          />
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
        <Sequence name="Wide again" from={809} durationInFrames={162}>
          <Footage src={SHOTS.playExpert.file} trimBefore={syncTrim(SHOTS.playExpert, at + 809)} duration={162} zoomFrom={1.0} zoomTo={1.06} />
        </Sequence>
      </AbsoluteFill>
      <Sequence name="Drop flash" durationInFrames={14}>
        <Flash peak={0.7} length={14} />
      </Sequence>
      <Sequence name="Cut flash 2" from={162} durationInFrames={10}>
        <Flash peak={0.3} length={10} />
      </Sequence>
      <Sequence name="Cut flash 3" from={323} durationInFrames={10}>
        <Flash peak={0.3} length={10} />
      </Sequence>
      <Sequence name="Cut flash 4" from={485} durationInFrames={10}>
        <Flash peak={0.3} length={10} />
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
