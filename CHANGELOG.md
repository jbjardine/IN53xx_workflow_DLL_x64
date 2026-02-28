# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added

### Changed

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
