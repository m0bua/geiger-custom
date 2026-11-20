# Retired Cloudflare build API

This directory contains the earlier Cloudflare Worker prototype. The portal
no longer calls it; do not deploy it or configure a build API URL.

The active custom-build flow uses a prefilled public GitHub Issue as its
request queue and `.github/workflows/build-custom-firmware.yml` to validate,
build, and publish short-lived artifacts. See
[`docs/LOG_BUILD_OPTIONS_SPEC.md`](../docs/LOG_BUILD_OPTIONS_SPEC.md) for the
current design and the requirement that the issue workflow be present on the
repository's default branch before public requests can trigger it.
