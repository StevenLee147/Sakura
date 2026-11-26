# Sakura 内置曲库

本目录包含 9 首内置原创曲目、共 26 张双区谱面（覆盖 Easy / Normal / Hard / Expert 四个难度梯度）；全部曲目均为项目作者通过 Suno 付费方案创作生成的真实 OGG Vorbis 音源（版权归项目作者所有，不含任何程序合成占位 WAV）。曲库时长、采样率与各难度音符统计汇总见 `album.json`。

| ID | 曲目 | 曲包 | BPM | 难度 | 时长 |
|---|---|---|---|---|---|
| `sakura_breath` | 樱落呼吸 | Vol. 02 | 178 | Normal 4.0 / Hard 7.5 / Expert 10.5 | 194.16 s |
| `sakura_awakening` | 樱光初醒 | Vol. 03 | 124 | Easy 2.5 / Normal 4.5 / Hard 7.0 | 60.00 s |
| `starry_sky` | 繁星 | Vol. 03 | 119 | Normal 3.5 / Hard 6.5 / Expert 9.5 | 103.88 s |
| `distant_star` | 遥远的星 | Vol. 03 | 184 | Normal 4.5 / Hard 7.5 / Expert 10.5 | 148.80 s |
| `unfallen_light` | 樱光未落 | Vol. 03 | 126 | Normal 5.0 / Hard 8.0 / Expert 11.0 | 156.20 s |
| `sidechain_dream` | 樱落侧链梦 | Vol. 03 | 192 | Normal 5.5 / Hard 8.5 / Expert 11.5 | 60.00 s |
| `flowing_petals` | 樱落流光 | Vol. 03 | 127.5 | Normal 5.0 / Hard 8.5 / Expert 12.0 | 179.00 s |
| `instant_bloom` | 一瞬花绽 | Vol. 03 | 140 | Normal 5.5 / Hard 9.0 / Expert 12.5 | 209.56 s |
| `petal_storm_overdrive` | Petal Storm: Overdrive | Vol. 03 | 187 | Hard 10.5 / Expert 14.5 | 218.92 s |

「樱落呼吸」（Sakura Originals · Vol. 02）由 `scripts/build_sakura_breath.py` 根据真实音频分析生成：键盘端（左手）跟随底鼓、军鼓、镲片等打击乐起音，鼠标端（右手）跟随主旋律起音，音高越高位置越靠上，长音转为 Slider，并自带独立 Pillow 封面绘制流程。

其余 8 首官方精选曲目（Sakura Official Pack · Vol. 03）由 `scripts/build_song_pack.py` 从未跟踪目录 `download/songs/` 中的源 MP3 转码为 `music.ogg`，并通过 STFT 谐波/打击乐分离（HPSS）与拍点自相关网格分析生成多难度双区谱面（涵盖 A/S/D/F 四轨、Space 樱枝横梁、跨区同押 `link_id`、键鼠协同 `require_lane`、曲线 Slider 与 SV 变速）及专属夜樱封面。

每首曲目独立存放 `info.json`、难度 JSON、`music.ogg`、`cover.png` 以及可选背景图或背景视频（如 `bg.mp4`）。资源字段均为相对本曲目目录的路径。自制谱面建议通过游戏导入到用户数据目录的 `charts`；编辑内置谱面会建立用户副本。

音乐支持 WAV/MP3/FLAC/OGG Vorbis；图片支持 PNG/JPEG/WebP/BMP。格式详见 `doc/CHART_FORMAT_SPEC.md`。
