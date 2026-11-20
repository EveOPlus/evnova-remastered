#!/usr/bin/env python3
"""Package the complete generated artwork, separately from small engine updates."""

import argparse
import hashlib
import json
from pathlib import Path
import struct
import zipfile
import zlib

ROOT = Path(__file__).resolve().parents[1]
PACK_ID = "original-4x-v1"


def make_graphics_package(source: Path, output: Path, root: Path = ROOT) -> dict:
    manifest = json.loads((source / "manifest.json").read_text())
    frames = manifest["frames"]
    keys = {frame["key"] for frame in frames}
    files = {path.name: path for path in source.glob("*.evs")}
    if len(keys) != len(frames) or {key + ".evs" for key in keys} != files.keys():
        raise ValueError("The generated pack must contain every manifest frame, without extras")
    for frame in frames:
        data = files[frame["key"] + ".evs"].read_bytes()
        signature, width, height = struct.unpack(">4sHH", data[:8])
        if (signature != b"EVS1" or width != frame["width"] * 4 or
                height != frame["height"] * 4 or
                len(zlib.decompress(data[8:])) != width * height * 4):
            raise ValueError("Invalid reconstructed frame: " + frame["key"])
    for name in ("manifest.json", "provenance.json", "Real-ESRGAN-LICENSE.txt"):
        files[name] = source / name
    for name in ("CREDITS.md", "NOTICE.md"):
        files[name] = root / name
    output.parent.mkdir(parents=True, exist_ok=True)
    temporary = output.with_suffix(".zip.part")
    # EVS files are already compressed. Stable ordering/timestamps make this
    # reproducible without adding the large derivatives to the source tree.
    try:
        with zipfile.ZipFile(temporary, "w", zipfile.ZIP_STORED) as archive:
            for name, path in sorted(files.items()):
                info = zipfile.ZipInfo(name, (2026, 1, 1, 0, 0, 0))
                archive.writestr(info, path.read_bytes())
        temporary.replace(output)
    finally:
        temporary.unlink(missing_ok=True)
    with output.open("rb") as packed:
        digest = hashlib.file_digest(packed, "sha256").hexdigest()
    return dict(id=PACK_ID, archive=output.name, size=output.stat().st_size,
                sha256=digest,
                file_count=len(files), expanded_bytes=sum(p.stat().st_size for p in files.values()))


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--source", type=Path, default=ROOT / "EV Nova/Remastered Sprites")
    parser.add_argument("--output", type=Path, default=ROOT / "dist/evnova-graphics-original-4x-v1.zip")
    args = parser.parse_args()
    print(json.dumps(make_graphics_package(args.source, args.output), indent=2))


if __name__ == "__main__":
    main()
