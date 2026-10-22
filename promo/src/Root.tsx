import "./fonts";
import React from "react";
import { Composition } from "remotion";
import { Cover } from "./Cover";
import { Promo } from "./Promo";

export const RemotionRoot: React.FC = () => {
  return (
    <>
      <Composition id="SakuraPromo" component={Promo} durationInFrames={3770} fps={60} width={3840} height={2160} />
      {/* One-frame composition (not <Still>, which runs at 1 fps) so footage trims stay in 60 fps frames. */}
      <Composition id="BilibiliCover" component={Cover} durationInFrames={1} fps={60} width={1920} height={1200} />
    </>
  );
};
