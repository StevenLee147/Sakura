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
    {"tag": "v1.3.0", "channel": "stable", "bump": "minor", "spec": "doc/roadmap/V1_3_GAMEPLAY_FEEL_AND_RECHART.md"},
    {"tag": "v1.4.0", "channel": "stable", "bump": "minor", "spec": "doc/roadmap/V1_4_COURSE_DAILY_AND_COLLECTIONS.md"},
    {"tag": "v1.5.0", "channel": "stable", "bump": "minor", "spec": "doc/roadmap/V1_5_SKINS_AND_STAGE_V4.md"},
    {"tag": "v1.6.0", "channel": "stable", "bump": "minor", "spec": "doc/roadmap/V1_6_GHOST_AND_MULTIPLAYER.md"},
    {"tag": "v1.7.0", "channel": "stable", "bump": "minor", "spec": "doc/roadmap/V1_7_PHIRA_CONVERTER_AND_VOL4.md"},
    {"tag": "v2.0.0", "channel": "stable", "bump": "major", "spec": "doc/roadmap/V2_0_SELF_UPDATER_AND_CAREER.md"},
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
    if not (REPO / "server" / "node_modules").is_dir():
        run([npm_exe, "--prefix", "server", "install", "--no-audit", "--no-fund"])
    run([npm_exe, "--prefix", "server", "run", "build"])
    run([npm_exe, "--prefix", "server", "test"])

    server_entry = REPO / "server" / "dist" / "index.js"
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
    proc = subprocess.Popen(
        ["node", str(server_entry)],
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

    print(f"[autopilot] Polling workflow run {run_id} for {target_tag}...", flush=True)
    deadline = time.time() + 1800.0
    conclusion = None
    while time.time() < deadline:
        probe = run(["gh", "run", "view", str(run_id), "--json", "status,conclusion"], check=False, capture=True)
        if probe.returncode == 0 and probe.stdout.strip():
            info = json.loads(probe.stdout)
            status = info.get("status")
            conclusion = info.get("conclusion")
            print(f"[autopilot]   run {run_id}: status={status} conclusion={conclusion}", flush=True)
            if status == "completed":
                break
        time.sleep(15)
    if conclusion != "success":
        run(["gh", "run", "view", str(run_id), "--log-failed"], check=False)
        raise RuntimeError(f"GitHub Actions run {run_id} finished with conclusion={conclusion!r} for {target_tag}.")

    for _ in range(12):
        rel = run(["gh", "release", "view", target_tag, "--json", "tagName,isPrerelease,assets,url"], check=False, capture=True)
        if rel.returncode == 0 and rel.stdout.strip():
            data = json.loads(rel.stdout)
            assets = [a["name"] for a in data.get("assets", [])]
            is_pre_tag = any(s in target_tag for s in ("-dev", "-alpha", "-beta", "-rc"))
            expected = [f"Sakura-{target_tag}-win64.zip"]
            if not is_pre_tag:
                expected.extend([
                    f"Sakura-{target_tag}-win64-setup.exe",
                    f"Sakura-{target_tag}-SHA256SUMS.txt",
                ])
            if all(req in assets for req in expected):
                if not is_pre_tag and data.get("isPrerelease") is not False:
                    raise RuntimeError(f"Expected stable release {target_tag} isPrerelease==False, got {data.get('isPrerelease')!r}")
                print(f"[autopilot] Verified GitHub Release {target_tag}: {data.get('url')} prerelease={data.get('isPrerelease')} assets={assets}", flush=True)
                return
        time.sleep(5)
    raise RuntimeError(f"GitHub Release {target_tag} missing or has no uploaded assets.")


def sync_server_version(target_tag: str) -> None:
    pkg_file = REPO / "server" / "package.json"
    if not pkg_file.is_file():
        return
    semver = target_tag[1:] if target_tag.startswith("v") else target_tag
    pkg_data = json.loads(pkg_file.read_text(encoding="utf-8"))
    if pkg_data.get("version") != semver:
        pkg_data["version"] = semver
        pkg_file.write_text(json.dumps(pkg_data, indent=2, ensure_ascii=False) + "\n", encoding="utf-8")
    app_ts = REPO / "server" / "src" / "app.ts"
    if app_ts.is_file():
        import re
        raw = app_ts.read_text(encoding="utf-8")
        updated = re.sub(
            r'(process\.env\.SAKURA_LATEST_VERSION\s*\|\|\s*")v[^"]+(")',
            rf'\g<1>{target_tag}\g<2>',
            raw,
        )
        if updated != raw:
            app_ts.write_text(updated, encoding="utf-8")


def deploy_production_server(target_tag: str) -> None:
    import io
    import ssl
    import tarfile
    import urllib.request

    server_dir = REPO / "server"
    if not (server_dir / "package.json").is_file():
        return
    ssh_host = os.environ.get("SAKURA_DEPLOY_SSH_HOST", "sakura-prod")
    public_health = os.environ.get("SAKURA_PUBLIC_HEALTH_URL", "https://steven-lee.tech:233/api/v1/health")

    npm_exe = "npm.cmd" if os.name == "nt" else "npm"
    run([npm_exe, "--prefix", "server", "run", "build"])

    buf = io.BytesIO()
    with tarfile.open(fileobj=buf, mode="w:gz") as tf:
        for item_name in ("package.json", "package-lock.json", "tsconfig.json", "deploy-systemd.sh", "src", "dist"):
            p = server_dir / item_name
            if p.exists():
                tf.add(str(p), arcname=item_name)
    archive_bytes = buf.getvalue()

    remote_cmd = (
        "set -euo pipefail; "
        "mkdir -p /srv/sakura-online; "
        "tar -xzf - -C /srv/sakura-online; "
        "sed -i 's/\\r$//' /srv/sakura-online/deploy-systemd.sh; "
        "chmod +x /srv/sakura-online/deploy-systemd.sh; "
        f"if [ -f /etc/systemd/system/sakura-online.service ]; then "
        f"sed -i 's/^Environment=SAKURA_LATEST_VERSION=.*/Environment=SAKURA_LATEST_VERSION={target_tag}/' /etc/systemd/system/sakura-online.service || true; "
        "systemctl daemon-reload || true; fi; "
        "bash /srv/sakura-online/deploy-systemd.sh"
    )
    print(f"[autopilot] Deploying server ({target_tag}) to {ssh_host}:/srv/sakura-online ...", flush=True)
    subprocess.run(
        ["ssh", "-o", "BatchMode=yes", "-o", "StrictHostKeyChecking=accept-new", "-o", "ConnectTimeout=15", ssh_host, remote_cmd],
        input=archive_bytes,
        cwd=REPO,
        check=True,
    )

    ctx = ssl.create_default_context()
    for _ in range(15):
        try:
            with urllib.request.urlopen(public_health, timeout=6.0, context=ctx) as resp:
                payload = json.loads(resp.read().decode("utf-8", errors="replace"))
                if payload.get("ok") is True and payload.get("version") == target_tag:
                    print(f"[autopilot] Verified production server healthy at {public_health}: {payload}", flush=True)
                    return
        except Exception:
            pass
        time.sleep(2.0)
    raise RuntimeError(f"Production server at {public_health} did not report version={target_tag!r} after SSH deploy.")


def perform_release(args: argparse.Namespace) -> int:
    sync_server_version(args.target_tag)
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
    if not args.skip_deploy_server:
        deploy_production_server(args.target_tag)
    return 0


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--status", action="store_true", help="Show current version and roadmap release status")
    parser.add_argument("--verify-local", action="store_true", help="Run CMake Release build, CTest, smoke-test, and packaging")
    parser.add_argument("--deploy-server", type=str, metavar="TAG", help="Deploy server/ to production SSH target and verify health")
    parser.add_argument("--release", action="store_true", help="Verify, bump version, tag, push, await GitHub Release, and deploy server")
    parser.add_argument("--target-tag", type=str, help="Expected release tag, e.g. v1.3.0")
    parser.add_argument("--channel", choices=["dev", "alpha", "beta", "rc", "stable"], default="stable")
    parser.add_argument("--bump", choices=["patch", "minor", "major"], default="minor")
    parser.add_argument("--message", type=str, default="")
    parser.add_argument("--skip-verify", action="store_true", help="Skip local build/test gate before committing")
    parser.add_argument("--skip-deploy-server", action="store_true", help="Skip SSH deployment to production server")
    args = parser.parse_args()

    if args.status:
        return show_status()
    if args.verify_local:
        verify_local()
        return 0
    if args.deploy_server:
        deploy_production_server(args.deploy_server)
        return 0
    if args.release:
        if not args.target_tag or not args.message:
            parser.error("--release requires --target-tag and --message")
        return perform_release(args)
    parser.print_help()
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
