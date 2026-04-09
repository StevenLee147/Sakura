"""Repository entrypoint for git-version-hooks with Sakura sync steps."""
from __future__ import annotations

import os
import subprocess
import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_SRC = REPO_ROOT / "tools" / "git-version-hooks" / "src"
SYNC_SCRIPT = REPO_ROOT / "scripts" / "sync_repo_version.py"

if str(TOOL_SRC) not in sys.path:
    sys.path.insert(0, str(TOOL_SRC))

from version_hooks_tool.version_hook import cli


def run_sync() -> int:
    if os.getenv("VERSION_HOOK_SKIP", "").strip() == "1":
        return 0
    if not SYNC_SCRIPT.exists():
        return 0

    completed = subprocess.run(
        [sys.executable, str(SYNC_SCRIPT)],
        cwd=REPO_ROOT,
        check=False,
    )
    if completed.returncode != 0:
        print("[version-hook] Sakura 版本同步失败。", file=sys.stderr)
    return completed.returncode


def main() -> int:
    args = sys.argv[1:]
    if not args:
        print("缺少 hook 命令。", file=sys.stderr)
        return 1

    result = cli(args)
    if result != 0:
        return result

    if args[0] == "pre-commit":
        return run_sync()

    return 0


if __name__ == "__main__":
    raise SystemExit(main())