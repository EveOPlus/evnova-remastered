#!/usr/bin/env python3
"""Build an engine-only Windows release; never copy the local playable package."""

from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import tempfile
import zipfile

ROOT = Path(__file__).resolve().parents[1]
DOCUMENTS = (
    "LICENSE.txt", "CREDITS.md", "NOTICE.md", "README.md",
    "docs/game_data_setup.md", "docs/modern_controls.md", "docs/arpia2.md",
    "docs/narration.md", "docs/remastered_graphics.md", "docs/local_windows_build.md",
    "tools/setup-game-data.ps1", "tools/game-downloads.json",
    "tools/setup-narration.ps1", "tools/narration-downloads.json", "tools/nova_narrator.py",
    "tools/update-game.ps1", "docs/automatic_updates.md",
    "src/third_party/README.md", "src/third_party/dr_mp3.h", "src/third_party/pl_mpeg.h",
)
REQUIRED_NOTICES = {"sdl3", "sdl3-ttf", "freetype", "harfbuzz", "fmt", "libpng", "zlib"}


def notice_files(root: Path, dependency_share: Path) -> dict[str, Path]:
    files = {"licenses/" + p.name: p for p in (root / "licenses").glob("*.txt")}
    files["licenses/README.md"] = root / "licenses/README.md"
    current = {p.parent.name: p for p in dependency_share.glob("*/copyright")}
    missing = REQUIRED_NOTICES - current.keys()
    if missing:
        raise ValueError("Missing build dependency notices: " + ", ".join(sorted(missing)))
    # Prefer the notices for the actual linked build over the source snapshot.
    files.update({"licenses/" + name + ".txt": p for name, p in current.items()})
    return files


def make_package(root: Path, binary: Path, dependency_share: Path, output: Path,
                 build_info: dict | None = None) -> None:
    if build_info is None:
        build_info = json.loads((binary.resolve().parents[1] / "build-info.json").read_text())
    version = (root / "VERSION").read_text().strip()
    if build_info["version"] != version or build_info["probe"] != "OFF":
        raise ValueError("Package requires a matching versioned player build with its test driver disabled")
    files = {name: root / name for name in DOCUMENTS}
    files["evnova.exe"] = binary
    files.update(notice_files(root, dependency_share))
    for name, source in files.items():
        if not source.is_file():
            raise ValueError(f"Missing package file: {name}")
    output.parent.mkdir(parents=True, exist_ok=True)
    # An explicit file list keeps game data, caches, saves and models out even
    # when building from a developer's populated checkout.
    with tempfile.TemporaryDirectory(prefix="evnova-release-", dir=output.parent) as temporary:
        archive = Path(temporary) / "release.zip"
        payloads = {name: source.read_bytes() for name, source in sorted(files.items())}
        payloads["START HERE.txt"] = (
                "EV Nova Community Remaster - unofficial community preview\n"
                "Developed with AI tooling, including OpenAI Codex.\n\n"
                "Extract this whole folder, then run evnova.exe.\n"
                "Choose Download and Set Up to fetch Community Edition and ARPIA2.\n"
                "Keep the tools folder beside the game. Internet: about 127 MB.\n"
                "You can also locate an existing EV Nova.exe installation.\n\n"
                "New releases download automatically and install on your next launch.\n"
                "Set Prefs > Extra Prefs controls automatic updates.\n"
                "Set Prefs > Narration > Install voices adds all four local voices.\n\n"
                "See CREDITS.md, NOTICE.md and licenses for authors and terms.\n"
                "No original game data or optional voice models are bundled.\n"
                "Source and help: https://github.com/EveOPlus/evnova-remastered\n"
            ).encode("utf-8")
        manifest = dict(schema=1, repository="EveOPlus/evnova-remastered",
                        version=version, build=build_info,
                        files=[dict(path=name, sha256=hashlib.sha256(data).hexdigest())
                               for name, data in sorted(payloads.items())])
        payloads["release.json"] = json.dumps(manifest, indent=2).encode("utf-8")
        with zipfile.ZipFile(archive, "w", zipfile.ZIP_DEFLATED) as package:
            for name, data in payloads.items():
                package.writestr("EV Nova Remastered/" + name, data)
        archive.replace(output)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--binary", type=Path, default=ROOT / "build/release/src/evnova.exe")
    parser.add_argument("--dependency-share", type=Path)
    parser.add_argument("--output", type=Path, default=ROOT / "dist/evnova-windows-x64.zip")
    args = parser.parse_args()
    share = args.dependency_share
    if share is None:
        cache = args.binary.resolve().parents[1] / "CMakeCache.txt"
        values = {}
        if cache.is_file():
            for line in cache.read_text(encoding="utf-8").splitlines():
                if "=" in line and not line.startswith(("//", "#")):
                    key, value = line.split("=", 1)
                    values[key.split(":", 1)[0]] = value
        if values.get("EVNOVA_ENABLE_PROBE") != "OFF":
            parser.error("Use a release build with EVNOVA_ENABLE_PROBE=OFF")
        installed = Path(values.get("VCPKG_INSTALLED_DIR", ROOT / "vcpkg_installed"))
        share = installed / values.get("VCPKG_TARGET_TRIPLET", "x64-windows-static") / "share"
    make_package(ROOT, args.binary, share, args.output)
    print(f"Created engine-only release: {args.output}")


if __name__ == "__main__":
    main()
