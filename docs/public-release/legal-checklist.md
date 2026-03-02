# Legal Checklist For Public Release

This checklist is a **blocking gate**. Do not switch repository visibility to public until all items are complete.

## Repository
- [x] Repository: `jbjardine/IN53xx_workflow_DLL_x64`
- [x] Target visibility change: Private -> Public
- [ ] Planned date:

## Vendor and licensing
- [ ] Confirm wrapper source publication is allowed.
- [ ] Confirm public documentation of consumed vendor API signatures is allowed.
- [x] Confirm no vendor SDK binaries (`SWHidApi.dll`, `.lib`) are tracked in Git.
- [x] Confirm public release ZIPs do not include `SWHidApi.dll`.
- [x] Confirm `NOTICE_VENDOR.md` is present and accurate.

## Compliance evidence
- [x] Attach latest CI run URL showing packaging checks passed.
  - https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/runs/22591757316
- [x] Attach latest secret scan run URL.
  - https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/runs/22591757293
- [x] Attach latest code scanning run URL.
  - https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/runs/22591757276

## Sign-off (required)
- [ ] Owner sign-off (name/date):
- [ ] Legal/compliance sign-off (name/date):

## Go/No-Go outcome
- [ ] GO
- [ ] NO-GO
- [ ] Notes:
