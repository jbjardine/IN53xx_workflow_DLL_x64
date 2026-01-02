# Changelog

All notable changes to this project will be documented in this file.

The format is based on [Keep a Changelog](https://keepachangelog.com/en/1.0.0/),
and this project adheres to [Semantic Versioning](https://semver.org/spec/v2.0.0.html).

## [Unreleased]

### Added
- Add project memory system in `docs/project_notes/` with ADRs, bug log, key facts, and work log.
- Add `CLAUDE.md` and update `docs/AGENTS.md` to keep memory protocols consistent.
- Add WorkMode helper APIs and automatic Answer/Active switching for user-friendly `UHF_*` calls.
- Add calibration persistence and calibrated read APIs + CLI commands.
- Add Transport helpers (`UHF_GetTransport`, `UHF_SetTransport`, `UHF_SetTransportUsb`, `UHF_EnsureUsbTransport`).

### Changed
- Update documentation references to include the new changelog and memory notes.
- Calibration now uses software EPC filtering to avoid firmware blocking power changes.
- Calibration power sweep now runs from max → min and requires all reads-per-step to pass.
- Calibration now rejects duplicate calibration EPCs when multiple TIDs are detected.
- Calibration skips duplicate check if the reader ignores mask select (best-effort).
- User-friendly calls now enforce Transport=USB when possible (auto-fix).

### Fixed
- Parse Answer-mode tag frames even when the vendor buffer reports zero tags.
