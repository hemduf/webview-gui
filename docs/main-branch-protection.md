# `main` branch protection

`main` is the release-quality integration branch. Normal development must arrive through a pull request and must not bypass the qualification matrix.

## Required repository setting

Configure a branch protection rule or repository ruleset targeting exactly `main` with these behaviors:

- require a pull request before merging;
- require status checks to pass before merging;
- require the branch to be up to date before merging;
- block merge while any required check is pending or failing;
- do not permit force-pushes to `main`;
- do not permit deletion of `main`.

This repository is commonly maintained by one developer, so an approval count is not part of the mandatory contract. If review approvals are enabled later, keep that policy independent from the qualification checks below.

## Required status-check contexts

Classic branch protection requires the **job/check names**, not only the workflow display names. Require all of the following current stable contexts:

### Repository policy

- `required main qualification contract`

### Cross-platform CI

- `ubuntu-latest / Debug`
- `macos-latest / Debug`
- `windows-latest / Debug`
- `ubuntu-latest / ASan+UBSan`
- `macos-latest / ASan+UBSan`
- `ubuntu-latest / TSan registry`

### Plug-in-safe private-module profile

- `ubuntu-latest / private module exports`
- `macos-latest / private module exports`
- `windows-latest / private module exports`

### Header-only fail-closed contract

- `ubuntu-latest standalone header-only diagnostic`
- `macos-latest standalone header-only diagnostic`
- `windows-latest standalone header-only diagnostic`

### macOS unload diagnostics

- `macOS Zombies lifecycle`

`tests/ci_policy_tests.cmake` protects these workflow/job names and verifies that every mandatory workflow also runs on pushes to `main`. Rename a required job only together with the branch-protection configuration and this document.

## Push-to-main requalification

Every mandatory workflow must run for both pull requests and pushes to `main`. The push run validates the actual merge commit rather than assuming that a successful PR-head or synthetic merge test is equivalent to the final branch state.

After merging a change, verify the `main` commit has successful runs for:

- Repository policy;
- CI;
- Plugin-safe CMake profile;
- Header-only contract;
- macOS plugin diagnostics.

## Administrative / emergency override

Preferred policy: administrators are subject to the same rule and do not bypass required checks.

If an emergency override is deliberately enabled in GitHub settings, its use must be exceptional and auditable:

1. create an issue recording why normal PR/check enforcement cannot be used;
2. make the smallest necessary change;
3. restore protection immediately;
4. run every mandatory workflow against the resulting `main` SHA;
5. keep the incident issue open until all post-change gates complete successfully.

Never treat an administrator bypass as validation of the code itself.
