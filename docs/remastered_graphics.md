# Remastered graphics

Enable or disable **Remastered graphics** under **Set Prefs > Extra Prefs**.
It defaults on, applies immediately and persists in the active campaign's
`EV Nova Extra Prefs.ini` (`remastered_graphics=1` or `0`). Older preference
files default to on. Classic mode restores the original flight presentation.

The original cockpit PICTs, government colours, icons, hulls, planets, stations,
animations and hit geometry remain in use. Hull and stellar textures have a
four-times-resolution reconstruction derived from their original frames. This
infers cleaner edges and surface detail; it does not recover lost source models.

- Three stable parallax star layers, sized in screen pixels at each zoom.
- Subdued, cached nebula textures using the scenario's space colour, or a
  neutral blue-grey when space is untinted. No new faction colour schemes.
- Soft bloom drawn from the actual stellar, engine, running-light, weapon and
  impact artwork; existing visibility, opacity, cloak and fog gates remain.
- Linear sampling for the enhanced flight sprites.
- Reconstructed hull, alternate hull and stellar frames, including stock and
  ARPIA2 artwork. The original palette, silhouettes, frame count, anchors and
  animation timing are retained. Fog/emergence masks use the enhanced alpha.

## Sharper ships and planets

`sprite_enhancement.*` keeps the decoded original artwork and selects an
enhanced texture only in remastered mode. Public Windows builds automatically
install the complete 8,188-frame pack from the companion GitHub release asset
into the shared support folder's `Remastered Sprites/original-4x-v1`. A progress
screen shows download and installation; Retry handles failures, and Continue
for now allows offline play with the fallback and retries next launch. The pack
is about 555 MB and is retained across engine updates. Existing local packs at
`EV Nova/Remastered Sprites` remain supported.
The pack uses Real-ESRGAN x4plus with 20% of a bicubic reconstruction of the
original blended back in to retain markings and texture. Output alpha is
restricted to the original silhouette's sampling footprint. The lower-resolution
collision masks, world sizes, and animation logic are unchanged.

Assets are keyed by CRC32 + Adler32 of the original RGBA and its dimensions,
not by resource ID, so plug-ins that replace stock IDs cannot receive unrelated
art. The bounded loader validates dimensions, compressed size and output length.
Missing, damaged or unsupported assets fall back to a three-times bicubic
reconstruction with conservative interior sharpening and premultiplied-alpha
interpolation. Frames above 512 pixels retain their original textures.

Only visible frames are uploaded. A per-flight-view 96 MiB LRU cache holds
enhanced colour and matching white masks; original textures remain available
for immediate classic-mode switching. There is no inference during gameplay,
and the normal package needs neither the model nor a particular GPU.

To reproduce the pack on Windows:

1. Build release tests; run `evnova_tests.exe '[.sprite-export]'` from the root.
2. Repeat in a separate process with `EVN_SPRITE_EXPORT_ARPIA=1`. This uses an
   isolated export profile, not the player's profile.
3. Extract the official [Real-ESRGAN ncnn Vulkan release](https://github.com/xinntao/Real-ESRGAN/releases/tag/v0.2.5.0)
   into `.cache/sprite-upscaler` and install Pillow into its `python` subfolder.
4. Run `python tools/build_sprite_pack.py`. It deduplicates source frames,
   reconstructs them using GPU 0, and writes the compressed EVS1 pack and
   source manifest. `--prepare-only` and `--pack-only` support inspection.
5. Run `python tools/package_graphics.py` to validate every frame and build the
   companion release ZIP with provenance, tool license and original-art credits.
   Publish it at the immutable URL pinned by `tools/game-downloads.json`.
   The engine ZIP retains its existing allowlist so older updaters can migrate.
   `setup-game-data.ps1 -GraphicsOnly` verifies the archive hash, count, total
   size and entry names before installation; a receipt is written last.

The four-byte EVS1 signature is followed by big-endian 16-bit width/height and
zlib-compressed straight RGBA8. Each output is exactly four times the source
dimensions. Asset pack inputs and visual comparisons are under
`analysis/sprite-detail`; `evnova_tests.exe '[.sprite-preview]'` renders a
side-by-side comparison through the actual game's sprite draw path.

`remastered_graphics.*` is a port-only presentation layer with no original
function counterpart. Its stateless visual hash is independent of the game's
random stream. The original ambient-star pool continues its usual updates even
when the enhanced background replaces its draw, preserving simulation RNG
cadence. Negative murk still hides ambient scenery. Textures are cached per
system and space colour, with ordinary sprite draws as the failure fallback.
The basic effects require no shaders or additional assets. The sharper artwork
pack is downloaded automatically by public Windows releases, separately from
small engine updates. Both campaigns share the same installed pack.

The integration retains the reconstructed paths documented in
[ship_sprite_rendering_path.md](ship_sprite_rendering_path.md) and
[system_murk_rendering.md](system_murk_rendering.md). Archived Ghidra views of
`0x00497df0`, `0x0042e590`, `0x00428340` and `0x0042cd10` supplied the render
boundaries; live Ghidra was unavailable during this change. These optional
effects do not increase reconstruction percentages.

Tests cover stable scenery across system changes, hidden starfields,
transparent/fully fogged bloom, and preference migration/round-trip. Headless
driver captures and checks are stored under `analysis/remaster/`.
Sprite-detail tests cover transparent RGB contamination, constant colours,
placement/fog/classic parity, cache eviction, content matching, and damaged-pack
fallback. Archived `0x00474ab0` / `0x00474e90` and the ship/stellar rendering
notes establish the original loader and geometry boundaries; the optional
reconstruction does not change port percentages. Live Ghidra was unavailable.
