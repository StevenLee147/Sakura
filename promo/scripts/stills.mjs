// Render QA stills of the promo with one bundle: node scripts/stills.mjs <frame,frame,...> [scale] [outDir]
import { bundle } from "@remotion/bundler";
import { renderStill, selectComposition } from "@remotion/renderer";
import fs from "node:fs";
import path from "node:path";

const frames = (process.argv[2] ?? "120,600,1100,1300,2400,3650").split(",").map((v) => Number(v.trim()));
const scale = Number(process.argv[3] ?? 0.25);
const outDir = path.resolve(process.argv[4] ?? "out/stills");
fs.mkdirSync(outDir, { recursive: true });

const serveUrl = await bundle({ entryPoint: path.resolve("src/index.ts"), publicDir: path.resolve("public") });
const composition = await selectComposition({ serveUrl, id: "SakuraPromo" });
for (const frame of frames) {
  const output = path.join(outDir, `f${String(frame).padStart(4, "0")}.jpg`);
  await renderStill({ serveUrl, composition, frame, output, scale, imageFormat: "jpeg", jpegQuality: 88 });
  console.log(output);
}
