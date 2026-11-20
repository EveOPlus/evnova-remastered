"""Build content-addressed flight artwork from exported original RGBA frames.

Requires Pillow and the official Real-ESRGAN ncnn Vulkan Windows release.
Run the hidden [.sprite-export] test once with stock data and again with
EVN_SPRITE_EXPORT_ARPIA=1 before this tool. Original resources are read-only.
"""
from __future__ import annotations

import argparse
import hashlib
import json
from pathlib import Path
import struct
import subprocess
import sys
import time
import zlib

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / '.cache/sprite-upscaler/python'))
from PIL import Image, ImageChops  # noqa: E402


def key_for(width: int, height: int, rgba: bytes) -> str:
    return f'{zlib.crc32(rgba):08x}{zlib.adler32(rgba):08x}-{width}x{height}'


def prepare(work: Path) -> list[dict]:
    inputs = work / 'inputs'
    inputs.mkdir(parents=True, exist_ok=True)
    frames: dict[str, dict] = {}
    for path in sorted((work / 'sources').glob('*.rgba')):
        data = path.read_bytes()
        w, h = struct.unpack('<II', data[:8])
        rgba = data[8:]
        if not (0 < w <= 512 and 0 < h <= 512 and len(rgba) == w * h * 4):
            raise ValueError(f'Invalid exported frame: {path}')
        key = key_for(w, h, rgba)
        if key in frames:
            frames[key]['sources'].append(path.name)
            continue
        frames[key] = dict(key=key, width=w, height=h, sources=[path.name],
                           source_sha256=hashlib.sha256(data).hexdigest())
        image = Image.frombytes('RGBA', (w, h), rgba)
        # Empty animation frames need no inference but remain in the pack.
        if image.getchannel('A').getextrema()[1] > 0:
            target = inputs / f'{key}.png'
            if not target.exists():
                image.save(target)
    manifest = list(frames.values())
    (work / 'manifest.json').write_text(json.dumps(manifest, indent=2), encoding='utf-8')
    return manifest


def pack(work: Path, destination: Path, manifest: list[dict]) -> None:
    destination.mkdir(parents=True, exist_ok=True)
    count = 0
    for item in manifest:
        key, w, h = item['key'], item['width'], item['height']
        original_data = (work / 'sources' / item['sources'][0]).read_bytes()
        original = Image.frombytes('RGBA', (w, h), original_data[8:])
        output_size = (w * 4, h * 4)
        if original.getchannel('A').getextrema()[1] == 0:
            result = Image.new('RGBA', output_size)
        else:
            with Image.open(work / 'outputs' / f'{key}.png') as generated:
                if generated.size != output_size:
                    raise ValueError(f'Unexpected output dimensions for {key}')
                # Retain source texture/markings while the reconstruction supplies
                # crisp edges. Pillow's RGBA resize handles premultiplied colour.
                faithful = original.resize(output_size, Image.Resampling.BICUBIC)
                result = Image.blend(generated.convert('RGBA'), faithful, 0.20)
                # Never create pixels beyond the source silhouette's sampling
                # footprint. Original collision masks remain untouched in-game.
                support = faithful.getchannel('A').point(lambda a: 255 if a else 0)
                alpha = result.getchannel('A')
                result.putalpha(ImageChops.multiply(alpha, support))
        raw = result.tobytes()
        payload = b'EVS1' + struct.pack('>HH', *output_size) + zlib.compress(raw, 6)
        temporary = destination / f'{key}.evs.tmp'
        temporary.write_bytes(payload)
        temporary.replace(destination / f'{key}.evs')
        count += 1
        if count % 500 == 0:
            print(f'Packed {count}/{len(manifest)} frames', flush=True)
    (destination / 'manifest.json').write_text(json.dumps({
        'format': 'EVS1', 'model': 'realesrgan-x4plus', 'scale': 4,
        'source_blend': 0.20, 'frames': manifest,
    }, indent=2), encoding='utf-8')
    print(f'Pack complete: {count} unique frames', flush=True)


def main() -> None:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--prepare-only', action='store_true')
    parser.add_argument('--pack-only', action='store_true')
    args = parser.parse_args()
    work = ROOT / 'analysis/sprite-detail'
    destination = ROOT / 'EV Nova/Remastered Sprites'
    manifest = prepare(work) if not args.pack_only else json.loads((work / 'manifest.json').read_text())
    print(f'Prepared {len(manifest)} unique frames', flush=True)
    if args.prepare_only:
        return
    if not args.pack_only:
        output = work / 'outputs'
        output.mkdir(exist_ok=True)
        # Resume only missing frames; retain completed outputs after interruption.
        pending = work / 'pending'
        pending.mkdir(exist_ok=True)
        for path in pending.glob('*.png'):
            if (output / path.name).exists():
                path.unlink()
        for path in (work / 'inputs').glob('*.png'):
            target = pending / path.name
            if not (output / path.name).exists() and not target.exists():
                target.hardlink_to(path)
        if any(pending.glob('*.png')):
            tool = ROOT / '.cache/sprite-upscaler/realesrgan-ncnn-vulkan.exe'
            with (work / 'inference.log').open('w', encoding='utf-8') as log:
                process = subprocess.Popen([
                    str(tool), '-i', str(pending), '-o', str(output),
                    '-m', str(tool.parent / 'models'), '-n', 'realesrgan-x4plus',
                    '-s', '4', '-g', '0', '-j', '2:2:2', '-f', 'png'],
                    stdout=log, stderr=log,
                    creationflags=getattr(subprocess, 'CREATE_NO_WINDOW', 0))
                try:
                    while process.poll() is None:
                        print(f'Reconstructed {len(list(output.glob("*.png")))}/{len(list((work / "inputs").glob("*.png")))} frames', flush=True)
                        time.sleep(15)
                    if process.returncode:
                        raise RuntimeError(f'Upscaler failed ({process.returncode}); see inference.log')
                finally:
                    if process.poll() is None:
                        process.terminate()
                        process.wait(timeout=10)
    pack(work, destination, manifest)


if __name__ == '__main__':
    main()
