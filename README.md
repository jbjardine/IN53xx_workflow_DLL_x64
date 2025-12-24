# IN53xx UHF Reader Wrapper (UhfWrapper)

[![GitHub](https://img.shields.io/badge/GitHub-jbjardine%2FIN53xx__workflow__DLL__x64-blue?logo=github)](https://github.com/jbjardine/IN53xx_workflow_DLL_x64)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.x-064F8C?logo=cmake)](https://cmake.org/)
[![.NET](https://img.shields.io/badge/.NET-P%2FInvoke-512BD4?logo=dotnet)](https://dotnet.microsoft.com/)
[![Windows](https://img.shields.io/badge/Windows-x64%20%7C%20x86-0078D6?logo=windows)](https://www.microsoft.com/windows)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Overview

This repository provides a **native Windows wrapper** around the vendor `SWHidApi.dll` for IN53xx / Impinj R2000 UHF RFID readers (HID mode). It exposes:

- A **stable, user‑friendly `UHF_*` API** (x64 + x86, identical surface)
- A **1:1 re‑export** of all vendor functions (`SWHid_*`), same names/signatures
- A **CLI** for diagnostics and production testing
- A **.NET P/Invoke layer** for easy integration in C# apps

The goal is to make RFID operations **fast to integrate** (WinDev, C/C++, .NET, etc.) while keeping **full access** to the original vendor SDK.

## Key Features

- **Cross‑arch parity**: x64 and x86 expose the same `UHF_*` functions
- **User‑friendly helpers**: read/stream, buffer pop/peek, whitelist management
- **Safe writes**: single‑tag check + read‑back verification (override with `--force`)
- **Select + Write multi‑tag**: mask selection is enforced for targeted writes
- **GPIO/Relay** support when available in the vendor DLL
- **Robust error reporting**: `UHF_GetLastError()` + `UHF_GetLastErrorCode()`

## Requirements

- Windows 10/11
- Vendor SDK DLL `SWHidApi.dll` (x64 and/or x86)
- CMake + Visual Studio build tools (for compiling)

## Quick Start

### Build (x64)

```powershell
cmake -S src/uhf_wrapper -B build-x64 -A x64
cmake --build build-x64 --config Release
```

### Build (x86)

```powershell
cmake -S src/uhf_wrapper -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

Outputs (Release):

- `UhfWrapper.dll`
- `UhfWrapperCli.exe`
- `SWHidApi.dll` (copied when available)

> You can override the vendor DLL path at runtime:
>
> ```powershell
> set UHF_VENDOR_DLL=C:\path\to\SWHidApi.dll
> ```

## CLI Examples

```powershell
UhfWrapperCli.exe --friendly status
UhfWrapperCli.exe --friendly read-once
UhfWrapperCli.exe --friendly read-stream --duration 2000

# Select a tag and write EPC (multi‑tag safe)
UhfWrapperCli.exe --friendly select-epc <EPC_HEX>
UhfWrapperCli.exe --friendly write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly select-clear

# One‑shot write with safety (blocks if multiple tags detected)
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> --force write-epc <NEW_EPC_HEX> 00000000

# Targeted memory write (same safety rules)
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> write 3 0 11223344 00000000
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> --force write 3 0 11223344 00000000
```

## API Highlights

- **Connection**: `UHF_Init`, `UHF_Open`, `UHF_Close`, `UHF_IsOpen`, `UHF_IsConnected`
- **Tag ops**: `UHF_ReadTag`, `UHF_WriteTag`, `UHF_WriteEpc`, `UHF_WriteEpcSelected`,
  `UHF_WriteTagSelected`, `UHF_SelectEpc`, `UHF_ClearSelect`
- **Buffer**: `UHF_PeekBuffer*`, `UHF_PopBuffer*` (safe variants)
- **Power/Frequency**: `UHF_GetPowerDbm/Pct`, `UHF_SetPowerDbm/Pct`, `UHF_GetFreq`, `UHF_SetFreq`
- **Whitelist**: `UHF_WhitelistCount`, `UHF_WhitelistGetRaw/Hex`, `UHF_WhitelistAddEpc`,
  `UHF_WhitelistRemoveEpc`, `UHF_WhitelistClear`
- **Advanced**: `UHF_ModuleCommand` (if vendor exports it)

For full details, see `docs/USER_GUIDE.md`.

## .NET Wrapper

Minimal P/Invoke projects are provided:

- `src/uhf_wrapper/dotnet/UhfWrapperNet.csproj` (x64)
- `src/uhf_wrapper/dotnet/UhfWrapperNet.x86.csproj` (x86)

## Project Structure

```
.
├─ docs/                # Guides and agent notes
├─ exemple/             # WinDev examples
├─ src/uhf_wrapper/     # Core wrapper + CLI + .NET
├─ vendor/              # Vendor SDK (private/local copy)
└─ build-*              # Build outputs (local)
```

## Documentation

- User guide: `docs/USER_GUIDE.md`
- Change log: `history.md`
- Structure: `docs/STRUCTURE.md`
- Architecture: `docs/ARCHITECTURE.md`

## License

MIT. See `LICENSE`.
