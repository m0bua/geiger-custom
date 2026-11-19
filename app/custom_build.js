const PORTAL_BUILD_API_URL =
  document.querySelector('meta[name="portal-build-api"]')?.content.trim().replace(/\/+$/, '') || '';
const CUSTOM_BUILD_PROFILE_FORMAT = 'geiger-custom-firmware-profile';
const CUSTOM_BUILD_FEATURES = [
  'logs.enabled', 'device.graphs', 'device.debug', 'indication.flashlight',
  'indication.modes', 'alarms.rate', 'alarms.dose', 'theme.editable',
  'settings.mutable',
];
let customBuildAccessToken = '';
let customBuildAuthActive = false;
let customBuildPollActive = false;
let customBuildUnknownProfile = {};

function customBuildProfile() {
  const features = {...(customBuildUnknownProfile.features || {})};
  for (const input of document.querySelectorAll('[data-build-feature]')) {
    features[input.dataset.buildFeature] = input.checked;
  }
  return {
    ...customBuildUnknownProfile,
    format: CUSTOM_BUILD_PROFILE_FORMAT,
    schemaVersion: 1,
    features,
  };
}

function updateCustomBuildProfileView() {
  if (!elements.customFirmwareBuilder) return;
  elements.customFirmwareBuilder.hidden = !PORTAL_BUILD_API_URL;
  if (!PORTAL_BUILD_API_URL) return;
  const enabled = [...document.querySelectorAll('[data-build-feature]')]
    .filter((input) => input.checked).length;
  const estimate = 30840 + Math.round((65280 - 30840) * enabled / CUSTOM_BUILD_FEATURES.length);
  elements.customBuildEstimate.textContent =
    `Approximate Flash use: about ${(estimate / 1024).toFixed(1)} KB ` +
    '(rough estimate from measured Full and Minimal builds; exact size follows compilation).';
  if (elements.customBuildProfileJson && !elements.customBuildProfileJson.value) {
    elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
  }
  elements.customBuildStart.disabled = !customBuildAccessToken || customBuildPollActive;
}

async function customBuildApi(path, options = {}) {
  const headers = new Headers(options.headers || {});
  headers.set('Accept', 'application/json');
  if (customBuildAccessToken) headers.set('Authorization', `Bearer ${customBuildAccessToken}`);
  if (options.body) headers.set('Content-Type', 'application/json');
  const response = await fetch(`${PORTAL_BUILD_API_URL}${path}`, {...options, headers});
  const body = await response.json().catch(() => ({}));
  if (!response.ok) throw new Error(body.error || `Build API returned HTTP ${response.status}.`);
  return body;
}

async function customBuildSignIn() {
  if (!PORTAL_BUILD_API_URL || customBuildAuthActive) return;
  customBuildAuthActive = true;
  elements.customBuildSignIn.disabled = true;
  elements.customBuildStatus.textContent = 'Starting GitHub device authorization…';
  elements.customBuildAuthCode.hidden = true;
  elements.customBuildAuthLink.hidden = true;
  try {
    const device = await customBuildApi('/api/auth/device', {method: 'POST', body: '{}'});
    elements.customBuildAuthLink.href = device.verification_uri;
    elements.customBuildAuthLink.hidden = false;
    elements.customBuildAuthCode.textContent = device.user_code;
    elements.customBuildAuthCode.hidden = false;
    elements.customBuildStatus.textContent = 'Open the GitHub authorization link and enter the code.';
    let interval = Math.max(5, Number(device.interval) || 5);
    const deadline = Date.now() + Math.max(60, Number(device.expires_in) || 900) * 1000;
    while (Date.now() < deadline && customBuildAuthActive) {
      await new Promise((resolve) => setTimeout(resolve, interval * 1000));
      const result = await customBuildApi('/api/auth/poll', {
        method: 'POST', body: JSON.stringify({device_code: device.device_code}),
      });
      if (result.access_token) {
        customBuildAccessToken = result.access_token;
        elements.customBuildAuthCode.hidden = true;
        elements.customBuildAuthLink.hidden = true;
        elements.customBuildStatus.textContent = 'GitHub authorization complete. You can start a build.';
        updateCustomBuildProfileView();
        return;
      }
      if (result.error === 'authorization_pending') continue;
      if (result.error === 'slow_down') { interval += 5; continue; }
      if (result.error) throw new Error(result.error_description || result.error);
    }
    if (customBuildAuthActive) elements.customBuildStatus.textContent = 'GitHub authorization expired. Try again.';
  } catch (error) {
    elements.customBuildStatus.textContent = error.message;
  } finally {
    customBuildAuthActive = false;
    elements.customBuildSignIn.disabled = false;
  }
}

async function pollCustomBuild(buildId, statusUrl, runUrl) {
  customBuildPollActive = true;
  elements.customBuildStart.disabled = true;
  elements.customBuildDownload.hidden = true;
  if (runUrl) {
    elements.customBuildRun.href = runUrl;
    elements.customBuildRun.hidden = false;
  }
  try {
    for (let attempt = 0; attempt < 80; ++attempt) {
      await new Promise((resolve) => setTimeout(resolve, attempt < 8 ? 5000 : 15000));
      const status = await customBuildApi(statusUrl);
      if (status.runUrl) {
        elements.customBuildRun.href = status.runUrl;
        elements.customBuildRun.hidden = false;
      }
      if (status.status === 'success') {
        if (!status.artifactUrl) throw new Error('Build succeeded, but its artifact is not available yet.');
        elements.customBuildDownload.href = status.artifactUrl;
        elements.customBuildDownload.hidden = false;
        elements.customBuildStatus.textContent = 'Build complete. Download the ZIP; it contains BIN, HEX, profile, and exact Flash report.';
        return;
      }
      if (['failure', 'cancelled', 'timed_out', 'action_required'].includes(status.status)) {
        throw new Error(`Build ${status.status}. Open the GitHub run for details.`);
      }
      elements.customBuildStatus.textContent = `Build ${status.status || 'in progress'}…`;
    }
    throw new Error('Build is taking longer than expected. Check the GitHub run for its latest status.');
  } catch (error) {
    elements.customBuildStatus.textContent = error.message;
  } finally {
    customBuildPollActive = false;
    updateCustomBuildProfileView();
  }
}

async function startCustomBuild() {
  if (!customBuildAccessToken || customBuildPollActive) return;
  const profile = customBuildProfile();
  const idempotencyKey = crypto.randomUUID();
  elements.customBuildStatus.textContent = 'Submitting build request…';
  elements.customBuildDownload.hidden = true;
  elements.customBuildRun.hidden = true;
  try {
    const created = await customBuildApi('/api/builds', {
      method: 'POST',
      headers: {'Idempotency-Key': idempotencyKey},
      body: JSON.stringify({profile}),
    });
    elements.customBuildStatus.textContent = 'Build queued on GitHub Actions.';
    await pollCustomBuild(created.buildId, created.statusUrl, created.runUrl);
  } catch (error) {
    elements.customBuildStatus.textContent = error.message;
  }
}

elements.customBuildSignIn?.addEventListener('click', customBuildSignIn);
elements.customBuildStart?.addEventListener('click', startCustomBuild);
elements.customBuildExportProfile?.addEventListener('click', () => {
  elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
});
elements.customBuildImportProfile?.addEventListener('click', () => {
  try {
    const parsed = JSON.parse(elements.customBuildProfileJson.value);
    if (!parsed || parsed.format !== CUSTOM_BUILD_PROFILE_FORMAT ||
        !Number.isInteger(parsed.schemaVersion) || parsed.schemaVersion < 1 ||
        !parsed.features || typeof parsed.features !== 'object' || Array.isArray(parsed.features)) {
      throw new Error('Invalid firmware build profile.');
    }
    for (const input of document.querySelectorAll('[data-build-feature]')) {
      const value = parsed.features[input.dataset.buildFeature];
      if (value !== undefined && typeof value !== 'boolean')
        throw new Error(`Feature ${input.dataset.buildFeature} must be true or false.`);
    }
    customBuildUnknownProfile = {...parsed};
    for (const input of document.querySelectorAll('[data-build-feature]')) {
      const value = parsed.features[input.dataset.buildFeature];
      if (typeof value === 'boolean') input.checked = value;
    }
    updateCustomBuildProfileView();
    elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
    elements.customBuildStatus.textContent = 'Profile applied.';
  } catch (error) {
    elements.customBuildStatus.textContent = `Profile not applied: ${error.message}`;
  }
});
document.querySelectorAll('[data-build-feature]').forEach((input) =>
  input.addEventListener('change', () => {
    elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
    updateCustomBuildProfileView();
  }));
updateCustomBuildProfileView();
