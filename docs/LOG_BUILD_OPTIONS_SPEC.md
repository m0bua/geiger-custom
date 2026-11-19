# Log options in Quick Firmware — technical specification

## Initial build-profile prototype

The `geiger2_minimal` PlatformIO environment is an initial proof of concept,
not the configurable Quick Firmware builder. It currently fixes settings to
firmware defaults and compiles out logs, on-device graphs, on-device Debug,
flashlight, indication-mode switching, rate and dose alarm classes, and
device-theme editing. The companion app uses protocol capability bits to hide
unavailable controls and log views while keeping app-side Debug and overview
graphs available. The standard `geiger2` environment keeps the full feature
set. Measured sizes are recorded with each build result; this profile is
intended for device boot testing before extending the builder.

## Goal

Allow a firmware build profile to omit selected log types so the linker can
remove their firmware code and reclaim Flash. Separately, allow the retained
capacity of each enabled log to be configured for the target device. Log
capacity is primarily a storage/history setting; it must not be presented as a
reliable way to reduce firmware Flash usage.

The profile format and build script should first work in the existing local
build flow. The portal API and GitHub Actions workflow then use the same
validated profile and build script. A build must report exact Flash usage
before the image can be flashed.

## Portal build delivery architecture

Keep the portal static on GitHub Pages and add a separate server-side API for
custom builds; do not embed a shared GitHub token in portal JavaScript. A
Cloudflare Worker is the proposed API host. Cloudflare Pages Functions are an
alternative only if portal hosting moves from GitHub Pages to Cloudflare Pages.

Use two asynchronous API requests; the portal must not hold one HTTP request
open for the duration of a firmware build:

1. `POST /api/builds` accepts a versioned JSON profile plus an idempotency key.
   The API authenticates the user, validates the profile against an allowlist,
   applies rate limits, dispatches a dedicated GitHub Actions workflow, stores
   the requesting user and workflow run ID, then quickly returns a `buildId`
   and status URL. Repeating the same request with the same idempotency key
   must return the existing in-flight build instead of dispatching another.
2. `GET /api/builds/{buildId}` returns the current state (`queued`,
   `in_progress`, `success`, or `failure`) and, when complete, the exact Flash
   usage, build summary, and download links for BIN/HEX and the profile/hash
   manifest. Only the authenticated owner of that build may poll it. The portal
   polls with a bounded interval/backoff and stops on a terminal state.
3. The workflow checks out a fixed allowed branch/ref, builds the firmware,
   enforces the exact Flash limit, and uploads BIN, HEX, build summary, profile,
   and image hash as artifacts.

Use a GitHub App installation token held only by the API, with the minimum
repository permissions needed to dispatch the workflow and read its run and
artifacts. Store build ownership and idempotency records in a server-side store
with an expiry. The API must never accept an arbitrary repository, branch,
workflow, shell command, or build script from the browser. Restrict dispatch to
the dedicated build workflow and a fixed trusted source branch. Authenticate
users and limit build frequency so the API cannot be used as an open Actions
runner.
Keep the default firmware embedded in the app as the immediate offline option;
custom builds require network access and depend on the API and GitHub Actions.

GitHub Pages remains responsible only for static portal assets. If the API is
unavailable, the default embedded firmware and manual firmware upload continue
to work. Public repository artifacts may be downloadable without a GitHub
token, but triggering a workflow requires write-level Actions authorization;
the server-side GitHub App mediates that permission for authorized portal
users.

## User-facing build options

Organize optional indication behavior into three independent groups:

1. **Normal-mode indication** — the ordinary LED/click indication behavior
   and its selectable modes/options.
2. **Rate alarms** — notifications triggered by rate thresholds.
3. **Dose alarms** — notifications triggered by dose thresholds.

For each applicable setting, distinguish two build-time choices:

- **Variable:** retain the setting UI/protocol/persistence so it can be
  changed on the device or through the app.
- **Fixed:** keep the behavior, but compile in the selected value as a build
  default and remove the runtime mutability machinery for that setting.

For the rate-alarm and dose-alarm groups, also provide a separate
**feature-disabled** choice. This removes that alarm class itself, including
threshold evaluation and its outputs. Disabling dose alarms must not disable
rate alarms, ordinary dose measurement, dose display, or dose logging. The
same independence applies between rate alarms and dose alarms. Where an alarm
class remains enabled, its screen, flashlight, and sound outputs should be
independently selectable if doing so has a meaningful code-size benefit.

Disabling a notification output is distinct from fixing its settings: a fixed
sound alarm still sounds when triggered; a disabled sound output never does.
Likewise, fixing the normal indication mode keeps that indication active,
whereas omitting it removes the behavior.

Also evaluate these optional firmware features as independent size-reduction
switches:

- **On-device Debug screens:** may be omitted without removing app-side Debug
  or device measurement.
- **On-device graphs:** may be omitted while retaining stored logs and app
  graphs. Keep any log query/protocol code required by the app.
- **Flashlight:** distinguish manual flashlight control from alarm-driven
  flashlight output; either may be fixed or omitted independently where the
  implementation permits.
- **Click sound:** distinguish ordinary pulse clicks and their selectable
  sound profiles from threshold alarm sound. Removing click sound must not
  implicitly remove alarm sound, or vice versa.

Treat these as candidates until compile/link measurements confirm they save
useful Flash and their code paths can be excluded cleanly. Preserve the
standard build's present behavior by default. Keep application-only UI
features out of firmware Flash accounting.

Provide an enable/disable option for each persisted log type:

- minute log;
- hour log;
- day log;
- alert log.

For every enabled log, expose its retained capacity in records. Use the
existing default capacities unless the user changes them. Disabled log types
must not show an active capacity control.

The build summary must list enabled log types and capacities, plus exact Flash
usage and remaining Flash after compilation. Before compilation, show an
approximate Flash estimate based on measured builds of known profiles and make
clear that it is an estimate. Do not allow flashing if the exact build fails
or exceeds the available Flash.

Provide theme-style build-profile export/import so users can copy the current
profile for sharing or backup and paste it to restore its options. Export a
readable JSON document (which may be encoded for convenient copying), not a
binary blob or a list of positional setting indexes. Identify settings by
stable, descriptive feature keys, for example `logs.minute.enabled` and
`logs.minute.capacity`.

The JSON must include a format identifier and schema version. Import should be
forward-compatible: ignore unknown keys, use current firmware defaults for
missing keys, and continue to recognize stable keys even when new features
are added or UI/order indexes change. Validate known values and ranges before
applying anything; an invalid known value must leave the current profile
unchanged and produce a clear error. Preserve unknown keys when importing and
re-exporting where practical, so an older app can round-trip settings created
by a newer one. Do not reject a profile solely because its schema version is
newer if the known fields are valid. Include only build-profile settings, not
device data, firmware images, or secrets. Show a human-readable profile
summary alongside the JSON.

## Firmware behavior

- Compile out the disabled log's storage writer, retrieval/query code,
  protocol commands or response branches, and device UI paths wherever they
  are exclusive to that log. Confirm actual removal in the linker map and
  exact Flash-size comparison; a runtime `if` or zero capacity is not enough.
- For fixed settings, remove only the code needed to change or persist those
  settings; retain the selected behavior. For a feature-disabled alarm class,
  remove its threshold evaluation and output paths while retaining all
  unrelated measurement and alarm behavior.
- Preserve shared measurement and aggregation work when another enabled log
  depends on it. In particular, disabling persisted minute records must not
  disable pulse counting or the aggregation needed to produce enabled hour or
  day records.
- A disabled log is reported as unsupported/unavailable, not as an empty log.
  The app must be able to discover supported log types and hide or disable
  operations it cannot perform.
- Keep dose measurement, alarms, detector operation, and any enabled higher
  level aggregation independent of whether a particular history stream is
  stored.

## Storage layout and compatibility

- Capacity controls must be validated against physical EEPROM/SPI capacity,
  metadata reservations, sector alignment, and minimum viable capacities.
- EEPROM currently packs minute, hour, day, and alert records before fixed
  metadata. Changing counts can move later bases; update compile-time bounds
  and layout calculations so records never overlap metadata.
- SPI currently partitions storage among streams. Recalculate partitions from
  enabled streams and requested capacities without assigning sectors to
  disabled streams or violating erase-sector boundaries.
- Give each storage layout a stable profile/layout identity. On startup,
  detect incompatible existing data before accessing it. Never silently
  reinterpret records using a different layout. Choose and document a safe
  policy (preserve compatible streams, migrate, or explicitly initialize the
  incompatible storage); do not erase user logs without a clear indication.
- When possible, keep the layout of unaffected streams stable when another
  stream is disabled or resized. If that cannot be guaranteed, identify the
  affected streams to the user before flashing and provide a backup path.

## Build configuration

- Keep feature flags and capacities in a single build-profile configuration
  consumed by the firmware build system and the Quick Firmware UI.
- Version and validate the exported/imported JSON profile. Use stable named
  keys rather than positional indexes, and define defaults, unknown-key
  handling, and forward-compatible round-tripping for future schema changes.
- Reject invalid combinations and out-of-range capacities before invoking the
  compiler. The compiler remains the authority for final Flash usage.
- Include the selected profile, supported-log capability bits, and storage
  layout identity in build metadata available to the app/device protocol.
- Preserve the standard firmware profile as the default and ensure it matches
  current behavior and storage layout.

## Acceptance criteria

1. Each log type can be independently disabled in a build profile.
2. Disabling a type removes its exclusive firmware paths and yields a
   reproducible Flash reduction verified from map/size output.
3. Pulse counting, dose, alarms, and enabled dependent aggregations continue
   to work when an unrelated log is disabled.
4. Each enabled log capacity can be changed within validated storage limits.
5. The build reports the exact firmware Flash usage and remaining space; an
   approximate pre-build estimate is clearly distinguished from that result.
6. The app discovers unavailable log types and does not mistake them for
   empty logs.
7. Existing storage is either read compatibly or handled by an explicit,
   safe migration/initialization path when the profile changes.
8. The default profile preserves existing firmware behavior and data layout.
9. Profile choices clearly distinguish variable settings, fixed settings that
   retain behavior, and disabled alarm features that remove behavior.
10. Rate and dose alarms can be disabled independently without affecting the
    other alarm class or ordinary rate/dose measurement.
11. Optional Debug screens, on-device graphs, flashlight behavior, and click
    sound/profile code have independently measurable build switches; omitted
    features do not remove related app features or unrelated device behavior.

## Implementation sequence

1. Inventory feature call paths, protocol surface, dependencies, and current
   EEPROM/SPI layout identities; record a baseline map and Flash usage.
2. Measure low-risk candidates (Debug screens and on-device graphs) with
   compile-time switches, one at a time. Retain only switches that produce
   useful, reproducible savings without breaking other paths.
3. Measure indication, alarm-output, flashlight, and click-sound options,
   preserving the distinctions above.
4. Add compile-time log flags and verify one independent type at a time with
   map-file and Flash-size comparisons.
5. Add capability reporting so the app can distinguish unsupported features
   and log types.
6. Add capacity parameters and storage bounds/layout compatibility handling.
7. Connect Quick Firmware profile controls, pre-build estimate, exact build
   report, and flash gating.
8. Validate the default and reduced profiles, including upgrades with
   existing stored logs.
