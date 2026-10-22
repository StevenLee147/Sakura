import { loadFont } from "@remotion/fonts";
import { staticFile } from "remotion";
import { FONT_FAMILY } from "./theme";

// The game's bundled fonts (copied into public/ by `python scripts/prepare.py assets`).
// loadFont() delays rendering until each face is ready.
loadFont({ family: FONT_FAMILY, url: staticFile("fonts/NotoSansSC-Regular.ttf"), weight: "400" });
loadFont({ family: FONT_FAMILY, url: staticFile("fonts/NotoSansSC-Bold.ttf"), weight: "700" });
