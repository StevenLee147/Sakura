# Git Version Hooks Tool

一个可复用到任意 Git 仓库的版本 Hook 工具，支持两种接入方式：

- 作为 Python 包安装到本机环境后接入任意仓库
- 直接把整个目录拷贝到目标仓库根目录

它提供以下能力：

- 自动配置仓库的 `core.hooksPath`
- 提交前选择 `major / minor / patch / no-bump`
- 自动更新版本文件，默认是 `VERSION.json`
- 支持持久化管理发布 tag 通道，如 `dev`、`alpha`、`beta`
- 提交后自动创建 annotated tag，如 `v1.2.3-dev`

## 目录结构

```text
git-version-hooks/
├── .githooks/
├── scripts/
├── src/version_hooks_tool/
├── VERSION.json
├── VERSION_HOOKS.json
└── README.md
```

## 方案一：直接拷贝到项目里

1. 把整个目录放到目标仓库根目录。
2. 在目标仓库根目录执行：

```powershell
python scripts/install_version_hooks.py
```

3. 安装完成后，正常提交即可。

## 方案二：作为 Python 包安装

在任意环境中安装本工具后，在目标仓库根目录执行：

```powershell
pip install .
version-hooks install
```

如果目标项目的版本文件不是默认的 `VERSION.json`，可以在安装时指定：

```powershell
version-hooks install --version-file package.json
```

说明：

- 这里的 `--version-file` 只是指定“由本工具维护的 JSON 文件路径”，不会自动兼容 `package.json` 或 `pyproject.toml` 原生结构。
- 如果你要接入已有项目，推荐先把版本信息单独落到一个 JSON 文件里，例如 `build/VERSION.json`。

## 提交时的行为

每次提交前会弹出终端选择，或者在非交互环境下尝试 GUI：

- `major`：大版本升级
- `minor`：小版本升级
- `patch`：补丁升级
- `no-bump`：本次提交不更新版本，也不创建 tag

如果当前启用了发布通道，例如 `dev`，那么 bump 后的 Git tag 会自动变成：

- `v1.2.3-dev`
- `v1.2.3-alpha`
- `v1.2.3-beta`

如果当前通道为空，则创建标准 tag：

- `v1.2.3`

## Tag 通道管理

查看当前通道和可用列表：

```powershell
version-hooks tag list
```

新增一个通道：

```powershell
version-hooks tag add rc
```

切换当前通道：

```powershell
version-hooks tag set dev
```

清空当前通道，回到稳定版：

```powershell
version-hooks tag clear
```

## 配置文件

安装后会在仓库根目录生成或维护 `VERSION_HOOKS.json`：

```json
{
	"version_file": "VERSION.json",
	"available_tags": ["dev", "alpha", "beta"],
	"active_tag": ""
}
```

字段说明：

- `version_file`：版本文件路径，支持相对仓库根目录的路径
- `available_tags`：允许切换的发布通道列表
- `active_tag`：当前默认发布通道，空字符串表示稳定版

当前版本文件格式如下：

```json
{
	"version": "v0.1.0",
	"updated_at": "2026-03-19T00:00:00+08:00",
	"channel": "",
	"tag": "v0.1.0"
}
```

## 配置命令

查看当前仓库配置：

```powershell
version-hooks config show
```

修改版本文件路径：

```powershell
version-hooks config set-version-file build/VERSION.json
```

## 环境变量

- `VERSION_HOOK_BUMP=major|minor|patch|no-bump`：非交互模式下直接指定升级动作
- `VERSION_HOOK_SKIP=1`：跳过整个版本 Hook
- `VERSION_HOOK_VERSION_FILE`：临时覆盖版本文件路径
- `VERSION_HOOK_PENDING_FILE`：临时覆盖 pending 文件路径
- `VERSION_HOOK_REPO_ROOT`：显式指定目标仓库根目录

## 说明

- 工具默认以 Git 仓库根目录为工作目录。
- 如果版本文件不存在，会自动使用默认版本 `v0.1.0`。
- 当前版本文件必须是 JSON 对象，并包含 `version` 字段；如果缺失，会回退到默认版本。
- 这个工具适合 Python 项目、前端项目以及通用任意 Git 仓库，只要仓库里能执行 Python 即可。