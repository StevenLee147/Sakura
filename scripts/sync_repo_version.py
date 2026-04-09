"""Synchronize Sakura's version-managed files from VERSION.json.

Versioned archival documents are intentionally excluded. This script only updates
the living files that should always mirror the repository's current version.
"""
from __future__ import annotations

import json
import re
import subprocess
import sys
from datetime import datetime, timedelta, timezone
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
DEFAULT_VERSION_FILE = REPO_ROOT / "VERSION.json"
BEIJING_TZ = timezone(timedelta(hours=8))
SEMVER_PATTERN = re.compile(r"^v?(\d+)\.(\d+)\.(\d+)(?:[-+].*)?$")


def load_hook_config() -> tuple[Path, str]:
    config_path = REPO_ROOT / "VERSION_HOOKS.json"
    if not config_path.exists():
        return DEFAULT_VERSION_FILE, ""

    with config_path.open("r", encoding="utf-8") as file:
        config = json.load(file)

    raw_path = str(config.get("version_file") or DEFAULT_VERSION_FILE.name).strip()
    candidate = Path(raw_path)
    active_tag = str(config.get("active_tag") or "").strip()
    if candidate.is_absolute():
        return candidate, active_tag
    return (REPO_ROOT / candidate).resolve(), active_tag


def load_version_info(version_path: Path) -> tuple[str, str, str, dict[str, str]]:
    with version_path.open("r", encoding="utf-8") as file:
        data = json.load(file)

    if not isinstance(data, dict):
        raise RuntimeError(f"{version_path} 必须是 JSON 对象。")

    raw_version = str(data.get("version") or "").strip()
    match = SEMVER_PATTERN.fullmatch(raw_version)
    if not match:
        raise RuntimeError(f"非法版本号: {raw_version}")

    core_version = ".".join(match.groups())
    channel = str(data.get("channel") or "").strip()
    release_tag = str(data.get("tag") or "").strip() or (f"v{core_version}-{channel}" if channel else f"v{core_version}")
    return core_version, channel, release_tag, data


def sync_version_metadata(version_path: Path, core_version: str, channel: str, data: dict[str, str]) -> bool:
    release_tag = f"v{core_version}-{channel}" if channel else f"v{core_version}"
    current_payload = {
        "version": str(data.get("version") or "").strip(),
        "updated_at": str(data.get("updated_at") or "").strip(),
        "channel": str(data.get("channel") or "").strip(),
        "tag": str(data.get("tag") or "").strip(),
    }

    next_payload_without_time = {
        "version": f"v{core_version}",
        "channel": channel,
        "tag": release_tag,
    }
    current_payload_without_time = {
        "version": current_payload["version"],
        "channel": current_payload["channel"],
        "tag": current_payload["tag"],
    }
    updated_at = current_payload["updated_at"] or datetime.now(BEIJING_TZ).isoformat()
    if current_payload_without_time != next_payload_without_time:
        updated_at = datetime.now(BEIJING_TZ).isoformat()

    payload = {
        **next_payload_without_time,
        "updated_at": updated_at,
    }
    if current_payload == payload:
        return False

    version_path.write_text(json.dumps(payload, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
    return True


def replace_once(path: Path, pattern: str, replacement: str, description: str) -> bool:
    original = path.read_text(encoding="utf-8")
    updated, count = re.subn(pattern, replacement, original, count=1, flags=re.MULTILINE)
    if count != 1:
        raise RuntimeError(f"无法在 {path} 中定位 {description}。")
    if updated == original:
        return False
    path.write_text(updated, encoding="utf-8", newline="\n")
    return True


def update_cmake(core_version: str, channel: str) -> bool:
    cmake_path = REPO_ROOT / "CMakeLists.txt"
    changed_version = replace_once(
        cmake_path,
        r"^(\s*VERSION\s+)\d+\.\d+\.\d+$",
        rf"\g<1>{core_version}",
        "project VERSION",
    )
    changed_channel = replace_once(
        cmake_path,
        r'^set\(SAKURA_VERSION_PRERELEASE "[^"]*"\)$',
        f'set(SAKURA_VERSION_PRERELEASE "{channel}")',
        "SAKURA_VERSION_PRERELEASE",
    )
    return changed_version or changed_channel


def update_vcpkg(core_version: str) -> bool:
    manifest_path = REPO_ROOT / "vcpkg.json"
    return replace_once(
        manifest_path,
        r'^(\s*"version-string"\s*:\s*")\d+\.\d+\.\d+("\s*,?)$',
        rf'\g<1>{core_version}\g<2>',
        'vcpkg version-string',
    )


def update_roadmap(release_tag: str) -> bool:
    roadmap_path = REPO_ROOT / "doc" / "ROADMAP.md"
    return replace_once(
        roadmap_path,
        r'^- \*\*当前版本：\*\* v\d+\.\d+\.\d+(?:-[A-Za-z0-9._-]+)?$',
        f'- **当前版本：** {release_tag}',
        'ROADMAP 当前版本',
    )


def stage_files(paths: list[Path]) -> None:
    relative_paths = [str(path.relative_to(REPO_ROOT)).replace("\\", "/") for path in paths]
    subprocess.run(["git", "add", *relative_paths], cwd=REPO_ROOT, check=True)


def main() -> int:
    version_path, configured_tag = load_hook_config()
    core_version, current_channel, release_tag, version_data = load_version_info(version_path)
    channel = configured_tag if configured_tag else current_channel
    release_tag = f"v{core_version}-{channel}" if channel else f"v{core_version}"

    managed_paths = [
        version_path,
        REPO_ROOT / "CMakeLists.txt",
        REPO_ROOT / "vcpkg.json",
        REPO_ROOT / "doc" / "ROADMAP.md",
    ]

    sync_version_metadata(version_path, core_version, channel, version_data)
    update_cmake(core_version, channel)
    update_vcpkg(core_version)
    update_roadmap(release_tag)
    stage_files(managed_paths)

    print(f"[version-hook] 已同步 Sakura 版本文件到 {release_tag}")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as exc:  # pragma: no cover - hook diagnostics path
        print(f"[version-hook] {exc}", file=sys.stderr)
        raise SystemExit(1)