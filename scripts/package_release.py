"""Build a self-contained Windows x64 ZIP from a completed CMake Release build.

Only explicit game assets are copied; player data and developer configuration
never enter the package. Uses Python's standard library.
"""
from __future__ import annotations

import argparse
import hashlib
import json
import os
from pathlib import Path
import shutil
import subprocess
import tempfile
import zipfile

REPO = Path(__file__).resolve().parents[1]


def digest(path: Path) -> str:
    value = hashlib.sha256()
    with path.open("rb") as stream:
        for block in iter(lambda: stream.read(1024 * 1024), b""):
            value.update(block)
    return value.hexdigest()


def cache_value(cache: Path, key: str) -> str:
    for line in cache.read_text(encoding="utf-8").splitlines():
        if line.startswith(key + ":"):
            return line.split("=", 1)[1]
    return ""


def runtime_directory(build: Path, requested: Path | None) -> Path:
    if requested:
        return requested.resolve()
    installations = []
    compiler = cache_value(build / "CMakeCache.txt", "CMAKE_CXX_COMPILER")
    if compiler:
        for parent in Path(compiler).parents:
            if parent.name == "VC":
                installations.append(parent.parent)
    vswhere = Path(os.environ.get("ProgramFiles(x86)", "C:/Program Files (x86)")) / "Microsoft Visual Studio/Installer/vswhere.exe"
    if vswhere.is_file():
        found = subprocess.check_output(
            [str(vswhere), "-latest", "-products", "*", "-requires", "Microsoft.VisualStudio.Component.VC.Tools.x86.x64", "-property", "installationPath"],
            text=True, encoding="utf-8",
        ).strip()
        if found:
            installations.append(Path(found))
    for installation in installations:
        candidates = list((installation / "VC/Redist/MSVC").glob("*/x64/Microsoft.VC143.CRT"))
        numbered = [p for p in candidates if p.parents[1].name[0].isdigit()]
        if numbered:
            return max(numbered, key=lambda p: tuple(int(x) for x in p.parents[1].name.split(".")))
    raise RuntimeError("MSVC x64 runtime not found; pass --crt-dir <Microsoft.VC143.CRT directory>.")


def package(args: argparse.Namespace) -> Path:
    build = args.build_dir.resolve()
    binary = build / "Release/Sakura.exe"
    if not binary.is_file():
        raise RuntimeError(f"Build Release first: {binary}")
    installed = cache_value(build / "CMakeCache.txt", "VCPKG_INSTALLED_DIR")
    dependencies = Path(installed) / "x64-windows" if installed else build / "vcpkg_installed/x64-windows"
    if not (dependencies / "share").is_dir():
        raise RuntimeError(f"Dependency notices not found: {dependencies}")
    runtime = runtime_directory(build, args.crt_dir)
    for name in ("msvcp140.dll", "vcruntime140.dll", "vcruntime140_1.dll"):
        if not (runtime / name).is_file():
            raise RuntimeError(f"Incomplete x64 MSVC runtime: {runtime / name}")

    version = json.loads((REPO / "VERSION.json").read_text(encoding="utf-8"))["tag"].removeprefix("v")
    name = f"Sakura-{version}-windows-x64"
    output = args.output_dir.resolve()
    output.mkdir(parents=True, exist_ok=True)
    destination = output / name
    if destination.exists():
        if not args.replace or not (destination / "release-manifest.json").is_file() or (destination / "userdata").exists():
            raise RuntimeError(f"Output already exists or contains player data: {destination}. Use a new output directory, or --replace for an untouched generated package.")

    with tempfile.TemporaryDirectory(prefix=".sakura-package-", dir=output) as temporary:
        stage = Path(temporary) / name
        stage.mkdir()
        shutil.copy2(binary, stage / "Sakura.exe")
        for library in sorted(binary.parent.glob("*.dll")):
            if library.stem.lower().endswith("d") and library.stem.lower().startswith(("msvcp", "vcruntime", "ucrtbase")):
                raise RuntimeError(f"Debug runtime found in Release output: {library}")
            shutil.copy2(library, stage / library.name)
        for library in sorted(runtime.glob("*.dll")):
            shutil.copy2(library, stage / library.name)
        shutil.copytree(REPO / "resources", stage / "resources", ignore=shutil.ignore_patterns("windows", "*.rc"))
        (stage / "config").mkdir()
        shutil.copy2(REPO / "config/achievements.json", stage / "config/achievements.json")
        (stage / "doc").mkdir()
        for file in ("README.md", "THIRD_PARTY_NOTICES.md", "VERSION.json"):
            shutil.copy2(REPO / file, stage / file)
        for file in ("PLAYER_GUIDE.md", "CHART_FORMAT_SPEC.md", "DEPENDENCIES.md", "RELEASE_REVIEW_0.6.0.md", "release-validation.json"):
            shutil.copy2(REPO / "doc" / file, stage / "doc" / file)
        shutil.copytree(REPO / "doc/images", stage / "doc/images")
        notices = stage / "licenses"
        notices.mkdir()
        for notice in sorted((dependencies / "share").glob("*/copyright")):
            shutil.copy2(notice, notices / f"{notice.parent.name}.txt")
        shutil.copy2(REPO / "resources/fonts/OFL.txt", notices / "NotoSansSC-OFL.txt")
        if args.portable:
            (stage / "portable.txt").write_text("Player data is stored in ./userdata. Keep this directory writable.\n", encoding="utf-8")

        files = {path.relative_to(stage).as_posix(): digest(path) for path in sorted(stage.rglob("*")) if path.is_file()}
        manifest = {"version": version, "platform": "windows-x64", "portable": args.portable, "sha256": files}
        (stage / "release-manifest.json").write_text(json.dumps(manifest, ensure_ascii=False, indent=2) + "\n", encoding="utf-8")
        if destination.exists():
            # Both the direct-child path and generated marker were checked above.
            if destination.resolve().parent != output or destination.is_symlink():
                raise RuntimeError("Refusing to replace a redirected output directory.")
            shutil.rmtree(destination)
        stage.rename(destination)

    archive = output / f"{name}.zip"
    with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED, compresslevel=6) as bundle:
        for path in sorted(destination.rglob("*")):
            if path.is_file():
                bundle.write(path, (Path(name) / path.relative_to(destination)).as_posix())
    (output / f"{archive.name}.sha256").write_text(f"{digest(archive)}  {archive.name}\n", encoding="ascii")
    print(f"Package: {destination}")
    print(f"Archive: {archive} ({archive.stat().st_size / 1024 / 1024:.1f} MiB)")
    print(f"SHA256: {digest(archive)}")
    return archive


if __name__ == "__main__":
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--build-dir", type=Path, default=REPO / "build/release")
    parser.add_argument("--output-dir", type=Path, default=REPO / "build/dist")
    parser.add_argument("--crt-dir", type=Path)
    parser.add_argument("--portable", action="store_true", help="Store player data beside the executable instead of AppData")
    parser.add_argument("--replace", action="store_true", help="Replace a generated package only if it contains no userdata")
    try:
        package(parser.parse_args())
    except (OSError, RuntimeError, subprocess.SubprocessError) as error:
        parser.exit(1, f"Packaging failed: {error}\n")
