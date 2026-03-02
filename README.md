# IN53xx UHF Reader Wrapper (UhfWrapper)

[![GitHub](https://img.shields.io/badge/GitHub-jbjardine%2FIN53xx__workflow__DLL__x64-blue?logo=github)](https://github.com/jbjardine/IN53xx_workflow_DLL_x64)
[![Build And Release](https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/workflows/build-and-release.yml/badge.svg)](https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/workflows/build-and-release.yml)
[![Release](https://img.shields.io/github/v/release/jbjardine/IN53xx_workflow_DLL_x64)](https://github.com/jbjardine/IN53xx_workflow_DLL_x64/releases)
[![C++](https://img.shields.io/badge/C%2B%2B-17-00599C?logo=c%2B%2B)](https://isocpp.org/)
[![CMake](https://img.shields.io/badge/CMake-3.x-064F8C?logo=cmake)](https://cmake.org/)
[![.NET](https://img.shields.io/badge/.NET-P%2FInvoke-512BD4?logo=dotnet)](https://dotnet.microsoft.com/)
[![Windows](https://img.shields.io/badge/Windows-x64%20%7C%20x86-0078D6?logo=windows)](https://www.microsoft.com/windows)
[![License: MIT](https://img.shields.io/badge/License-MIT-yellow.svg)](https://opensource.org/licenses/MIT)

## Overview

Native Windows wrapper around the vendor `SWHidApi.dll` for IN53xx / Impinj R2000 UHF RFID readers (HID mode).
The goal is a stable, user-friendly API (`UHF_*`) with identical x64/x86 surface, plus a CLI for diagnostics.
All vendor functions are re-exported 1:1 so you can call `SWHid_*` without loading the vendor DLL directly.

## Key Features

- Same `UHF_*` API for x64 and x86 (one surface, two builds)
- Re-export of vendor SDK (same names/signatures)
- CLI for production checks and diagnostics
- Safe writes (single-tag guard + read-back verification, force override available)
- Select + Write for multi-tag targeting
- Anti-double read window (dedup by time, optional EPC+antenna key)
- RSSI filter (software)
- Auto-calibration (power sweep + RSSI window, optional apply)
- WorkMode handled automatically in user-friendly calls
- USB transport guard with auto-switch when possible

## Requirements

- Windows 10/11
- Vendor `SWHidApi.dll` (x64 and/or x86) — not shipped in this public repo
- CMake + Visual Studio Build Tools

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

> Override vendor DLL path at runtime (absolute `.dll` path required):
>
> ```powershell
> set UHF_VENDOR_DLL=C:\path\to\SWHidApi.dll
> ```

## Documentation

- API reference: `API.md`
- CLI reference: `CLI.md`
- Changelog: `CHANGELOG.md`

## CLI Examples

```powershell
UhfWrapperCli.exe --friendly status
UhfWrapperCli.exe --friendly read-once
UhfWrapperCli.exe --friendly read-stream --duration 2000
UhfWrapperCli.exe --friendly rssi-filter --rssi-min -65 --rssi-max -40

# Select a tag and write EPC (multi-tag safe)
UhfWrapperCli.exe --friendly select-epc <EPC_HEX>
UhfWrapperCli.exe --friendly write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly select-clear

# One-shot write with safety (blocks if multiple tags detected)
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly --target <EPC_ACTUEL> --force write-epc <NEW_EPC_HEX> 00000000
```

Note: `info` JSON/CSV includes `freqRegion` (e.g., `EU`, `US`).

If reads unexpectedly return 0 tags:

```powershell
# Clear any lingering EPC mask selection
UhfWrapperCli.exe --friendly select-clear

# On USB-over-IP (VirtualHere), use a slower polling loop
UhfWrapperCli.exe --friendly --interval 1000 --timeout 10000 read-count 1
UhfWrapperCli.exe --friendly --interval 1000 --duration 8000 read-stream
```

## API Reference (Friendly `UHF_*`)

The public, stable surface is the `UHF_*` API (same for x64/x86). For exact
signatures, see `src/uhf_wrapper/uhf_wrapper.h`.

Core / lifecycle:
- `UHF_Init`, `UHF_Shutdown`
- `UHF_Open`, `UHF_Close`
- `UHF_IsReaderPresent`, `UHF_IsOpen`, `UHF_IsConnected`
- `UHF_GetLastError`, `UHF_GetLastErrorCode`

Read / buffer:
- `UHF_StartRead`, `UHF_StopRead`, `UHF_ClearBuffer`
- `UHF_PeekBufferAll`, `UHF_PeekBufferDedup`
- `UHF_PopBufferAll`, `UHF_PopBufferDedup`
- `UHF_PopBufferAllSafe`, `UHF_PopBufferDedupSafe`
- `UHF_PopBufferDedupFiltered`
- `UHF_DedupWindowSet`, `UHF_DedupWindowReset`, `UHF_DedupKeySet`
- `UHF_ReadOnce`
- `UHF_ReadOnceCalibrated`, `UHF_ReadStreamCalibrated`

Filters / power:
- `UHF_RssiFilterSet`, `UHF_RssiFilterReset`
- `UHF_GetPowerDbm`, `UHF_SetPowerDbm`
- `UHF_GetPowerPct`, `UHF_SetPowerPct`

Select / write / lock:
- `UHF_SelectEpc`, `UHF_ClearSelect`
- `UHF_ReadTag`, `UHF_WriteTag`
- `UHF_WriteEpc`
- `UHF_WriteEpcSelected`, `UHF_WriteTagSelected` (safe multi-tag write)
- `UHF_LockTag`

Calibration:
- `UHF_CalibrationTagPrepare`
- `UHF_CalibrateByTag`
- `UHF_CalibrationApply`
- `UHF_CalibrationGetCurrent`
- `UHF_CalibrationSave`, `UHF_CalibrationLoad`

I/O / misc:
- `UHF_RelayOn`, `UHF_RelayOff`, `UHF_Relay2On`, `UHF_Relay2Off`
- `UHF_Out1On`, `UHF_Out1Off`, `UHF_Out2On`, `UHF_Out2Off`
- `UHF_WhitelistCount`, `UHF_WhitelistGetHex`, `UHF_WhitelistAddEpc`,
  `UHF_WhitelistRemoveEpc`, `UHF_WhitelistClear`
- `UHF_GetTransport`, `UHF_SetTransport`, `UHF_SetTransportUsb`, `UHF_EnsureUsbTransport`
- `UHF_GetWorkMode`, `UHF_SetWorkModeAnswer`, `UHF_SetWorkModeActive`

### Vendor 1:1 (re-export)

All `SWHid_*` vendor functions are re-exported with identical names/signatures
so you can call them without loading the vendor DLL directly. See
`src/uhf_wrapper/UhfWrapper.def` and `src/uhf_wrapper/uhf_wrapper.h`.

### Return Codes (common)

```
UHF_ERR_OK (0)
UHF_ERR_INVALID_ARG (-2)
UHF_ERR_VENDOR_MISSING (-3)
UHF_ERR_VENDOR_CALL_FAILED (-4)
UHF_ERR_NOT_SUPPORTED (-5)
UHF_ERR_NOT_OPEN (-6)
UHF_ERR_NOT_CONNECTED (-7)
UHF_ERR_NO_DEVICE (-8)
UHF_ERR_MULTI_TAG (-9)
UHF_ERR_VERIFY_FAILED (-10)
UHF_ERR_ALREADY_READING (-11)
UHF_ERR_NOT_READING (-12)
UHF_ERR_NO_TAG (-13)
UHF_ERR_CALIBRATION_FAILED (-14)
UHF_ERR_CALIBRATION_MISSING (-15)
```

## CLI Reference (Friendly)

Use `UhfWrapperCli.exe --help` for the full command list and options.
Outputs support `--json`, `--csv`, `--text`.

Common commands:
- `status`, `open`, `close`, `info`, `usbinfo`
- `read-once`, `read-stream`, `read-count`
- `read-once-calib`, `read-stream-calib`
- `power-get`, `power-set`, `power-get-pct`, `power-set-pct`
- `rssi-filter`, `rssi-reset`
- `select-epc`, `select-clear`
- `write-epc` (with `--target` and optional `--force`)
- `calib-prepare`, `calib-run`
- `whitelist-count`, `whitelist-add`, `whitelist-del`, `whitelist-clear`

## API Examples

Anti-double (dedup window):

```c
UHF_DedupWindowSet(3000);   // 3 seconds
UHF_DedupKeySet(0);         // 0=EPC only, 1=EPC+antenna
UHF_PopBufferDedupFiltered(tags, 256, &count);
```

RSSI filter (software):

```c
UHF_RssiFilterSet(-65, -40); // keep only tags in [-65..-40] dBm
UHF_RssiFilterReset();       // disable filter
```

## Auto-Calibration (how it works)

### What it does

1) Choose a target EPC (single-tag or explicit EPC when multiple tags).
2) Sweep RF power from max to min in steps.
3) At each step, read the tag multiple times (Active + buffer for reliability).
4) Keep the lowest power that still detects the tag consistently.
5) Capture RSSI stats at the recommended power and compute filter window.
6) Optionally apply power + RSSI filter to the reader.

Calibration filters by EPC in software (no hardware mask) to avoid some firmwares
rejecting `SetPowerDbm` while a mask is active.

### DLL usage

```c
char calibEpc[128] = {0};
UHF_CalibrationResult res{};

// Use existing single tag as calibration target
UHF_CalibrationTagPrepare(nullptr, 0, calibEpc, sizeof(calibEpc));

// Sweep power and capture RSSI, then apply settings
UHF_CalibrateByTag(calibEpc, 0, 26, 1, 3, 2, 8000, 3, 1, &res);
```

### CLI usage

```powershell
UhfWrapperCli.exe --friendly calib-prepare
UhfWrapperCli.exe --friendly calib-run --calib-min 0 --calib-max 26 --calib-step 1 --calib-reads 3 --calib-pwr-margin 2 --calib-capture 8000 --calib-rssi-margin 3 --apply
```

If multiple tags are present, pass a target EPC:

```powershell
UhfWrapperCli.exe --friendly --calib-epc <EPC_HEX> calib-run --calib-min 0 --calib-max 26 --calib-step 1 --calib-reads 3 --calib-pwr-margin 2 --calib-capture 8000 --calib-rssi-margin 3 --apply
```

## Interface / Transport (USB HID)

The reader must be in USB/HID for `UHF_*` calls.

Transport param (index `0x01`, vendor protocol V1.9):
- 0 = USB
- 1 = RS232/RS485
- 2 = RJ45
- 3 = WIFI
- 4 = Weigand

Note: ReaderSoft `SystemConfig.ini` uses its own mapping (e.g., `Transport=1`) which
is not the same as the device parameter `0x01`.

Helpers:

```c
UHF_GetTransport();       // read raw value
UHF_SetTransport(v);      // set raw value if you know mapping
UHF_SetTransportUsb();    // attempt to switch to USB
UHF_EnsureUsbTransport(); // verify + switch to USB if possible
```

If the device is in Weigand (WG26/WG34), switch back to USB via ReaderSoft or helper.

## Known Limitations / Notes

- Some firmwares return 0 tags in Answer/InventoryG2; user-friendly reads fall back to Active + buffer.
- A lingering EPC mask selection can hide tags from generic reads; call `select-clear` before diagnostics.
- On USB-over-IP links, faster poll loops can be unstable; prefer `--interval 500..1000`.
- Calibration uses software EPC filtering (no hardware mask) to avoid `SetPowerDbm` failures on some devices.
- Trigger mode is not supported by the current vendor firmware/EXE.
- If a vendor call is missing in x64, the wrapper returns a clear “x86 required” error for parity.

## Project Structure

```
IN53xx_workflow_DLL_x64/
├── src/uhf_wrapper/        # wrapper source (DLL + CLI)
│   ├── uhf_wrapper.cpp
│   ├── uhf_wrapper.h
│   ├── UhfWrapper.def
│   └── uhf_cli.cpp
├── docs/                   # public documentation
├── vendor/                 # vendor SDK and DLLs (ignored in public repo)
├── build-x64/              # x64 build output
├── build-x86/              # x86 build output
└── README.md
```

## Tests (optional)

Unit tests are lightweight and skip if no reader is connected:

```powershell
cmake -S src/uhf_wrapper -B build-x64 -A x64 -DUHF_BUILD_TESTS=ON
cmake --build build-x64 --config Release
ctest --test-dir build-x64 -C Release
```

## CI / Release Artifacts

- GitHub Actions builds x64 + x86 on `push`/`PR` and for tags `v*`.
- On tag builds, the workflow uploads release assets:
  - `UhfWrapper-x64.zip`
  - `UhfWrapper-x86.zip`
- CI builds use minimal exports (`UHF_*` only) so they do not require vendor `.lib` files.
- Runtime still requires providing the vendor `SWHidApi.dll`.

## License

MIT. See `LICENSE`.

## Project Policies

- Security reporting: `SECURITY.md`
- Contribution process: `CONTRIBUTING.md`
- Code of conduct: `CODE_OF_CONDUCT.md`
- Support channels: `SUPPORT.md`
- Vendor/legal notice: `NOTICE_VENDOR.md`

## Support

Open an issue: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/issues
