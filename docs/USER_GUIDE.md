# UHF Wrapper User Guide

## What this project provides
`UhfWrapper.dll` is a native Windows wrapper around the vendor `SWHidApi.dll`.

You get:
- A stable `UHF_*` API for integration code
- A CLI (`UhfWrapperCli.exe`) for diagnostics and field checks
- Optional .NET P/Invoke bindings

## Prerequisites
- Windows 10/11
- CMake + Visual Studio Build Tools
- Vendor `SWHidApi.dll` obtained from the vendor SDK (not distributed here)

## Build
Run:
```powershell
cmake -S src/uhf_wrapper -B build-x64 -A x64
cmake --build build-x64 --config Release
```

Then:
```powershell
cmake -S src/uhf_wrapper -B build-x86 -A Win32
cmake --build build-x86 --config Release
```

## Runtime DLL loading
By default, the wrapper expects `SWHidApi.dll` next to `UhfWrapper.dll`.

To override the vendor DLL location, set an **absolute** path:
```powershell
set UHF_VENDOR_DLL=C:\full\path\to\SWHidApi.dll
```

## Quick CLI checks
Run:
```powershell
UhfWrapperCli.exe --friendly status
UhfWrapperCli.exe --friendly read-once
UhfWrapperCli.exe --friendly read-stream --duration 2000
```

Then:
```powershell
UhfWrapperCli.exe --friendly select-clear
UhfWrapperCli.exe --friendly --interval 1000 --timeout 10000 read-count 1
```

This clears lingering selection masks and uses safer timing for unstable USB-over-IP links.

## API behavior notes
- Action-style APIs return `1` on success and `0` on failure.
- Value-returning APIs return `>= 0` on success and `-1` on failure.
- Use `UHF_GetLastError()` and `UHF_GetLastErrorCode()` after failures.
- `UHF_*` is designed to keep x64/x86 surface parity.

## Transport and work mode
Reader transport must be USB/HID for `UHF_*` operations.

Helpers:
- `UHF_GetTransport`
- `UHF_SetTransport`
- `UHF_SetTransportUsb`
- `UHF_EnsureUsbTransport`

Work mode helpers:
- `UHF_GetWorkMode`
- `UHF_SetWorkModeAnswer`
- `UHF_SetWorkModeActive`

## Write safety
Use select and target flows for multi-tag safety:
```powershell
UhfWrapperCli.exe --friendly select-epc <EPC_HEX>
UhfWrapperCli.exe --friendly write-epc <NEW_EPC_HEX> 00000000
UhfWrapperCli.exe --friendly select-clear
```

## Calibration
You can calibrate power and RSSI for a known target tag:
```powershell
UhfWrapperCli.exe --friendly calib-prepare
UhfWrapperCli.exe --friendly calib-run --calib-min 0 --calib-max 26 --calib-step 1 --calib-reads 3 --calib-pwr-margin 2 --calib-capture 8000 --calib-rssi-margin 3 --apply
```

## Reference docs
- `README.md`
- `API.md`
- `CLI.md`
- `NOTICE_VENDOR.md`
