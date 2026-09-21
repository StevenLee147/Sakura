# Sakura 内置曲库

本目录包含 6 首曲目、10 张谱面；主曲包信息见 `album.json`。

| 曲目 | BPM | 难度 | 时长 |
|---|---|---|---|
| Spring Breeze | 130 | Normal 3 / Hard 5.5 | 76.35 s |
| Cherry Blossom | 155 | Normal 4 / Hard 7 / Expert 10 | 64.44 s |
| Digital Dream | 175 | Hard 8.5 / Expert 12 | 57.36 s |
| Sakura Storm | 200 | Expert 14 | 50.50 s |
| First Steps | 100 | 入门练习 | 入门练习曲 |
| Timing Garden | 120 | 节奏练习 | 节奏练习曲 |

四首主曲使用 40 小节的完整段落结构，包含前奏、主题、间奏、再现与结尾；音符覆盖全曲并保留休息段。音乐、封面可通过 `scripts/build_starter_album.py` 重建，音源为程序合成，不使用外部录音采样。

每首曲目独立存放 `info.json`、难度 JSON、音乐和可选图片。资源字段均为相对本曲目目录的路径。自制谱面建议通过游戏导入到用户数据目录的 `charts`；编辑内置谱面会建立用户副本。

音乐支持 WAV/MP3/FLAC/OGG Vorbis；图片支持 PNG/JPEG/WebP/BMP。格式详见 `doc/CHART_FORMAT_SPEC.md`。
