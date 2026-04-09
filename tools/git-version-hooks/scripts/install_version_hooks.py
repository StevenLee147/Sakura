"""Install repository-managed git hooks for version bumping."""
from __future__ import annotations

import sys
from pathlib import Path


REPO_ROOT = Path(__file__).resolve().parents[1]
SRC_DIR = REPO_ROOT / "src"
if str(SRC_DIR) not in sys.path:
    sys.path.insert(0, str(SRC_DIR))

from version_hooks_tool.version_hook import cli


def main() -> int:
    return cli(["install", *sys.argv[1:]])


if __name__ == "__main__":
    raise SystemExit(main())