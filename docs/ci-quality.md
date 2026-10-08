# Additional CI quality checks

These workflows run on pull requests, pushes to main, and manual dispatch.

## Static analysis

[Static Analysis](../.github/workflows/static-analysis.yml) builds Vela's C
compositor using Clang Static Analyzer (scan-build --status-bugs) and checks
CPU-independent C modules with cppcheck and clang-tidy. No GPU is necessary.

## Sanitizers

[Sanitizer Tests](../.github/workflows/sanitizers.yml) builds with
AddressSanitizer and UndefinedBehaviorSanitizer and runs CPU-only CTest
suites. It deliberately excludes functional and sharpness tests, which
need a real GPU exposing Vulkan 1.4 and a DRM render node.

## Standalone package test

[Standalone Package Test](../.github/workflows/package-smoke.yml) builds
the existing PKGBUILD from the checked-out revision, then installs its
artifact into a separate fresh Arch container.
[ci-check-package.sh](../scripts/ci-check-package.sh) verifies files,
runtime linking, and no required Plasma/GNOME Shell installation.
This is a package smoke test, NOT a graphical login or Vulkan test.

## Prerequisites

GitHub Actions must be enabled. No secrets or self-hosted runners are
needed. Required check / branch protection policy is a separate optional
admin setting. Existing workflows are unchanged.
