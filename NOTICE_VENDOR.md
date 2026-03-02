# Vendor Notice

This project depends on the proprietary vendor library `SWHidApi.dll` for hardware access.

## Distribution boundary
- This repository does **not** distribute `SWHidApi.dll`.
- Public release artifacts from this repository must not include vendor binaries.
- Integrators must obtain the vendor SDK and DLL through the vendor's official licensing channel.

## API boundary
- `UhfWrapper.dll` provides a stable `UHF_*` API and may re-export vendor functions for runtime compatibility.
- Re-export support in this project does not transfer any redistribution rights for vendor code.

## Legal gate
Before changing repository visibility to public, the project owner must confirm:
1. Public publication of wrapper source code is permitted.
2. Public exposure of function signatures used by the wrapper is permitted.
3. Release artifacts do not contain proprietary vendor binaries.

See `docs/public-release/legal-checklist.md` for the required sign-off workflow.
