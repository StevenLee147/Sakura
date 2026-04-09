"""Install repository-managed git-version-hooks for Sakura."""
from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
TOOL_SRC = REPO_ROOT / "tools" / "git-version-hooks" / "src"
if str(TOOL_SRC) not in sys.path:
    sys.path.insert(0, str(TOOL_SRC))

from version_hooks_tool.version_hook import cli


def main() -> int:
    return cli(["install", *sys.argv[1:]])


if __name__ == "__main__":
    raise SystemExit(main())