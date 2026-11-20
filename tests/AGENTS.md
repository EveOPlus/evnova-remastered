# Test instructions

Follow the root instructions and [README.md](README.md). Tests link the real
runtime; distinguish data-free tests from original-resource and campaign tests.

- Add focused regression coverage for a demonstrated bug or meaningful complex
  behavior. Do not freeze speculative original behavior into expected results.
- Name the observable behavior and test completion, failure and cancellation
  where relevant. Automation tests should use real movement calculations where
  practical rather than only asserting private controller flags.
- Add new C++ test sources to `tests/CMakeLists.txt`. Catch2 filename tags allow
  focused execution without a full campaign/resource run.
- Keep original game assets, pilot fixtures and voice models untracked. A
  missing local fixture is a skipped/blocked check, not a successful assertion.
- Do not run stock and ARPIA2 resource suites in the same process; resource
  registration is process-wide. ARPIA2 tests are explicit opt-in.
- Keep tests that use fixed temporary names serial. Use unique isolated profile
  directories for driver scenarios; never use a player's live profile as a test
  fixture or modify their public installation.
- Do not broaden Defender exclusions or weaken checks to get a test to run.
  Report the exact environmental failure and preserve the unaffected work.
- Record the binary revision, filter, exit result and relevant scenario evidence.
  Clearly distinguish state/physics verification, rendered screenshots, generated
  audio and audio actually heard by a person.
