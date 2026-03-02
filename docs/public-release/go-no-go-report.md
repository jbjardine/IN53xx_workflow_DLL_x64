# Public Release Go/No-Go Report

Date: 2026-03-02
Branch: `hardening/public-readiness-2026-03`
Repository: `jbjardine/IN53xx_workflow_DLL_x64`

## Scoring rubric
- Secret exposure (30%): **required 1.00**
- Legal/vendor compliance (25%): **required 1.00**
- Security controls and hardening (20%): required >= 0.95
- CI/release reproducibility (15%): required >= 0.95
- Public documentation usability (10%): required >= 0.90

Passing rule: overall >= 0.95 and first two dimensions at 1.00.

## Evidence
- Pull request with hardening changes: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/pull/1
- Secret scan workflow (Gitleaks): https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/runs/22591962093
- CodeQL analysis workflow: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/runs/22591962096
- Build and release workflow: https://github.com/jbjardine/IN53xx_workflow_DLL_x64/actions/runs/22591962045
- Legal checklist: `docs/public-release/legal-checklist.md`

Implementation notes:
- Branch protection enabled on `main` with required checks:
  - `build (x64, x64)`
  - `build (x86, Win32)`
  - `gitleaks`
  - `analyze (cpp-csharp)`
- Dependabot security updates enabled.
- Secret scanning is currently unavailable on this private repository plan (GitHub API 422), so OSS CI scanning is used as blocking control.

## Dimension scores
- Secret exposure: **1.00** (regex history scan + CI gitleaks pass)
- Legal/vendor compliance: **0.60** (technical/legal artifacts ready, legal sign-off pending)
- Security controls and hardening: **0.96**
- CI/release reproducibility: **0.97**
- Public documentation usability: **0.92**
- Weighted total: **0.88**

## Decision
- Outcome: **NO-GO (legal sign-off pending)**
- Notes:
  - Complete owner/legal sign-off in `docs/public-release/legal-checklist.md`.
  - After legal sign-off, rerun and re-link the latest CI evidence before changing visibility.
