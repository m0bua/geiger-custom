# Release preparation

Before preparing any pre-release or release:

- Update the app's **About → Recent changes** changelog.
- Align the app's **Manual** and `docs/USER_MANUAL_DRAFT.md` with the behavior being released.
- For a pre-release, include only changes since the previous pre-release tag in `.github/workflows/build-dev-prerelease.yml`.
- For a stable release, include changes since the previous stable release in `.github/workflows/build-standalone-app.yml`.
- Keep the app changelog, release notes, and instructions consistent. Remove descriptions of behavior that has since changed.
- Preparing release notes does not authorize publishing. Run or publish a release only when explicitly requested.
