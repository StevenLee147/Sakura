"""Unattended build, verification, version-bump, and GitHub Release runner for Sakura.

Supports zero-human-intervention Autopilot loops across sequential milestone releases:
  python scripts/autopilot_release.py --status
  python scripts/autopilot_release.py --verify-local
  python scripts/autopilot_release.py --release --target-tag v0.8.0-alpha --channel alpha --bump minor --message "..."
"""
from __future__ import annotations

import argparse
from datetime import datetime
import json
import os
from pathlib import Path
import shutil
import socket
import subprocess
import sys
import time

REPO = Path(__file__).resolve().parents[1]
CO_AUTHOR = "Co-authored-by: Copilot App <223556219+Copilot@users.noreply.github.com>"

ROADMAP_RELEASES = [
    {"tag": "v0.8.0-alpha", "channel": "alpha", "bump": "minor", "spec": "doc/roadmap/V0_8_SYNERGY_AND_SONGS.md"},
    {"tag": "v0.8.1-alpha", "channel": "alpha", "bump": "patch", "spec": "doc/roadmap/V0_8_SYNERGY_AND_SONGS.md"},
    {"tag": "v0.9.0-beta", "channel": "beta", "bump": "minor", "spec": "doc/roadmap/V0_9_PRO_TOOLS_AND_BGA.md"},
    {"tag": "v0.9.1-beta", "channel": "beta", "bump": "patch", "spec": "doc/roadmap/V0_9_PRO_TOOLS_AND_BGA.md"},
    {"tag": "v1.0.0", "channel": "stable", "bump": "major", "spec": "doc/roadmap/V1_0_OFFICIAL_RELEASE.md"},
    {"tag": "v1.1.0", "channel": "stable", "bump": "minor", "spec": "doc/roadmap/V1_1_1_2_ONLINE_ECOSYSTEM.md"},
    {"tag": "v1.2.0", "channel": "stable", "bump": "minor", "spec": "doc/roadmap/V1_1_1_2_ONLINE_ECOSYSTEM.md"},
]


def build_env(extra: dict[str, str] | None = None) -> dict[str, str]:
    env = os.environ.copy()
    if not env.get("VCPKG_ROOT") and Path(r"C:\dev\vcpkg").is_dir():
        env["VCPKG_ROOT"] = r"C:\dev\vcpkg"
    if extra:
        env.update(extra)
    return env


def run(cmd: list[str], *, env: dict[str, str] | None = None, check: bool = True, capture: bool = False) -> subprocess.CompletedProcess[str]:
    print(f"[autopilot] $ {' '.join(cmd)}", flush=True)
    return subprocess.run(
        cmd,
        cwd=REPO,
        env=env or build_env(),
        text=True,
        encoding="utf-8",
        errors="replace",
        capture_output=capture,
        check=check,
    )


def get_published_releases() -> dict[str, dict]:
    res = run(["gh", "release", "list", "--limit", "30", "--json", "tagName,isPrerelease,publishedAt"], check=False, capture=True)
    if res.returncode != 0 or not res.stdout.strip():
        return {}
    items = json.loads(res.stdout)
    return {item["tagName"]: item for item in items}


def show_status() -> int:
    version_data = json.loads((REPO / "VERSION.json").read_text(encoding="utf-8"))
    hooks_data = json.loads((REPO / "VERSION_HOOKS.json").read_text(encoding="utf-8"))
    local_tags = set(run(["git", "tag", "-l"], capture=True).stdout.splitlines())
    published = get_published_releases()

    print(f"Current VERSION.json : {version_data.get('tag')} (version={version_data.get('version')}, channel={version_data.get('channel')!r})")
    print(f"Active hook channel  : {hooks_data.get('active_tag')!r}")
    print("\nRoadmap Release Ladder:")
    next_target = None
    for item in ROADMAP_RELEASES:
        tag = item["tag"]
        is_pub = tag in published
        has_tag = tag in local_tags
        state = "PUBLISHED" if is_pub else ("TAGGED_LOCAL" if has_tag else "PENDING")
        if not is_pub and next_target is None:
            next_target = item
        print(f"  - {tag:<14} [{state:<12}] channel={item['channel']:<6} bump={item['bump']:<5} ({item['spec']})")

    if next_target:
        print(f"\nNext Target Release  : {next_target['tag']} (spec: {next_target['spec']})")
    else:
        print("\nAll roadmap releases up to v1.2.0 are PUBLISHED!")
    return 0


def pick_free_port() -> int:
    with socket.socket(socket.AF_INET, socket.SOCK_STREAM) as sock:
        sock.bind(("127.0.0.1", 0))
        return int(sock.getsockname()[1])


def verify_online_if_present(exe_path: Path, qa_root: Path) -> None:
    server_pkg = REPO / "server" / "package.json"
    if not server_pkg.is_file():
        return
    npm_exe = "npm.cmd" if os.name == "nt" else "npm"
    run([npm_exe, "--prefix", "server", "test"])

    server_entry = REPO / "server" / "src" / "index.ts"
    if not server_entry.is_file():
        return
    port = pick_free_port()
    data_dir = qa_root / "online-server-data"
    data_dir.mkdir(parents=True, exist_ok=True)
    env = build_env({
        "PORT": str(port),
        "SAKURA_SERVER_DATA_DIR": str(data_dir),
        "SAKURA_JWT_SECRET": "autopilot-e2e-secret-key",
        "SAKURA_USER_DIR": str(qa_root / "online-client-user"),
    })
    npx_exe = "npx.cmd" if os.name == "nt" else "npx"
    proc = subprocess.Popen(
        [npx_exe, "--prefix", "server", "tsx", "server/src/index.ts"],
        cwd=REPO,
        env=env,
        stdout=subprocess.PIPE,
        stderr=subprocess.STDOUT,
        text=True,
        encoding="utf-8",
        errors="replace",
    )
    try:
        base_url = f"http://127.0.0.1:{port}"
        deadline = time.time() + 20.0
        ready = False
        import urllib.request
        while time.time() < deadline:
            try:
                with urllib.request.urlopen(f"{base_url}/api/v1/health", timeout=1.0) as resp:
                    if resp.status == 200:
                        ready = True
                        break
            except OSError:
                time.sleep(0.3)
        if not ready:
            raise RuntimeError("Local Sakura online server failed to become healthy within 20s.")
        run([str(exe_path), "--verify-online", base_url], env=env)
    finally:
        proc.terminate()
        try:
            proc.wait(timeout=5)
        except subprocess.TimeoutExpired:
            proc.kill()


def verify_local() -> None:
    run(["cmake", "--preset", "release", "-DSAKURA_BUILD_TESTS=ON"])
    run(["cmake", "--build", "--preset", "release", "--parallel"])
    run(["ctest", "--test-dir", "build/release", "-C", "Release", "--output-on-failure", "--no-tests=error", "--timeout", "90"])

    exe_path = REPO / "build" / "release" / "Release" / "Sakura.exe"
    if not exe_path.is_file():
        raise RuntimeError(f"Release executable missing: {exe_path}")

    stamp = datetime.now().strftime("%Y%m%d-%H%M%S")
    qa_dir = REPO / "build" / "qa" / f"autopilot-{stamp}"
    if qa_dir.exists():
        shutil.rmtree(qa_dir)
    run([str(exe_path), "--smoke-test", str(qa_dir), "--size", "1600", "900", "--verify-play"])
    verify_online_if_present(exe_path, qa_dir)
    run([sys.executable, "scripts/package_release.py", "--build-dir", "build/release", "--replace"])


def set_release_channel(channel: str) -> None:
    if channel == "stable":
        run([sys.executable, "scripts/version_hook_entry.py", "tag", "clear"])
    elif channel in ("dev", "alpha", "beta", "rc"):
        hooks_file = REPO / "VERSION_HOOKS.json"
        hooks = json.loads(hooks_file.read_text(encoding="utf-8"))
        if channel not in hooks.get("available_tags", []):
            run([sys.executable, "scripts/version_hook_entry.py", "tag", "add", channel])
        run([sys.executable, "scripts/version_hook_entry.py", "tag", "set", channel])
    else:
        raise ValueError(f"Unsupported channel: {channel}")


def watch_remote_release(target_tag: str) -> None:
    print(f"[autopilot] Waiting for GitHub Actions Build workflow for tag {target_tag}...", flush=True)
    run_id = None
    for _ in range(30):
        res = run(
            ["gh", "run", "list", "--workflow", "Build", "--limit", "10", "--json", "databaseId,headBranch,status,conclusion"],
            capture=True,
        )
        runs = json.loads(res.stdout or "[]")
        for item in runs:
            if item.get("headBranch") == target_tag:
                run_id = item["databaseId"]
                break
        if run_id is not None:
            break
        time.sleep(3)

    if run_id is None:
        raise RuntimeError(f"Could not locate GitHub Actions workflow run for tag {target_tag}.")

    print(f"[autopilot] Watching workflow run {run_id} for {target_tag}...", flush=True)
    watch = run(["gh", "run", "watch", str(run_id), "--exit-status"], check=False)
    if watch.returncode != 0:
        run(["gh", "run", "view", str(run_id), "--log-failed"], check=False)
        raise RuntimeError(f"GitHub Actions run {run_id} failed for {target_tag}.")

    for _ in range(12):
        rel = run(["gh", "release", "view", target_tag, "--json", "tagName,isPrerelease,assets,url"], check=False, capture=True)
        if rel.returncode == 0 and rel.stdout.strip():
            data = json.loads(rel.stdout)
            assets = [a["name"] for a in data.get("assets", [])]
            if assets:
                print(f"[autopilot] Verified GitHub Release {target_tag}: {data.get('url')} assets={assets}", flush=True)
                return
        time.sleep(5)
    raise RuntimeError(f"GitHub Release {target_tag} missing or has no uploaded assets.")


def perform_release(args: argparse.Namespace) -> int:
    if not args.skip_verify:
        verify_local()

    set_release_channel(args.channel)
    run(["git", "add", "-A"])

    full_msg = args.message.strip()
    if CO_AUTHOR not in full_msg:
        full_msg = f"{full_msg}\n\n{CO_AUTHOR}"

    run(["git", "commit", "-m", full_msg], env=build_env({"VERSION_HOOK_BUMP": args.bump}))

    version_data = json.loads((REPO / "VERSION.json").read_text(encoding="utf-8"))
    actual_tag = version_data.get("tag", "")
    if actual_tag != args.target_tag:
        raise RuntimeError(f"Expected VERSION.json tag {args.target_tag!r}, got {actual_tag!r}.")

    run(["git", "rev-parse", "--verify", f"refs/tags/{args.target_tag}"])
    run(["git", "push", "origin", "main"])
    run(["git", "push", "origin", args.target_tag])
    watch_remote_release(args.target_tag)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--status", action="store_true", help="Show current version and roadmap release status")
    parser.add_argument("--verify-local", action="store_true", help="Run CMake Release build, CTest, smoke-test, and packaging")
    parser.add_argument("--release", action="store_true", help="Verify, bump version, tag, push, and await GitHub Release")
    parser.add_argument("--target-tag", type=str, help="Expected release tag, e.g. v0.8.0-alpha")
    parser.add_argument("--channel", choices=["dev", "alpha", "beta", "rc", "stable"], default="alpha")
    parser.add_argument("--bump", choices=["patch", "minor", "major"], default="minor")
    parser.add_argument("--message", type=str, default="")
    parser.add_argument("--skip-verify", action="store_true", help="Skip local build/test gate before committing")
    args = parser.parse_args()

    if args.status:
        return show_status()
    if args.verify_local:
        verify_local()
        return 0
    if args.release:
        if not args.target_tag or not args.message:
            parser.error("--release requires --target-tag and --message")
        return perform_release(args)
    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
