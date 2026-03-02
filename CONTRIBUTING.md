# Contributing

Thanks for contributing.

## Before you start
- Read `README.md`, `API.md`, and `CLI.md`.
- Keep changes compatible with both x64 and x86 builds.
- Do not add vendor SDK binaries to Git.

## Development setup
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

This ensures both architectures stay aligned.

## Pull request checklist
- [ ] Change is scoped and documented.
- [ ] `README.md` / `API.md` / `CLI.md` updated if behavior changed.
- [ ] No secrets or machine-local paths added.
- [ ] No vendor binaries added.
- [ ] CI is green.

## Commit guidance
- Keep commits focused.
- Explain user impact in the commit message.
- Link the related issue when possible.
