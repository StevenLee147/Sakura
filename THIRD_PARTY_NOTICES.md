# Third-party notices

Sakura uses the libraries below. A generated Windows release contains their
installed copyright and license texts in `licenses/`.

| Component | License / notice |
|---|---|
| SDL3, SDL3_image, SDL3_ttf | zlib |
| miniaudio | Public domain or MIT No Attribution; see bundled notice |
| stb_vorbis | Public domain or MIT; see bundled notice |
| nlohmann/json, spdlog, fmt | MIT |
| SQLite | Public domain |
| FreeType | FreeType License or GPL; see bundled FreeType notice |
| libpng, zlib, Brotli, bzip2, libjpeg-turbo, libwebp | Individual notices bundled under `licenses/` |
| Noto Sans SC | SIL Open Font License 1.1; `resources/fonts/OFL.txt` |
| Microsoft Visual C++ runtime | Microsoft redistributable runtime; Microsoft license terms apply |

The bundled fonts are from the Noto Sans SC family. Their reserved font names,
copyright and full OFL text are retained with the assets.

The starter music, chart arrangements, covers and app icon in this update were
generated specifically for Sakura. `scripts/build_starter_album.py` documents
and reproduces the synthesized music and covers; it uses no external recordings
or sampled music. Third-party licenses apply to their respective components and
do not assign a license to the rest of the project.

The track 「樱落呼吸」 (`resources/charts/sakura_breath/music.ogg` and procedural
looping stage video `bg.mp4`) and Official Song Pack Vol.03 (`resources/charts/*`)
were generated with Suno by the project author on a paid Suno plan, under which
the author owns the output. Their Chart v3 arrangements and procedural covers
were produced for Sakura by `scripts/build_sakura_breath.py` and
`scripts/build_song_pack.py`.

Windows hardware background video decoding uses the operating system's built-in
Microsoft Media Foundation runtime (`mfplat.dll`, `mfreadwrite.dll`), and the
Windows setup installer (`Sakura-*-win64-setup.exe`) is packaged with Inno Setup 6
(Inno Setup License).
