# Automatic updates and build versions

Windows release packages check the public `EveOPlus/evnova-remastered` GitHub
releases on launch. Checks and downloads run in a hidden helper while the game
continues. A verified download is installed on the next launch, before the game
opens. Source commits alone do not trigger an update: publish a newer numbered
release with `evnova-windows-x64.zip` attached.

**Set Prefs > Extra Prefs > Automatic updates (Windows)** turns this off or on.
Turning it off also defers a downloaded update. Offline or failed checks leave
the current version playable. The helper reports status in
`.evnova-update/status.txt` beside the executable. Read-only installations need
to be moved to a writable folder, or updated by extracting the release manually.

Everyone uses one stream of regular `major.minor.patch` releases, starting with
`1.0.0`. Completed, tested changes on `main` are published with a new version;
unfinished source changes do not update players. The updater ignores drafts,
prereleases, preview tags and releases without a completed Windows asset and
SHA-256 digest. Comparisons are numeric; it never installs an equal or older
version. Existing `0.1.0-preview.2` installations can upgrade directly to `1.0.0`.

Downloads use HTTPS and are restricted to this repository. Both the archive
SHA-256 supplied by GitHub and the packaged file hashes must match. Archive
entries are limited to engine files, helpers, documentation and notices. Paths
that escape the package, duplicate entries and symlink/junction destinations are
rejected. Pilots, preferences, original game data, plug-ins and narration
runtimes are not update targets. A failed file replacement rolls back prior
replacements; an interrupted transaction is recovered on the next attempt.
Updates never kill an active game session.

## Identifying a build

The main-menu footer displays the community version and a short source build
ID. Startup logs and `evnova.exe --version` include the full revision, build
configuration and test-driver status. `release.json` records the version,
revision and hashes of the package contents. The Windows executable is unsigned.

`VERSION` is the version source. CMake embeds it into the executable and
writes `build-info.json`; the public packager rejects a mismatched version or
a test-driver build. Normal source builds do not self-update. Developer builds
report their revision with a dirty suffix when appropriate; release builds must
be built from the published commit. `tools/build-windows.ps1 -Revision <commit>`
supports the separate local build workspace. Do not reuse a released version
number for a different binary.

## Publishing an update

1. Bump `VERSION`, finish validation and commit the source.
2. Build with that source revision; run `python tools/package_release.py`.
3. Create a draft tagged `v<VERSION>` with **pre-release unchecked** and upload
   the complete ZIP. Publish it as **Latest** only after the upload finishes.
4. Confirm the asset's GitHub SHA-256 matches the locally packaged ZIP.

The public download is always available at
`https://github.com/EveOPlus/evnova-remastered/releases/latest/download/evnova-windows-x64.zip`.
Player installations, including the maintainer's downloaded copy, receive
updates through this same release process. Local development packages must not
replace them or redirect their shortcuts.

The first updater-capable build is `0.1.0-preview.2`. Older installations require
one manual download or local installation of that version before they can
receive future updates automatically. GitHub authentication is not required
for players.


## Enhanced artwork delivery

From 1.0.1, public Windows builds automatically install the complete 8,188-frame
stock/ARPIA2 graphics pack before play, when Remastered graphics is enabled.
The approximately 555 MB companion asset is pinned by URL and SHA-256 in
`tools/game-downloads.json`. It lives in shared support data, survives engine
updates and is not downloaded again per release. The first-launch screen shows
progress, Retry and Continue for now for offline use.

Keep the engine ZIP's historical allowlist unchanged: older installed updaters
must be able to accept the new executable and existing setup helper filenames.
Publish the graphics companion before making its referencing engine release
Latest. Later releases can reuse that immutable graphics URL. To replace the
artwork, generate a new pack ID, update both the paths constant and manifest,
and validate and upload its companion before publishing. Never remove assets
still pinned by a supported release. See [graphics](remastered_graphics.md).
