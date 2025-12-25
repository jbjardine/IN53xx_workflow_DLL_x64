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

### Changed
- Update documentation references to include the new changelog and memory notes.
- Calibration now uses software EPC filtering to avoid firmware blocking power changes.
