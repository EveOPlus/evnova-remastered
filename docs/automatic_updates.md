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

Preview builds accept newer `major.minor.patch-preview.number` releases and
stable releases. Stable builds accept newer stable releases only. Comparisons
are numeric; the updater never installs an equal or older version. Drafts and
releases without a completed Windows asset and SHA-256 digest are ignored.

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
revision and hashes of the package contents. The Windows preview is unsigned.

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
3. Publish a release tagged `v<VERSION>` and upload the complete ZIP. Mark
   preview versions as prereleases. Publish only after the upload finishes.
4. Confirm the asset's GitHub SHA-256 matches the locally packaged ZIP.

The first updater-capable build is `0.1.0-preview.2`. Older installations require
one manual download or local installation of that version before they can
receive future updates automatically. GitHub authentication is not required
for players.
