# Architecture Overview

This wrapper is a thin native layer on top of the vendor HID DLL (`SWHidApi.dll`).
It provides a stable, friendly API and re‑exports the vendor surface 1:1.

## Layers

1) **Vendor SDK (SWHidApi.dll)**
   - Implements HID communication with the reader.
   - Exposes low‑level `SWHid_*` functions.

2) **UhfWrapper (native C++)**
   - Dynamically loads the vendor DLL.
   - Re‑exports **all** vendor functions unchanged.
   - Adds the `UHF_*` API for safer, higher‑level usage.
   - Provides consistent error reporting.

3) **CLI + .NET**
   - `UhfWrapperCli.exe` drives the wrapper for diagnostics and production testing.
   - .NET P/Invoke projects expose the same `UHF_*` API for C# apps.

## Data Flow (typical)

```
App / CLI / .NET
        │
        ▼
   UHF_* API (wrapper)
        │
        ▼
  SWHid_* API (vendor DLL)
        │
        ▼
      Reader
```

## Select + Write (Multi‑Tag Safe)

- The wrapper applies **mask selection** through `SWHid_SetDeviceSpecialParam`.
- This forces **access ops** (write/read/lock) to target only the selected EPC.
- The old raw mask frame path is kept as a fallback for incomplete SDKs.

## Error Handling

- `UHF_GetLastError()` returns a human‑readable message.
- `UHF_GetLastErrorCode()` returns a stable numeric error code.
- When vendor exports are missing, the wrapper returns `UHF_ERR_NOT_SUPPORTED`.

## Compatibility Notes

- Some vendor functions differ between x86 and x64 naming.
- The wrapper normalizes these differences internally.
- x86 + x64 present the same `UHF_*` surface area for applications.
