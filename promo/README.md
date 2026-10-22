# Sakura · 樱 宣传片（Remotion）

4K60（3840 × 2160，60 fps）约 63 秒宣传片。全部实机画面由游戏的离线逐帧录制模式生成，配乐为新曲《樱落呼吸》的 60 秒剪辑版，所有剪辑点都落在小节线上，游戏画面与配乐逐帧对齐。

## 目录

| 路径 | 内容 |
|---|---|
| `edit.json` | 配乐剪辑定义（唯一数据源）：两段音乐在成片与原曲中的帧位置、交叉淡化、淡出与响度目标 |
| `record/shots.json` | 交给 `Sakura.exe --record` 的录制脚本（菜单、曲库、练习、工房、设置、教程、校准、Expert/Hard 自动演示） |
| `scripts/prepare.py` | 生成 `public/` 下的素材：字体与图片、配乐剪辑、导入录制结果 |
| `src/edit.ts` | 小节线帧号表与「成片帧 → 音乐帧」换算 |
| `src/footage.ts` | 录制素材清单，`syncTrim()` 让实机片段与配乐同步 |
| `src/scenes/` | 各段落：开场、双区概念、曲名、呼吸、Drop 快剪、功能一览、终段、结算、片尾 |
| `src/components/` | 樱花花瓣、夜空月光、实机片段虚拟镜头、功能展示窗、字幕、闪白与暗角 |
| `src/Cover.tsx` | B 站封面（1920 × 1200，16:10），背景取自同一份实机录制 |

`public/`、`record-out/`、`out/` 均为生成内容，不提交到仓库。

## 制作流程

在 `promo/` 目录内执行（PowerShell）：

```powershell
npm i

# 1. 录制 4K60 实机素材（游戏需先以 Release 构建；录制期间可正常使用电脑）
..\build\debug\Release\Sakura.exe --record record\shots.json --record-out record-out

# 2. 生成素材：字体与图片、配乐剪辑（默认取游戏内置的 music.ogg，也可用 --music 指定原始音源）、导入录制结果
python scripts\prepare.py assets
python scripts\prepare.py soundtrack
python scripts\prepare.py footage --record-out record-out

# 3. 预览、抽帧检查与渲染
npm run dev
node scripts\stills.mjs "120,1300,2650,3650" 0.5          # 指定帧的检查图写入 out\stills
npx remotion render SakuraPromo out\Sakura_Promo_4K60.mp4 --codec=h264 --crf=15 --x264-preset=slow --pixel-format=yuv420p --color-space=bt709 --audio-codec=aac --audio-bitrate=320k
npx remotion still BilibiliCover out\Sakura_Bilibili_Cover.jpg --frame=0 --image-format=jpeg --jpeg-quality=92
```

`prepare.py footage` 会写出 `public/footage/index.json`，其中 `musicZeroFrame`（音乐 0 秒对应的视频帧）与 `resultFrame`（结算画面出现的帧）需同步到 `src/footage.ts`。重新录制后只需更新这两个数字。

## 剪辑结构（178 BPM，一小节 ≈ 80.9 帧）

| 成片帧 | 原曲拍号 | 段落 |
|---|---|---|
| 0–324 | 16–32 | 开场：月下「樱」字 → 游戏标题画面 |
| 324–971 | 32–64 | 渐强：左手节奏 / 右手旋律 特写 |
| 971–1214 | 64–76 | 新曲《樱落呼吸》字卡 |
| 1214–1295 | 76–80 | 「呼吸」：Drop 前的静默小节 |
| 1295–2266 | 80–128 | Drop：两小节一切的实机快剪 |
| 2266–3317 | 128–180 | 功能一览：曲库、练习、工房、设置、教程、校准 |
| 3317–3399 | 484–488 | 拼接到 Drop C 最后一小节 |
| 3399–3770 | 488–504 | 尾声：结算画面 → 片尾 |

两段 Drop 的编排相同，因此拼接点（Drop A 第 29 小节 → Drop C 第 29 小节）在和声与音色上几乎无缝；交叉淡化放在下拍前的自然停顿里。

## 署名与声明

- 字体：Noto Sans SC（SIL OFL 1.1，随游戏发布于 `resources/fonts/`）。
- 配乐：《樱落呼吸》由项目作者使用 Suno（付费订阅）生成，作者拥有其权利；发布时请勾选「含 AI 生成内容」。
- 实机画面为自动演示（AUTO），片中已标注。
