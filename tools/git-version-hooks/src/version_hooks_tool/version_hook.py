"""Git hook entrypoint for reusable semantic version bumps and release tags."""
from __future__ import annotations

import argparse
import json
import os
import re
import subprocess
import sys
from dataclasses import dataclass, field
from datetime import datetime, timedelta, timezone
from pathlib import Path
from typing import Any, Optional


BEIJING_TZ = timezone(timedelta(hours=8))
DEFAULT_VERSION = "v0.1.0"
DEFAULT_CONFIG_FILE = "VERSION_HOOKS.json"
DEFAULT_VERSION_FILE = "VERSION.json"
DEFAULT_TAGS = ["dev", "alpha", "beta"]
VALID_BUMPS = {"major", "minor", "patch"}
NO_BUMP_CHOICES = {"no-bump", "none", "skip"}
VALID_CHOICES = VALID_BUMPS | NO_BUMP_CHOICES
SEMVER_PATTERN = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:[-+].*)?$")
TAG_PATTERN = re.compile(r"^[A-Za-z0-9][A-Za-z0-9._-]*$")


@dataclass
class HookConfig:
    version_file: str = DEFAULT_VERSION_FILE
    available_tags: list[str] = field(default_factory=lambda: list(DEFAULT_TAGS))
    active_tag: str = ""


@dataclass
class HookContext:
    repo_root: Path
    config_path: Path
    version_file: Path
    pending_file: Path
    hooks_dir: Path


def run_git(repo_root: Path, *args: str, check: bool = True) -> subprocess.CompletedProcess[str]:
    return subprocess.run(
        ["git", *args],
        cwd=repo_root,
        check=check,
        text=True,
        capture_output=True,
    )


def resolve_repo_root(explicit_repo: Optional[str] = None) -> Path:
    if explicit_repo:
        return Path(explicit_repo).resolve()

    env_repo = os.getenv("VERSION_HOOK_REPO_ROOT", "").strip()
    if env_repo:
        return Path(env_repo).resolve()

    result = subprocess.run(
        ["git", "rev-parse", "--show-toplevel"],
        check=False,
        text=True,
        capture_output=True,
    )
    if result.returncode != 0:
        raise RuntimeError("当前目录不在 Git 仓库内，无法确定仓库根目录。")
    return Path(result.stdout.strip()).resolve()


def resolve_git_path(repo_root: Path, relative_path: str) -> Path:
    result = run_git(repo_root, "rev-parse", "--git-path", relative_path)
    return Path(result.stdout.strip()).resolve()


def resolve_repo_path(repo_root: Path, raw_path: str) -> Path:
    candidate = Path(raw_path)
    if candidate.is_absolute():
        return candidate
    return (repo_root / candidate).resolve()


def normalize_version(raw_version: Any) -> str:
    value = str(raw_version or DEFAULT_VERSION).strip()
    match = SEMVER_PATTERN.match(value)
    if not match:
        return DEFAULT_VERSION
    major, minor, patch = match.groups()
    return f"v{major}.{minor}.{patch}"


def normalize_tag(raw_tag: str) -> str:
    tag = raw_tag.strip().lower()
    if not tag:
        return ""
    if not TAG_PATTERN.fullmatch(tag):
        raise ValueError(f"非法 tag: {raw_tag}")
    return tag


def unique_tags(tags: list[str]) -> list[str]:
    ordered: list[str] = []
    for item in tags:
        normalized = normalize_tag(item)
        if normalized and normalized not in ordered:
            ordered.append(normalized)
    return ordered


def build_release_tag(version: str, active_tag: str) -> str:
    return f"{version}-{active_tag}" if active_tag else version


def load_config(repo_root: Path) -> HookConfig:
    config_path = repo_root / DEFAULT_CONFIG_FILE
    if not config_path.exists():
        return HookConfig()

    with config_path.open("r", encoding="utf-8") as file:
        data = json.load(file)

    if not isinstance(data, dict):
        raise RuntimeError(f"配置文件 {config_path.name} 必须是 JSON 对象。")

    available_tags = data.get("available_tags") or DEFAULT_TAGS
    if not isinstance(available_tags, list):
        raise RuntimeError("available_tags 必须是数组。")

    active_tag = normalize_tag(str(data.get("active_tag") or ""))
    normalized_tags = unique_tags([str(item) for item in available_tags])
    if active_tag and active_tag not in normalized_tags:
        normalized_tags.append(active_tag)

    version_file = str(data.get("version_file") or DEFAULT_VERSION_FILE).strip() or DEFAULT_VERSION_FILE
    env_version_file = os.getenv("VERSION_HOOK_VERSION_FILE", "").strip()
    if env_version_file:
        version_file = env_version_file

    return HookConfig(
        version_file=version_file,
        available_tags=normalized_tags,
        active_tag=active_tag,
    )


def save_config(repo_root: Path, config: HookConfig) -> Path:
    config_path = repo_root / DEFAULT_CONFIG_FILE
    payload = {
        "version_file": config.version_file,
        "available_tags": config.available_tags,
        "active_tag": config.active_tag,
    }
    config_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return config_path


def build_context(repo_root: Path, config: HookConfig) -> HookContext:
    pending_override = os.getenv("VERSION_HOOK_PENDING_FILE", "").strip()
    pending_file = resolve_repo_path(repo_root, pending_override) if pending_override else resolve_git_path(repo_root, "version-hook-pending.json")
    hooks_dir = repo_root / ".githooks"
    return HookContext(
        repo_root=repo_root,
        config_path=repo_root / DEFAULT_CONFIG_FILE,
        version_file=resolve_repo_path(repo_root, config.version_file),
        pending_file=pending_file,
        hooks_dir=hooks_dir,
    )


def load_version_info(context: HookContext, config: HookConfig) -> dict[str, str]:
    fallback_tag = build_release_tag(DEFAULT_VERSION, config.active_tag)
    if not context.version_file.exists():
        return {
            "version": DEFAULT_VERSION,
            "updated_at": datetime.now(BEIJING_TZ).isoformat(),
            "tag": fallback_tag,
            "channel": config.active_tag,
        }

    try:
        with context.version_file.open("r", encoding="utf-8") as file:
            data = json.load(file)
    except (OSError, json.JSONDecodeError):
        return {
            "version": DEFAULT_VERSION,
            "updated_at": datetime.now(BEIJING_TZ).isoformat(),
            "tag": fallback_tag,
            "channel": config.active_tag,
        }

    version = normalize_version(data.get("version"))
    channel = normalize_tag(str(data.get("channel") or config.active_tag or ""))
    tag = str(data.get("tag") or build_release_tag(version, channel)).strip() or build_release_tag(version, channel)
    return {
        "version": version,
        "updated_at": str(data.get("updated_at") or datetime.now(BEIJING_TZ).isoformat()),
        "tag": tag,
        "channel": channel,
    }


def save_version_info(context: HookContext, version: str, active_tag: str) -> None:
    context.version_file.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": version,
        "updated_at": datetime.now(BEIJING_TZ).isoformat(),
        "channel": active_tag,
        "tag": build_release_tag(version, active_tag),
    }
    context.version_file.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def bump_semver(version: str, bump_type: str) -> str:
    match = SEMVER_PATTERN.match(version)
    if not match:
        raise ValueError(f"非法版本号: {version}")

    major, minor, patch = [int(part) for part in match.groups()]
    if bump_type == "major":
        major += 1
        minor = 0
        patch = 0
    elif bump_type == "minor":
        minor += 1
        patch = 0
    else:
        patch += 1
    return f"v{major}.{minor}.{patch}"


def choose_bump_type_gui(current_version: str, active_tag: str) -> Optional[str]:
    try:
        import tkinter as tk
    except ImportError:
        return None

    selection = {"value": None}
    root = tk.Tk()
    root.title("选择版本升级类型")
    root.resizable(False, False)
    root.attributes("-topmost", True)

    selected = tk.StringVar(value="patch")
    frame = tk.Frame(root, padx=16, pady=16)
    frame.pack(fill="both", expand=True)

    channel_label = active_tag or "stable"
    tk.Label(frame, text=f"当前版本: {current_version}", anchor="w", font=("Microsoft YaHei UI", 10, "bold")).pack(fill="x")
    tk.Label(frame, text=f"当前发布通道: {channel_label}", anchor="w", pady=(6, 2)).pack(fill="x")
    tk.Label(frame, text="请选择本次提交对应的版本动作：", anchor="w", pady=8).pack(fill="x")

    options = [
        ("major  大版本升级", "major"),
        ("minor  小版本升级", "minor"),
        ("patch  修复版本升级", "patch"),
        ("no-bump  不更新版本", "no-bump"),
    ]
    for label, value in options:
        tk.Radiobutton(frame, text=label, variable=selected, value=value, anchor="w", justify="left").pack(fill="x", pady=2)

    hint = "major: 破坏性变更\nminor: 新功能或增强\npatch: 修复或小改动\nno-bump: 不改版本文件，也不创建 tag"
    tk.Label(frame, text=hint, anchor="w", justify="left", fg="#555555", pady=8).pack(fill="x")

    button_frame = tk.Frame(frame, pady=8)
    button_frame.pack(fill="x")

    def confirm() -> None:
        selection["value"] = selected.get()
        root.destroy()

    def cancel() -> None:
        root.destroy()

    tk.Button(button_frame, text="确认", width=10, command=confirm).pack(side="left", padx=(0, 8))
    tk.Button(button_frame, text="取消", width=10, command=cancel).pack(side="left")

    root.protocol("WM_DELETE_WINDOW", cancel)
    root.update_idletasks()
    width = root.winfo_width()
    height = root.winfo_height()
    screen_width = root.winfo_screenwidth()
    screen_height = root.winfo_screenheight()
    x = int((screen_width - width) / 2)
    y = int((screen_height - height) / 3)
    root.geometry(f"+{x}+{y}")
    root.mainloop()
    return selection["value"]


def choose_bump_type(current_version: str, active_tag: str) -> str:
    env_choice = os.getenv("VERSION_HOOK_BUMP", "").strip().lower()
    if env_choice in VALID_BUMPS:
        return env_choice
    if env_choice in NO_BUMP_CHOICES:
        return "no-bump"

    if not sys.stdin.isatty() or not sys.stdout.isatty():
        gui_choice = choose_bump_type_gui(current_version, active_tag)
        if gui_choice in VALID_CHOICES:
            return "no-bump" if gui_choice in NO_BUMP_CHOICES else gui_choice
        raise RuntimeError(
            "当前提交环境不可交互，且未完成图形化版本选择。请重试，或先设置 VERSION_HOOK_BUMP=major|minor|patch|no-bump。"
        )

    channel_label = active_tag or "stable"
    prompt = (
        f"\n当前版本: {current_version}  发布通道: {channel_label}\n"
        "请选择本次提交的版本动作:\n"
        "  1. major    大版本升级\n"
        "  2. minor    小版本升级\n"
        "  3. patch    修复版本升级\n"
        "  4. no-bump  不更新版本\n"
        "输入 1/2/3/4 后回车: "
    )
    mapping = {"1": "major", "2": "minor", "3": "patch", "4": "no-bump"}
    while True:
        answer = input(prompt).strip()
        if answer in mapping:
            return mapping[answer]
        print("无效输入，请输入 1、2、3 或 4。")


def load_pending_action(context: HookContext) -> Optional[dict[str, str]]:
    if not context.pending_file.exists():
        return None
    with context.pending_file.open("r", encoding="utf-8") as file:
        data = json.load(file)
    if not isinstance(data, dict):
        return None
    return {key: str(value or "") for key, value in data.items()}


def write_pending_action(context: HookContext, version: str, bump_type: str, release_tag: str, active_tag: str) -> None:
    context.pending_file.parent.mkdir(parents=True, exist_ok=True)
    payload = {
        "version": version,
        "bump_type": bump_type,
        "release_tag": release_tag,
        "active_tag": active_tag,
    }
    context.pending_file.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")


def clear_pending_action(context: HookContext) -> None:
    if context.pending_file.exists():
        context.pending_file.unlink()


def ensure_existing_pending_version(context: HookContext, config: HookConfig) -> bool:
    pending = load_pending_action(context)
    if not pending:
        return False

    pending_version = normalize_version(pending.get("version"))
    pending_tag = pending.get("release_tag") or build_release_tag(pending_version, pending.get("active_tag") or config.active_tag)
    current_info = load_version_info(context, config)
    current_tag = build_release_tag(current_info["version"], config.active_tag)
    if pending_version == current_info["version"] and pending_tag == current_tag:
        run_git(context.repo_root, "add", str(context.version_file))
        print(f"[version-hook] 复用上一次未完成提交的版本号 {pending_version}")
        return True

    clear_pending_action(context)
    return False


def pre_commit(repo_root: Optional[str] = None) -> int:
    if os.getenv("VERSION_HOOK_SKIP", "").strip() == "1":
        return 0

    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    context = build_context(resolved_repo, config)

    if ensure_existing_pending_version(context, config):
        return 0

    current_version = load_version_info(context, config)["version"]
    bump_type = choose_bump_type(current_version, config.active_tag)
    if bump_type == "no-bump":
        clear_pending_action(context)
        print("[version-hook] 已选择 no-bump，本次提交不更新版本也不创建 tag。")
        return 0

    next_version = bump_semver(current_version, bump_type)
    release_tag = build_release_tag(next_version, config.active_tag)
    save_version_info(context, next_version, config.active_tag)
    run_git(context.repo_root, "add", str(context.version_file))
    write_pending_action(context, next_version, bump_type, release_tag, config.active_tag)
    print(f"[version-hook] 版本已从 {current_version} 更新到 {next_version}，发布 tag 为 {release_tag}")
    return 0


def post_commit(repo_root: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    context = build_context(resolved_repo, config)
    pending = load_pending_action(context)
    if not pending:
        return 0

    version = normalize_version(pending.get("version"))
    release_tag = pending.get("release_tag") or build_release_tag(version, pending.get("active_tag") or config.active_tag)
    if not release_tag:
        clear_pending_action(context)
        return 0

    existing_tag = run_git(context.repo_root, "rev-parse", "-q", "--verify", f"refs/tags/{release_tag}", check=False)
    if existing_tag.returncode == 0:
        print(f"[version-hook] Git tag {release_tag} 已存在，跳过创建。")
        clear_pending_action(context)
        return 0

    create_tag = run_git(context.repo_root, "tag", "-a", release_tag, "-m", f"Release {release_tag}", check=False)
    if create_tag.returncode != 0:
        print(create_tag.stderr.strip() or f"[version-hook] 创建 Git tag {release_tag} 失败。", file=sys.stderr)
        return 1

    clear_pending_action(context)
    print(f"[version-hook] 已创建 Git tag {release_tag}")
    return 0


def hook_script(command: str, local_script_relpath: str) -> str:
    local_script_path = Path(local_script_relpath).as_posix()
    error_message = "未找到可用的 Python，无法执行版本 Hook。"
    return "\n".join(
        [
            "#!/bin/sh",
            "set -e",
            "",
            "ROOT=$(git rev-parse --show-toplevel)",
            f"LOCAL_SCRIPT=\"$ROOT/{local_script_path}\"",
            "REPO_VENV_PY=\"$ROOT/.venv/Scripts/python.exe\"",
            "REPO_VENV_PY_UNIX=\"$ROOT/.venv/bin/python\"",
            "",
            "if [ -f \"$LOCAL_SCRIPT\" ]; then",
            "    if [ -x \"$REPO_VENV_PY\" ]; then",
            f"        VERSION_HOOK_REPO_ROOT=\"$ROOT\" \"$REPO_VENV_PY\" \"$LOCAL_SCRIPT\" {command}",
            "        exit $?",
            "    fi",
            "",
            "    if [ -x \"$REPO_VENV_PY_UNIX\" ]; then",
            f"        VERSION_HOOK_REPO_ROOT=\"$ROOT\" \"$REPO_VENV_PY_UNIX\" \"$LOCAL_SCRIPT\" {command}",
            "        exit $?",
            "    fi",
            "",
            "    if command -v python >/dev/null 2>&1; then",
            f"        VERSION_HOOK_REPO_ROOT=\"$ROOT\" python \"$LOCAL_SCRIPT\" {command}",
            "        exit $?",
            "    fi",
            "",
            "    if command -v python3 >/dev/null 2>&1; then",
            f"        VERSION_HOOK_REPO_ROOT=\"$ROOT\" python3 \"$LOCAL_SCRIPT\" {command}",
            "        exit $?",
            "    fi",
            "",
            "    if command -v py >/dev/null 2>&1; then",
            f"        VERSION_HOOK_REPO_ROOT=\"$ROOT\" py -3 \"$LOCAL_SCRIPT\" {command}",
            "        exit $?",
            "    fi",
            "fi",
            "",
            "if command -v python >/dev/null 2>&1; then",
            f"    VERSION_HOOK_REPO_ROOT=\"$ROOT\" python -m version_hooks_tool.version_hook {command}",
            "    exit $?",
            "fi",
            "",
            "if command -v python3 >/dev/null 2>&1; then",
            f"    VERSION_HOOK_REPO_ROOT=\"$ROOT\" python3 -m version_hooks_tool.version_hook {command}",
            "    exit $?",
            "fi",
            "",
            "if command -v py >/dev/null 2>&1; then",
            f"    VERSION_HOOK_REPO_ROOT=\"$ROOT\" py -3 -m version_hooks_tool.version_hook {command}",
            "    exit $?",
            "fi",
            "",
            f"echo \"{error_message}\" >&2",
            "exit 1",
            "",
        ]
    )


def install_hooks(repo_root: Optional[str] = None, version_file: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    if version_file:
        config.version_file = version_file
    context = build_context(resolved_repo, config)

    context.hooks_dir.mkdir(parents=True, exist_ok=True)
    pre_commit_hook = context.hooks_dir / "pre-commit"
    post_commit_hook = context.hooks_dir / "post-commit"

    repo_entry_script = resolved_repo / "scripts" / "version_hook_entry.py"
    if repo_entry_script.exists():
        local_script_relpath = str(repo_entry_script.relative_to(resolved_repo))
    else:
        try:
            local_script_relpath = str(Path(__file__).resolve().relative_to(resolved_repo))
        except ValueError:
            local_script_relpath = "src/version_hooks_tool/version_hook.py"

    pre_commit_hook.write_text(hook_script("pre-commit", local_script_relpath), encoding="utf-8", newline="\n")
    post_commit_hook.write_text(hook_script("post-commit", local_script_relpath), encoding="utf-8", newline="\n")
    pre_commit_hook.chmod(0o755)
    post_commit_hook.chmod(0o755)
    save_config(resolved_repo, config)
    run_git(resolved_repo, "config", "core.hooksPath", ".githooks")

    print(f"[version-hook] 已安装 Git hooks 到 {context.hooks_dir}")
    print(f"[version-hook] 配置文件: {context.config_path.name}")
    print(f"[version-hook] 版本文件: {config.version_file}")
    return 0


def print_config(repo_root: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    context = build_context(resolved_repo, config)
    current_version = load_version_info(context, config)
    payload = {
        "repo_root": str(resolved_repo),
        "config_file": context.config_path.name,
        "version_file": str(context.version_file.relative_to(resolved_repo)),
        "active_tag": config.active_tag,
        "available_tags": config.available_tags,
        "current_version": current_version["version"],
        "current_release_tag": build_release_tag(current_version["version"], config.active_tag),
    }
    print(json.dumps(payload, ensure_ascii=False, indent=2))
    return 0


def tag_list(repo_root: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    current = config.active_tag or "stable"
    print(f"当前发布通道: {current}")
    for item in config.available_tags:
        marker = "*" if item == config.active_tag else "-"
        print(f"{marker} {item}")
    if not config.available_tags:
        print("(当前没有预设 tag)")
    return 0


def tag_add(tag: str, repo_root: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    normalized = normalize_tag(tag)
    if normalized not in config.available_tags:
        config.available_tags.append(normalized)
        save_config(resolved_repo, config)
    print(f"[version-hook] 已添加 tag: {normalized}")
    return 0


def tag_set(tag: str, repo_root: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    normalized = normalize_tag(tag)
    if normalized not in config.available_tags:
        config.available_tags.append(normalized)
    config.active_tag = normalized
    save_config(resolved_repo, config)
    print(f"[version-hook] 当前发布通道已切换为: {normalized}")
    return 0


def tag_clear(repo_root: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    config.active_tag = ""
    save_config(resolved_repo, config)
    print("[version-hook] 当前发布通道已切换为 stable")
    return 0


def set_version_file(version_file: str, repo_root: Optional[str] = None) -> int:
    resolved_repo = resolve_repo_root(repo_root)
    config = load_config(resolved_repo)
    config.version_file = version_file.strip() or DEFAULT_VERSION_FILE
    save_config(resolved_repo, config)
    print(f"[version-hook] 版本文件已设置为: {config.version_file}")
    return 0


def build_parser() -> argparse.ArgumentParser:
    parser = argparse.ArgumentParser(description="Reusable git version hooks tool")
    subparsers = parser.add_subparsers(dest="command", required=True)

    for hook_name in ("pre-commit", "post-commit"):
        hook_parser = subparsers.add_parser(hook_name)
        hook_parser.add_argument("--repo", dest="repo")

    install_parser = subparsers.add_parser("install")
    install_parser.add_argument("--repo", dest="repo")
    install_parser.add_argument("--version-file", dest="version_file")

    config_parser = subparsers.add_parser("config")
    config_subparsers = config_parser.add_subparsers(dest="config_command", required=True)

    show_parser = config_subparsers.add_parser("show")
    show_parser.add_argument("--repo", dest="repo")

    version_file_parser = config_subparsers.add_parser("set-version-file")
    version_file_parser.add_argument("path")
    version_file_parser.add_argument("--repo", dest="repo")

    tag_parser = subparsers.add_parser("tag")
    tag_subparsers = tag_parser.add_subparsers(dest="tag_command", required=True)

    tag_list_parser = tag_subparsers.add_parser("list")
    tag_list_parser.add_argument("--repo", dest="repo")

    tag_add_parser = tag_subparsers.add_parser("add")
    tag_add_parser.add_argument("tag")
    tag_add_parser.add_argument("--repo", dest="repo")

    tag_set_parser = tag_subparsers.add_parser("set")
    tag_set_parser.add_argument("tag")
    tag_set_parser.add_argument("--repo", dest="repo")

    tag_clear_parser = tag_subparsers.add_parser("clear")
    tag_clear_parser.add_argument("--repo", dest="repo")
    return parser


def cli(argv: Optional[list[str]] = None) -> int:
    parser = build_parser()
    args = parser.parse_args(argv)

    if args.command == "pre-commit":
        return pre_commit(args.repo)
    if args.command == "post-commit":
        return post_commit(args.repo)
    if args.command == "install":
        return install_hooks(args.repo, args.version_file)
    if args.command == "config":
        if args.config_command == "show":
            return print_config(args.repo)
        if args.config_command == "set-version-file":
            return set_version_file(args.path, args.repo)
    if args.command == "tag":
        if args.tag_command == "list":
            return tag_list(args.repo)
        if args.tag_command == "add":
            return tag_add(args.tag, args.repo)
        if args.tag_command == "set":
            return tag_set(args.tag, args.repo)
        if args.tag_command == "clear":
            return tag_clear(args.repo)
    parser.error("未知命令")
    return 1


def main() -> int:
    return cli(sys.argv[1:])


if __name__ == "__main__":
    raise SystemExit(main())