# UHF Wrapper (x64)

This module builds `UhfWrapper.dll`, a native x64 wrapper around the vendor `SWHidApi.dll`.

Goals:
- Expose a simple, stable `UHF_*` API for WinDev and other languages.
- Re-export **all** vendor exports 1:1 (same names), so callers can use them without
  loading `SWHidApi.dll` directly.

## Build (Windows)
```
cd src/uhf_wrapper
mkdir build
cd build
cmake -A x64 ..
cmake --build . --config Release
```

Outputs (Release):
- `UhfWrapper.dll`
- `UhfWrapperCli.exe`
- `SWHidApi.dll` (copied from the vendor SDK when present)

### x86 build (optional)
```
cd src/uhf_wrapper
mkdir build_x86
cd build_x86
cmake -A Win32 ..
cmake --build . --config Release
```

The build copies the **x86** vendor `SWHidApi.dll` next to the wrapper when present.
If the SDK DLL is missing, copy it manually at runtime.

## Notes
- `UHF_LockTag` is **disabled** because the vendor does not document the signature of
  `SWHid_LockCardG2`. Call the vendor export directly (re-exported 1:1) once you know
  the correct signature.
- Tag buffer parsing follows the same layout used in the WinDev example.
- Relay and frequency helpers are provided (`UHF_RelayOn/Off`, `UHF_GetFreq/SetFreq`).

## .NET (optional)
`src/uhf_wrapper/dotnet/UhfWrapperNet.csproj` (x64) and
`src/uhf_wrapper/dotnet/UhfWrapperNet.x86.csproj` (x86) provide a minimal .NET wrapper (P/Invoke)
for the `UHF_*` API plus common vendor functions. All vendor exports are still available
via the native re-export.
