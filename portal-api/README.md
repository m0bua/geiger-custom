# Portal custom-build API

This Worker is a small asynchronous API for the portal. `POST /api/builds`
starts a GitHub Actions build and returns a build ID immediately;
`GET /api/builds/{id}` polls its status and returns the artifact ZIP URL once
the build succeeds. The app's default firmware remains embedded and works
without this service.

## Provisioning

1. Create a GitHub App for `m0bua/geiger-custom` with **Actions: read and
   write** and **Contents: read**, install it on this repository, and note the
   App ID and installation ID. The Worker uses the installation token only to
   dispatch `build-custom-firmware.yml` on the fixed `dev` branch and read its
   run/artifact metadata.
2. Create a GitHub OAuth App, enable Device Flow, and use `read:user`. Add the
   OAuth Client ID to the Worker configuration. The Worker allows only names
   listed in `ALLOWED_GITHUB_USERS`; start with the repository maintainer.
3. Create the Cloudflare KV namespace with `npx wrangler kv namespace create
   BUILD_JOBS` and replace the placeholder namespace ID in `wrangler.toml`.
4. Set the GitHub App private key as an encrypted Worker secret; never put it
   in `wrangler.toml`, portal JavaScript, or the repository:

   ```powershell
   npx wrangler secret put GITHUB_APP_PRIVATE_KEY
   ```

5. Set `PORTAL_ORIGIN` to the exact deployed portal origin, then deploy with
   `npx wrangler deploy`. The workflow and its build script must already be
   present on `dev` before enabling requests from the portal.
6. Configure the portal's public `PORTAL_BUILD_API_URL` to the deployed
   `workers.dev` URL. This URL is not a secret. Do not configure a shared
   GitHub token in the portal.

The OAuth device token is held in app memory and sent only to this API for user
verification. The Worker checks the GitHub account against the allowlist and
keeps build ownership in KV for 24 hours. It applies a best-effort five-minute
per-account build cooldown and supports idempotent create retries. The workflow
also serializes custom builds so concurrent requests wait in the Actions queue.

## API

- `POST /api/auth/device` starts GitHub Device Flow.
- `POST /api/auth/poll` checks the Device Flow authorization.
- `POST /api/builds` accepts `{ "profile": { ... } }` and an
  `Idempotency-Key` header; it returns `buildId`, `statusUrl`, and `runUrl`.
- `GET /api/builds/{buildId}` returns `queued`, `in_progress`, `success`, or a
  failure conclusion. On success it includes a short-lived artifact URL.

All browser requests must come from the configured portal origin. The Worker
never accepts a caller-provided repository, branch, workflow, shell command,
or compiler argument. Build profiles are validated in both the Worker and the
trusted repository build script.
