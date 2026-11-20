# Tooling instructions

Follow root instructions and [README.md](README.md). These tools can touch
downloads, installed data, profiles, binary analysis and public releases; keep
their scope explicit.

- Scripts need usable help/parameters, actionable failures and nonzero exit
  codes for failed required operations. Test Windows setup/update behavior in
  Windows PowerShell 5.1 as well as any newer shell used for development.
- Never embed account tokens or local credentials. Publishing tools obtain
  authorized credentials at runtime; do not print or commit them.
- Download helpers must validate hashes, extraction paths and required files.
  Keep originals and existing saves/plugins intact. Preserve downloaded credits.
- `package_release.py` uses an explicit engine-only allowlist. Do not turn it
  into a recursive copy of the working tree or local playable package.
- `package-windows.ps1` creates a private local package and changes a local
  shortcut. It must not be used to update the maintainer's public-download copy.
- `update-game.ps1` accepts only regular numbered releases and validates both
  archive and per-file hashes. Retain parsing of historical preview versions
  solely so those installations can migrate. Preserve rollback, process-exit
  coordination and the existing user-data exclusions.
- Python/PowerShell test fixtures use temporary directories with checked
  cleanup bounds. Never recursively delete or move a computed Windows path
  without verifying its resolved target stays inside the intended test root.
- `nova_driver.py` uses the separately built test executable and isolated
  profiles. Preserve ordinary input semantics and the player build's disabled
  listener. Do not work around a missing driver by controlling the desktop.
- Treat Ghidra bulk-edit and cleanup scripts as investigative tools, not routine
  setup. Inspect the script, target program and supporting evidence before
  writes. Keep port trackers and Ghidra annotations consistent.

Update the relevant user/developer guide when a tool contract changes. Do not
add a dependency merely to parse or copy a small standard format already handled
by the standard library.
