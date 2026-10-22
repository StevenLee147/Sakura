import { musicFrameAt } from "./edit";

/**
 * Recorded with `Sakura.exe --record record/shots.json` (4K60, offline frame-exact clock) and imported by
 * `python scripts/prepare.py footage --record-out <dir>`, which writes public/footage/index.json.
 * musicZeroFrame = video frame that shows music time 0; resultFrame = first frame of the result screen.
 */
export const SHOTS = {
  menu: { file: "footage/menu.mp4" },
  select: { file: "footage/select.mp4" },
  practice: { file: "footage/practice.mp4" },
  editor: { file: "footage/editor.mp4" },
  settings: { file: "footage/settings.mp4" },
  tutorial: { file: "footage/tutorial.mp4" },
  calibration: { file: "footage/calibration.mp4" },
  playExpert: { file: "footage/play_expert.mp4", musicZeroFrame: 181, resultFrame: 11856 },
  playHard: { file: "footage/play_hard.mp4", musicZeroFrame: 181 },
} as const;

/** trimBefore for a gameplay clip that starts at `editFrame`, so notes stay locked to the soundtrack. */
export const syncTrim = (shot: { musicZeroFrame: number }, editFrame: number): number =>
  shot.musicZeroFrame + musicFrameAt(editFrame);
