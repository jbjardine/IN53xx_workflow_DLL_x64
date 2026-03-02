# Project Structure

This document describes the repository layout and the purpose of each top‑level area.

```
.
├─ docs/                # Public guides and technical docs
├─ src/uhf_wrapper/     # Native wrapper, CLI, and .NET P/Invoke
├─ vendor/              # Vendor SDK (local copy)
├─ build-x64/           # Local build outputs (x64)
├─ build-x86/           # Local build outputs (x86)
└─ README.md            # Project overview
```

## Core Components

- `src/uhf_wrapper/uhf_wrapper.cpp` / `uhf_wrapper.h`
  - Main implementation of the `UHF_*` API.
  - Dynamically loads vendor `SWHidApi.dll` and re‑exports vendor symbols.
  - Adds user‑friendly helpers, error handling, and buffer parsing.

- `src/uhf_wrapper/uhf_cli.cpp`
  - Command‑line tool (UhfWrapperCli.exe) for diagnostics and testing.

- `src/uhf_wrapper/dotnet/`
  - Minimal .NET P/Invoke projects for x64/x86.

- `docs/USER_GUIDE.md`
  - End‑user guide (CLI + API usage).

## Build Outputs

- Built artifacts are created under `build-x64/` and `build-x86/`.
- The wrapper DLL is self‑contained and re‑exports all vendor functions.
- The vendor `SWHidApi.dll` must be placed next to the wrapper (or overridden via `UHF_VENDOR_DLL`).
