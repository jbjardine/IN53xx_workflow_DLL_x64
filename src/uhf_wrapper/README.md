# UHF Wrapper (x64/x86)

This module builds `UhfWrapper.dll`, a native x64/x86 wrapper around the vendor `SWHidApi.dll`.

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

If you override vendor loading with `UHF_VENDOR_DLL`, use an absolute `.dll` path.

## Notes
- `UHF_LockTag` uses the vendor signature (`lockCfg + pwd`). `lockCfg` is built from
  `lockType` and `lockMem` as `(lockMem << 4) | lockType`.
- Tag buffer parsing follows the same layout used in the WinDev example.
- Extra helpers: streaming + buffer pop/peek, whitelist management, relay/out controls,
  frequency helpers, and a raw `UHF_ModuleCommand` (when available).
- Transport (interface) helpers: `UHF_GetTransport`, `UHF_SetTransport`,
  `UHF_SetTransportUsb`, `UHF_EnsureUsbTransport` to switch the reader back to
  USB/HID when it was left in another interface (RJ45/Weigand/etc).
- Some vendor exports differ between x86/x64 (e.g. whitelist delete names). The wrapper
  handles both where possible. `UHF_ModuleCommand` is only available when the vendor
  DLL exports `SWHid_CommunicateWithModule`.
- On SDKs without `SWHid_ReadWhiteListCnt`, `UHF_WhitelistCount` falls back to
  enumerating entries until a read fails.
- Error handling: `UHF_GetLastError()` returns a text message and
  `UHF_GetLastErrorCode()` returns a numeric code.

## Documentation
User guide: `docs/USER_GUIDE.md`
Vendor notice: `NOTICE_VENDOR.md`

## High-level helpers (highlights)
- Buffer: `UHF_PeekBuffer*` / `UHF_PopBuffer*` (with optional safe stop/start).
- Whitelist: `UHF_WhitelistCount`, `UHF_WhitelistGet*`, `UHF_WhitelistAddEpc`,
  `UHF_WhitelistRemoveEpc`, `UHF_WhitelistClear`.
- GPIO: `UHF_RelayOn/Off`, `UHF_Relay2On/Off`, `UHF_Out1On/Off`, `UHF_Out2On/Off`
  (availability depends on the vendor DLL).
- Advanced: `UHF_ModuleCommand` (raw module command passthrough, when exported).

## .NET (optional)
`src/uhf_wrapper/dotnet/UhfWrapperNet.csproj` (x64) and
`src/uhf_wrapper/dotnet/UhfWrapperNet.x86.csproj` (x86) provide a minimal .NET wrapper (P/Invoke)
for the `UHF_*` API plus common vendor functions. All vendor exports are still available
via the native re-export.
