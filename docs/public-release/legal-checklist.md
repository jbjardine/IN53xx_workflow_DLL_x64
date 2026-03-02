# Legal Checklist For Public Release

This checklist is a **blocking gate**. Do not switch repository visibility to public until all items are complete.

## Repository
- [ ] Repository: `jbjardine/IN53xx_workflow_DLL_x64`
- [ ] Target visibility change: Private -> Public
- [ ] Planned date:

## Vendor and licensing
- [ ] Confirm wrapper source publication is allowed.
- [ ] Confirm public documentation of consumed vendor API signatures is allowed.
- [ ] Confirm no vendor SDK binaries (`SWHidApi.dll`, `.lib`) are tracked in Git.
- [ ] Confirm public release ZIPs do not include `SWHidApi.dll`.
- [ ] Confirm `NOTICE_VENDOR.md` is present and accurate.

## Compliance evidence
- [ ] Attach latest CI run URL showing packaging checks passed.
- [ ] Attach latest secret scan run URL.
- [ ] Attach latest code scanning run URL.

## Sign-off (required)
- [ ] Owner sign-off (name/date):
- [ ] Legal/compliance sign-off (name/date):

## Go/No-Go outcome
- [ ] GO
- [ ] NO-GO
- [ ] Notes:
