# GitHub workflow instructions

The root release policy applies. Public updates are completed, tested regular
versions from `main`, not moving nightly tags or prereleases.

- `workflows/ci.yml` builds Linux, macOS and Windows. It runs the selected
  hermetic runtime checks, packages Windows and macOS artifacts, and keeps
  public player builds at `EVNOVA_ENABLE_PROBE=OFF`.
- `workflows/distribute.yml` is manually dispatched and requires a successful
  trusted main CI run plus configured Apple signing/notarization credentials.
  It is not an enabled promise of automatic Windows publishing on every push.
- Preserve the source-run/repository/branch validation before consuming an
  artifact with write permissions or secrets. Preserve resumable notarization,
  obsolete-source checks and credential cleanup.
- Read the version/revision from the built package. Publish `v<VERSION>` as a
  normal Latest release only after upload/hash verification. Never overwrite a
  published version with different binaries; bump `VERSION` for a new update.
- Do not publish game data, voices, pilots, private analysis or build credentials
  as workflow artifacts or release assets. Use the existing packagers.
- Limit permissions per job, keep third-party action changes deliberate, and
  validate YAML plus scripts before pushing. A parsed workflow is not an
  executed CI/notarization run; report that distinction.

See [release documentation](../docs/automatic_updates.md) and the
[agent handoff](../docs/agent_handoff.md) for the local Windows release route.
