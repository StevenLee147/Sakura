import edit from "../edit.json";

export const FPS = edit.fps;
export const DURATION = edit.durationInFrames;

/**
 * Bar starts in edit frames (60 fps). Song grid: beat j at -0.01858 + j * 60 / 178 s, bars at j = 0 mod 4.
 * Segment 1 maps beat j to 60 * t(j) - 322; segment 2 (after the splice at 3317) maps it to 60 * t(j) - 6470.
 * One bar = 4 beats = 80.9 frames; the indices in the comments are song beats.
 */
export const BAR = {
  intro: 0, // beat 16, quiet intro
  menu: 162, // beat 24
  build: 324, // beat 32, build-up
  buildHalf: 648, // beat 48
  leadIn: 971, // beat 64, drop A lead-in
  dip: 1214, // beat 76, near-silent bar before the drop
  drop: 1295, // beat 80, DROP
  drop2: 1457,
  drop3: 1618,
  drop4: 1780,
  drop5: 1942,
  drop6: 2104,
  features: 2266, // beat 128
  feature2: 2427,
  feature3: 2589,
  feature4: 2751,
  feature5: 2913,
  feature6: 3075,
  climax: 3236, // beat 176
  splice: 3317, // beat 180 -> beat 484 (end of drop C)
  outro: 3399, // beat 488, outro begins
  endCard: 3560, // beat 496
  end: DURATION,
} as const;

/** Music time (in frames) heard at a given edit frame. */
export const musicFrameAt = (editFrame: number): number => {
  const segment = [...edit.segments].reverse().find((s) => editFrame >= s.editFrom) ?? edit.segments[0];
  return editFrame - segment.editFrom + segment.sourceFrom;
};
