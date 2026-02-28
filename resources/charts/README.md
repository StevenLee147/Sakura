# resources/charts/

每首曲子对应**一个独立子文件夹**，文件夹名即为谱面 ID（与 `info.json` 中的 `id` 字段一致）。

## 目录结构约定

```
resources/charts/
├── {chart_id}/          ← 每首曲子一个文件夹
│   ├── info.json        ← 谱面元信息（标题、曲师、难度列表等）
│   ├── normal.json      ← Normal 难度谱面数据
│   ├── hard.json        ← Hard 难度谱面数据（可选）
│   ├── expert.json      ← Expert 难度谱面数据（可选）
│   ├── music.ogg        ← 音乐文件（推荐 OGG/FLAC）
│   ├── cover.png        ← 封面图片（512x512 或 1024x1024）
│   └── bg.png           ← 游戏内背景图片（可选）
│
├── test-song/           ← 内置测试谱面（开发用）
│   ├── info.json
│   └── normal.json
│
└── README.md            ← 本文件
```

## 说明

- `info.json` 中的 `music_file`、`cover_file`、`background_file` 均为**相对于本谱面文件夹**的路径
- `ChartLoader::ScanCharts()` 扫描此目录，递归查找所有包含 `info.json` 的子文件夹
- 格式详见 `doc/CHART_FORMAT_SPEC.md`
