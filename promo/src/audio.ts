import spectrumData from "../public/audio/spectrum.json";
import coversData from "../public/img/covers/manifest.json";

export type CoverEntry = {
  id: string;
  title: string;
  artist: string;
  bpm: number;
  maxLevel: number;
  version: number;
  cover: string;
};

export const COVERS: readonly CoverEntry[] = coversData;

const ZERO_BANDS = new Array<number>(24).fill(0);

/** Sample precomputed 60fps FFT spectrum & RMS/Bass envelope at absolute promo frame `globalFrame`. */
export const sampleSpectrum = (globalFrame: number) => {
  const idx = Math.max(0, Math.min(spectrumData.frames - 1, Math.round(globalFrame)));
  return {
    bass: spectrumData.bass[idx] ?? 0,
    rms: spectrumData.rms[idx] ?? 0,
    bands: spectrumData.bands[idx] ?? ZERO_BANDS,
  };
};
