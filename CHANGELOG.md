# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## Tags / Releases

Every versioned section below maps 1:1 to a Git tag named `vX.Y.Z`.
When a GitHub Release exists, it uses the same tag.
On tags where the CI workflow is active, release binaries are generated from CI.

Current tag/release map:
- `v0.1.4` (2026-02-28): [tag][tag-v0.1.4] + [release][release-v0.1.4] + CI assets (`UhfWrapper-x64.zip`, `UhfWrapper-x86.zip`)
- `v0.1.3` (2026-02-28): [tag][tag-v0.1.3] + [release][release-v0.1.3] + CI assets (`UhfWrapper-x64.zip`, `UhfWrapper-x86.zip`)
- `v0.1.2` (2026-02-28): [tag][tag-v0.1.2] + [release][release-v0.1.2] + CI assets (`UhfWrapper-x64.zip`, `UhfWrapper-x86.zip`)
- `v0.1.1` (2026-02-28): [tag][tag-v0.1.1] + [release][release-v0.1.1] (no binary assets attached)
- `v0.1.0` (2026-01-04): [tag][tag-v0.1.0] only (no GitHub Release)

## [Unreleased]

### Added

### Changed

### Fixed

## [0.1.4] - 2026-02-28

### Added

### Changed
- Clarify API return conventions in docs: most command functions are `1=success / 0=failure`, while value getters return `>=0` value or `-1` on failure.
- Clarify `UHF_GetLastError()` signature in docs (`const char*`, no output buffer parameter).

### Fixed

## [0.1.3] - 2026-02-28

### Added
- Document the complete tag/release mapping in the changelog and add a direct changelog link in README.

### Changed
- `UHF_GetStatus` now reports `present/connected` from the open handle state when already open, to avoid disruptive USB re-enumeration in the critical `Open -> StartRead` path.

### Fixed
- Prevent `UHF_IsReaderPresent` and `UHF_IsConnected` from triggering USB-count side effects while the reader handle is already open.

## [0.1.2] - 2026-02-28

### Added
- Add GitHub Actions workflow to build x64/x86 on push/PR and publish release assets on tags.
- Add README badges for workflow status and latest release.

### Changed
- CMake now supports CI builds without vendor import libraries by generating a minimal DEF (`UHF_*` exports only).

### Fixed

## [0.1.1] - 2026-02-28

### Added
- Add CLI troubleshooting guidance for lingering select masks and USB-over-IP polling intervals.

### Changed
- `read-count` now uses safe pop internally (stop/pop/restart) for better reliability on some firmware/transport combinations.

### Fixed
- Improve tag parsing for `CT` framed inventory responses (`cmd=0x01`) in addition to active data frames (`cmd=0x45`).
- Add a one-time retry path for `SWHid_StartRead` to reduce transient read-start failures.
- Clear lingering selection before calibration and improve calibration power-set retry/error details.

## [0.1.0] - 2026-01-04

### Added
- Add project memory system in `docs/project_notes/` with ADRs, bug log, key facts, and work log.
- Add `CLAUDE.md` and update `docs/AGENTS.md` to keep memory protocols consistent.
- Add WorkMode helper APIs and automatic Answer/Active switching for user-friendly `UHF_*` calls.
- Add calibration persistence and calibrated read APIs + CLI commands.
- Add Transport helpers (`UHF_GetTransport`, `UHF_SetTransport`, `UHF_SetTransportUsb`, `UHF_EnsureUsbTransport`).
 - Add public API/CLI documentation (`API.md`, `CLI.md`) and expand README.
 - Add `UHF_GetStatus` with enriched CLI `info` output (power, transport, workmode, freq region).

### Changed
- Update documentation references to include the new changelog and memory notes.
- Calibration now uses software EPC filtering to avoid firmware blocking power changes.
- Calibration power sweep now runs from max → min and requires all reads-per-step to pass.
- Calibration now rejects duplicate calibration EPCs when multiple TIDs are detected.
- Calibration skips duplicate check if the reader ignores mask select (best-effort).
- User-friendly calls now enforce Transport=USB when possible (auto-fix).
 - Improve CLI `info` output to be user-friendly when RSSI filter is disabled.

### Fixed
- Parse Answer-mode tag frames even when the vendor buffer reports zero tags.

[Unreleased]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/compare/v0.1.4...HEAD
[0.1.4]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.4
[0.1.3]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.3
[0.1.2]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.2
[0.1.1]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.1
[0.1.0]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/tree/v0.1.0

[tag-v0.1.4]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/tree/v0.1.4
[release-v0.1.4]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.4
[tag-v0.1.3]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/tree/v0.1.3
[release-v0.1.3]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.3
[tag-v0.1.2]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/tree/v0.1.2
[release-v0.1.2]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.2
[tag-v0.1.1]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/tree/v0.1.1
[release-v0.1.1]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases/tag/v0.1.1
[tag-v0.1.0]: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/tree/v0.1.0
