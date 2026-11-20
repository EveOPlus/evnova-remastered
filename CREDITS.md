# Credits and provenance

EV Nova Community Remaster is an unofficial community fork, maintained under
**EveOPlus**. It builds
on other people's engine reconstruction, original game, tools and artwork.
The names below acknowledge their work; they do not imply endorsement.

## Engine foundation

**Lancelot de Ferrière (wraitii)** created
[evnova-decomp](https://github.com/wraitii/evnova-decomp), the C++23/SDL3 engine
and reverse-engineering foundation used here. This fork began at commit
[`e3dd30407939296bf19e96f94a499cc6bb2cafd1`](https://github.com/wraitii/evnova-decomp/commit/e3dd30407939296bf19e96f94a499cc6bb2cafd1).
Its original Git history and [MIT copyright notice](LICENSE.txt) are retained.
The engine's implementation coverage and research notes are principally inherited
from that project, not work newly created for this fork.

EveOPlus's additions include modern mouse controls, radial actions, navigation
assistance, market comparison, local narration integration, ARPIA2 compatibility,
Windows packaging and remastered presentation.

## AI tooling disclosure

This fork is developed using AI tooling, including **OpenAI Codex**, for code
generation and modification, investigation, documentation, and test execution.
Both this fork and the upstream project use AI-assisted work. These tools do
not replace attribution to the original game creators, upstream engine authors,
or other contributors. Behavioral checks do not establish complete
original-game fidelity.

## Escape Velocity Nova

Credit **Matt Burch**, **ATMOS**, **Ambrosia Software**, and the original game's
artists, writers, musicians, testers and other contributors. The original game,
story, imagery, sound, fonts and names are not this project's original content.
Original documentation is retained by the data installer.

The Windows compatibility reference and downloadable data come from
**EV Nova Community Edition** and the maintainers of
[escape-velocity.games](https://escape-velocity.games/). Their Community Edition
Read Me is kept with downloaded data. Their archive hosting is independent of
this fork; availability is not a grant of this repository's MIT license to
those assets.

## ARPIA2

**Peter “Pace” Craddock** created ARPIA2 (copyright 2004–2010), with the **ARPIA
Team**. The following credits are drawn from the supplied *ReadMe (do, even
cheaters).rtf*, sections 5 and 6. The installer retains that entire document,
its license section, the extras ReadMe, warning and walkthrough documents.

| Contributor | Contribution credited by ARPIA2 |
| --- | --- |
| Peter “Pace” Craddock | Campaign creation, writing and implementation |
| Steve “-8-” Chick (ATMOS) | Arpian ship artwork |
| Ed “(The Real) Darth Bob” Paget | Landing artwork for Nova Bruocsella/UVP-018, Hogun and Culuria |
| Scott “Eyeya” Vardy (ATMOS) | Arpian ship sketches |
| Greg “Insomniac” / “persondude” Haworth | Planet artwork revisions and designs |
| Jonas “Modesty” / “Modesty Blaise” Waage | Outfits, ships, Kilmura gas miner and animation, explosions and other artwork |
| Pieris | Feature and mission ideas |
| Dafydd “pipeline” Williams (ATMOS) | Logo, ships and collaboration with ATMOS artists |
| Philip “pstan” / “particlestan” Chick (ATMOS) | Arpian ship artwork |
| Prime | Hogun ship concepts |
| Joe “Requiem” | Original interface and planet artwork |
| Mark Hettenbach | Reset Missions v9 resources used with permission by ARPIA2 |
| Christopher Buchholz / Terek E ' Ban | PlugPack Extra Outfits resources and imagery |
| Agee / MacDevil | Officers concepts and Frandall resurfacing idea |
| ArcAngel Counterstrike | Advanced Solar Panels resource and image |
| Hudson | Hogun transmission formatting assistance |
| Andrew “LiAnNaSu” Moscoe | Spaceball assistance |
| Belthazar, slouch and Guy | PC/Mac testing; Guy also contributed officer-image work and a launcher |
| Rik “rmx256” Gresham | Development support |
| Jacabyte | Community assistance |
| Philip, Pace's brother | Testing and feedback |
| General11, kitkut, prophile and Steelix | Development support |
| Aelran, bmac, Darth Bob, Darth Eddy, Prime, Requiem, Swithich and stlyesom | Early EV:Genesis team input |
| Ralph and Roger Sutherland | Mish and DeRLE tools |
| Mark Chai | EVN Bits Bible |
| David Arthur | Mission Computer editor |

ARPIA2 also thanks its early players, supporters, testers and community. Its
ReadMe reserves reproduction rights and asks users to contact the creators
before reusing graphics. This repository distributes compatibility code, not
ARPIA2's resources, movies or artwork. The download helper obtains the original
archive from the user-requested community mirror; it does not relicense it.

## Libraries shipped as source or linked into the engine

| Project | Authors / maintainers | License / notice |
| --- | --- | --- |
| [SDL3](https://github.com/libsdl-org/SDL) | Sam Lantinga and SDL contributors | zlib; `licenses/sdl3.txt` |
| [SDL3_ttf](https://github.com/libsdl-org/SDL_ttf) | SDL_ttf contributors | zlib; `licenses/sdl3-ttf.txt` |
| [FreeType](https://freetype.org/) | The FreeType Project authors | FreeType License / GPL alternative; `licenses/freetype.txt` |
| [HarfBuzz](https://github.com/harfbuzz/harfbuzz) | HarfBuzz contributors | MIT-style notices; `licenses/harfbuzz.txt` |
| [fmt](https://github.com/fmtlib/fmt) | Victor Zverovich and contributors | MIT; `licenses/fmt.txt` |
| [Catch2](https://github.com/catchorg/Catch2) | Phil Nash and Catch2 contributors | BSL-1.0; test dependency; `licenses/catch2.txt` |
| [dr_mp3](https://github.com/mackron/dr_libs) | David Reid | Public domain / MIT-0; notice retained in the vendored header |
| [minimp3](https://github.com/lieff/minimp3) | lieff and contributors | CC0; used within dr_mp3, with its notice retained |
| [pl_mpeg](https://github.com/phoboslab/pl_mpeg) | Dominic Szablewski | MIT; notice retained in the vendored header |
| [libpng](http://www.libpng.org/), [zlib](https://zlib.net/), [Brotli](https://github.com/google/brotli), [bzip2](https://sourceware.org/bzip2/) | Their respective authors and contributors | Full dependency notices in `licenses/` |

The vendored decoder revisions and hashes are recorded in
[src/third_party/README.md](src/third_party/README.md). The `licenses/` directory
contains the exact notices from the pinned local dependency build; release
packaging also collects the notices from the dependencies used for that build.

## Optional tools and separately downloaded models

- [Piper](https://github.com/OHF-Voice/piper1-gpl): Open Home Foundation,
  Michael Hansen and contributors; GPL-3.0. The narration helper launches a
  separately installed speech runtime. Piper binaries and voice weights are
  not in this repository or the engine-only release.
- Piper voices: **Lessac**, **Ryan**, **Alba** and **Alan**, and their dataset
  authors, speakers and model contributors. Their individual `MODEL_CARD`
  files are downloaded with each voice. Ryan's card identifies CC BY-NC-SA
  4.0 data; Alba's identifies CC BY 4.0 data and Cori Samuel's base voice;
  Lessac and Alan point to their dataset terms. Those terms are separate from
  the engine license. See [narration setup](docs/narration.md).
- [Real-ESRGAN](https://github.com/xinntao/Real-ESRGAN): Xintao Wang and
  contributors (BSD-3-Clause), with
  [Real-ESRGAN-ncnn-vulkan](https://github.com/xinntao/Real-ESRGAN-ncnn-vulkan)
  and [ncnn](https://github.com/Tencent/ncnn) contributors. Used for the local
  optional sprite enhancement workflow; model binaries and generated game
  artwork are not included. [Pillow](https://python-pillow.github.io/) supplies
  image preparation and packing.
- [FFmpeg](https://ffmpeg.org/): the FFmpeg developers. Used, when installed,
  to create local movie companions. Licensing depends on the selected FFmpeg
  build; this repository does not bundle FFmpeg or converted game movies.
- [Ghidra](https://github.com/NationalSecurityAgency/ghidra): NSA and Ghidra
  contributors (Apache-2.0); used for reverse-engineering research.
- ResForge, the EV Nova Bible, original pilot-format references, and other
  community resource editors/documentation supported the reconstruction.
  Local reference archives are not included in this source repository.
- [vcpkg](https://github.com/microsoft/vcpkg), CMake, Ninja, LLVM/Clang,
  Microsoft Visual C++, Python and their contributors provide build/test tools.

## Scope of the license

[LICENSE.txt](LICENSE.txt) covers the engine source as described by its original
MIT notice. Third-party code retains its own notices. Original game data,
ARPIA2, fonts, voice models and enhanced derivatives of game artwork retain
separate terms and are not made MIT-licensed by attribution or by this fork.
