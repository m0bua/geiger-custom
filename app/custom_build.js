const PORTAL_BUILD_API_URL =
  document.querySelector('meta[name="portal-build-api"]')?.content.trim().replace(/\/+$/, '') || '';
const PORTAL_BUILD_BRANCH =
  document.querySelector('meta[name="portal-build-branch"]')?.content.trim() || '';
const PORTAL_BUILD_CONFIGURED = Boolean(PORTAL_BUILD_API_URL) &&
  ['dev', 'main'].includes(PORTAL_BUILD_BRANCH);
const CUSTOM_BUILD_PROFILE_FORMAT = 'geiger-custom-firmware-profile';
const CUSTOM_BUILD_FEATURES = [
  'logs.enabled', 'device.graphs', 'device.debug', 'indication.flashlight',
  'indication.modes', 'alarms.rate', 'alarms.dose',
];
const CUSTOM_BUILD_SETTING_DEFAULTS = [
  2, 0, 0, 0, 0, 0, 0, 2, 0, 0, 1, 1, 0, 2, 0, 0,
  4, 0, 0, 0, 0, 3, 1, 1, 0, 0, 0, 0, 0, 0, 0, 4,
];
const CUSTOM_BUILD_SETTING_OPTION_COUNTS = [
  6, 6, 4, 4, 4, 9, 9, 6, 2, 3, 4, 4, 6, 3, 6, 2,
  9, 2, 1, 1, 2, 6, 8, 4, 4, 4, 4, 8, 9, 3, 2, 6,
];
let customBuildAccessToken = '';
let customBuildAuthActive = false;
let customBuildPollActive = false;
let customBuildUnknownProfile = {};

function customBuildSettingKey(setting) {
  return setting.label.toLowerCase().replace(/[^a-z0-9]+/g, '-');
}

function customBuildSettingDraft() {
  const imported = customBuildUnknownProfile.settings || {};
  const legacySettingsChoice = customBuildUnknownProfile.features?.['settings.mutable'];
  const draft = {};
  const values = CUSTOM_BUILD_SETTING_DEFAULTS.slice();
  for (const setting of remoteSettings) {
    if ([14, 18, 19].includes(setting.id)) continue;
    const key = customBuildSettingKey(setting);
    const stored = imported[key];
    const defaultMutable = typeof legacySettingsChoice === 'boolean' ? legacySettingsChoice :
      !demoHiddenSettings.has(setting.id);
    const mutable = typeof stored?.mutable === 'boolean' ? stored.mutable : defaultMutable;
    const value = setting.id === 15 ? (mutable ?
      (Number.isInteger(stored?.default) ? stored.default : CUSTOM_BUILD_SETTING_DEFAULTS[15]) : 0) :
      Number.isInteger(stored?.default) ? stored.default :
      CUSTOM_BUILD_SETTING_DEFAULTS[setting.id];
    draft[key] = {mutable, default: value};
    values[setting.id] = value;
  }
  for (const setting of remoteSettings) {
    if ([14, 18, 19].includes(setting.id)) continue;
    const available = settingsOptions(setting, values).map((item, index) =>
      typeof item === 'string' ? {label: item, value: index, disabled: false} : item);
    const selected = available.find((item) => item.value === draft[customBuildSettingKey(setting)].default &&
      !item.disabled);
    if (!selected) {
      const fallback = available.find((item) => !item.disabled);
      if (fallback && Number.isInteger(fallback.value)) {
        draft[customBuildSettingKey(setting)].default = fallback.value;
        values[setting.id] = fallback.value;
      }
    }
  }
  return {draft, values};
}

function renderCustomBuildConfiguration() {
  const root = elements.customBuildConfigList;
  if (!root) return;
  root.replaceChildren();
  const {draft, values} = customBuildSettingDraft();
  const rows = document.createElement('div');
  rows.className = 'settings-pair custom-build-settings-pair';
  root.append(rows);
  const addRow = (label, key, current, options, onUpdate) => {
    const row = document.createElement('div');
    row.className = 'setting-row';
    const title = document.createElement('label');
    title.textContent = label;
    const controls = document.createElement('div');
    controls.className = 'setting-controls';
    const staticLabel = document.createElement('label');
    staticLabel.className = 'setting-visible';
    const staticInput = document.createElement('input');
    staticInput.type = 'checkbox';
    staticInput.checked = !current.mutable;
    staticInput.setAttribute('aria-label', `${label} static`);
    staticLabel.append(staticInput, document.createTextNode('Static'));
    const select = document.createElement('select');
    select.setAttribute('aria-label', `${label} default`);
    for (const item of options) {
      const option = document.createElement('option');
      option.value = String(item.value);
      option.textContent = item.label;
      option.disabled = !!item.disabled;
      select.append(option);
    }
    select.value = String(current.default);
    if (![...select.options].some((option) => option.value === select.value)) {
      select.selectedIndex = 0;
      if (typeof current.default === 'number' && select.value !== '') {
        current.default = Number(select.value);
      }
    }
    staticInput.addEventListener('change', () => {
      current.mutable = !staticInput.checked;
      onUpdate(key, current);
    });
    select.addEventListener('change', () => {
      current.default = typeof current.default === 'number' ? Number(select.value) : select.value;
      onUpdate(key, current);
    });
    controls.append(staticLabel, select);
    row.append(title, controls);
    rows.append(row);
  };

  for (const setting of remoteSettings) {
    if ([14, 18, 19].includes(setting.id)) continue;
    const key = customBuildSettingKey(setting);
    if (setting.id === 15) {
      const row = document.createElement('div');
      row.className = 'setting-row';
      const title = document.createElement('label');
      title.textContent = setting.label;
      const alwaysOnLabel = document.createElement('label');
      alwaysOnLabel.className = 'setting-visible';
      const alwaysOn = document.createElement('input');
      alwaysOn.type = 'checkbox';
      alwaysOn.checked = !draft[key].mutable;
      alwaysOn.setAttribute('aria-label', 'Keep HV always on and remove its OFF control');
      alwaysOnLabel.append(alwaysOn, document.createTextNode('Always ON'));
      const hvDefault = document.createElement('select');
      hvDefault.setAttribute('aria-label', 'HV default state');
      for (const [value, label] of [[0, 'ON'], [1, 'OFF']]) {
        const option = document.createElement('option');
        option.value = String(value);
        option.textContent = label;
        hvDefault.append(option);
      }
      hvDefault.value = String(draft[key].default);
      hvDefault.disabled = !draft[key].mutable;
      alwaysOn.addEventListener('change', () => {
        draft[key].mutable = !alwaysOn.checked;
        draft[key].default = alwaysOn.checked ? 0 : 1;
        hvDefault.value = String(draft[key].default);
        hvDefault.disabled = alwaysOn.checked;
        customBuildUnknownProfile.settings = {
          ...(customBuildUnknownProfile.settings || {}), [key]: {...draft[key]},
        };
        elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
        renderCustomBuildConfiguration();
        updateCustomBuildEstimate();
      });
      hvDefault.addEventListener('change', () => {
        draft[key].default = Number(hvDefault.value);
        customBuildUnknownProfile.settings = {
          ...(customBuildUnknownProfile.settings || {}), [key]: {...draft[key]},
        };
        elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
      });
      const controls = document.createElement('div');
      controls.className = 'setting-controls';
      controls.append(alwaysOnLabel, hvDefault);
      row.append(title, controls);
      rows.append(row);
      continue;
    }
    const settingOptions = settingsOptions(setting, values).map((item, index) =>
      typeof item === 'string' ? {label: item, value: index, disabled: false} : item);
    addRow(setting.label, key, draft[key], settingOptions, (settingKey, value) => {
      customBuildUnknownProfile.settings = {
        ...(customBuildUnknownProfile.settings || {}), [settingKey]: {...value},
      };
      elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
      renderCustomBuildConfiguration();
      updateCustomBuildEstimate();
    });
  }

  const storedTheme = customBuildUnknownProfile.theme || {};
  const oldPreset = typeof storedTheme.preset === 'string' ? THEME_PRESETS[storedTheme.preset] : null;
  const theme = {
    mutable: typeof storedTheme.mutable === 'boolean' ? storedTheme.mutable :
      typeof customBuildUnknownProfile.features?.['theme.editable'] === 'boolean' ?
        customBuildUnknownProfile.features['theme.editable'] : false,
    colors: Array.isArray(storedTheme.colors) && storedTheme.colors.length === THEME_COLORS.length ?
      storedTheme.colors.slice() : (oldPreset?.colors || DEFAULT_THEME_RGB565).map(rgb565ToHex),
    dimming: Number.isInteger(storedTheme.dimming) ? storedTheme.dimming : DEFAULT_THEME_DIMMING,
  };
  customBuildUnknownProfile.theme = {...storedTheme, ...theme, colors: theme.colors.slice()};
  const themeSection = document.createElement('details');
  themeSection.className = 'settings-section custom-build-theme-section';
  themeSection.open = true;
  const themeHeading = document.createElement('summary');
  themeHeading.className = 'settings-section-summary';
  const themeTitle = document.createElement('span');
  themeTitle.className = 'settings-section-title';
  themeTitle.textContent = 'Display theme';
  const themeStaticLabel = document.createElement('label');
  themeStaticLabel.className = 'setting-visible';
  const themeStatic = document.createElement('input');
  themeStatic.type = 'checkbox';
  themeStatic.checked = !theme.mutable;
  themeStatic.setAttribute('aria-label', 'Make theme static');
  themeStaticLabel.append(themeStatic, document.createTextNode('Static palette'));
  themeHeading.append(themeTitle, themeStaticLabel);
  themeSection.append(themeHeading);
  const themeToolbar = document.createElement('div');
  themeToolbar.className = 'theme-toolbar custom-build-theme-toolbar';
  const presetLabel = document.createElement('label');
  presetLabel.className = 'theme-preset-control';
  presetLabel.textContent = 'Palette';
  const presetSelect = document.createElement('select');
  presetSelect.setAttribute('aria-label', 'Theme palette preset');
  for (const [presetId, preset] of Object.entries(THEME_PRESETS)) {
    const option = document.createElement('option');
    option.value = presetId;
    option.textContent = preset.label;
    presetSelect.append(option);
  }
  const customOption = document.createElement('option');
  customOption.value = 'custom';
  customOption.textContent = 'Custom';
  presetSelect.append(customOption);
  const detectedPreset = detectThemePreset(theme.colors.map(hexToRgb565), theme.dimming);
  theme.preset = THEME_PRESETS[detectedPreset] || detectedPreset === 'custom' ? detectedPreset : 'custom';
  presetSelect.value = theme.preset;
  presetLabel.append(presetSelect);
  customBuildUnknownProfile.theme = {...storedTheme, ...theme, colors: theme.colors.slice()};
  const themeCodeLabel = document.createElement('label');
  themeCodeLabel.className = 'theme-code-control custom-build-theme-code-control';
  themeCodeLabel.textContent = 'Palette code';
  const themeCode = document.createElement('input');
  themeCode.type = 'text';
  themeCode.readOnly = true;
  themeCode.value = encodeThemeConfigCode(theme.colors.map(hexToRgb565), theme.dimming);
  themeCode.setAttribute('aria-label', 'Theme palette export code');
  themeCodeLabel.append(themeCode);
  const copyTheme = document.createElement('button');
  copyTheme.className = 'button button-quiet';
  copyTheme.type = 'button';
  copyTheme.textContent = 'Copy';
  copyTheme.addEventListener('click', async () => {
    try {
      await navigator.clipboard.writeText(themeCode.value);
      elements.customBuildStatus.textContent = 'Theme code copied. Paste it into Settings → Display theme.';
    } catch (error) {
      themeCode.focus();
      themeCode.select();
      elements.customBuildStatus.textContent = 'Select and copy the theme code; clipboard access is unavailable.';
    }
  });
  themeToolbar.append(presetLabel, themeCodeLabel, copyTheme);
  themeSection.append(themeToolbar);
  const palette = document.createElement('div');
  palette.className = 'theme-colors custom-build-theme-colors';
  const syncThemeDraft = () => {
    presetSelect.value = theme.preset;
    themeCode.value = encodeThemeConfigCode(theme.colors.map(hexToRgb565), theme.dimming);
    customBuildUnknownProfile.theme = {...theme, colors: theme.colors.slice()};
    elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
    updateCustomBuildEstimate();
  };
  renderThemeEditor(palette, theme.colors.map(hexToRgb565), theme.dimming, {
    onEditStart: () => {
      theme.preset = 'custom';
      presetSelect.value = 'custom';
    },
    onColorChange: (index, color) => {
      theme.colors[index] = rgb565ToHex(color).toUpperCase();
      theme.preset = 'custom';
      syncThemeDraft();
    },
    onColorInvalid: () => {
      elements.customBuildStatus.textContent = 'Enter a valid #RRGGBB value for each theme color before building.';
    },
    onDimmingChange: (value) => {
      theme.dimming = value;
      theme.preset = detectThemePreset(theme.colors.map(hexToRgb565), value);
      syncThemeDraft();
    },
    onDimmingInvalid: () => {
      elements.customBuildStatus.textContent = 'Graph dimming must be an integer from 0 to 100.';
    },
  });
  themeSection.append(palette);
  root.append(themeSection);
  presetSelect.addEventListener('change', () => {
    theme.preset = presetSelect.value;
    const preset = THEME_PRESETS[theme.preset];
    if (preset) {
      theme.colors = preset.colors.map(rgb565ToHex);
      theme.dimming = preset.dimming;
      renderThemeEditor(palette, theme.colors.map(hexToRgb565), theme.dimming, {
        onEditStart: () => { theme.preset = 'custom'; presetSelect.value = 'custom'; },
        onColorChange: (index, color) => {
          theme.colors[index] = rgb565ToHex(color).toUpperCase();
          theme.preset = 'custom';
          syncThemeDraft();
        },
        onColorInvalid: () => {
          elements.customBuildStatus.textContent = 'Enter a valid #RRGGBB value for each theme color before building.';
        },
        onDimmingChange: (value) => {
          theme.dimming = value;
          theme.preset = detectThemePreset(theme.colors.map(hexToRgb565), value);
          syncThemeDraft();
        },
        onDimmingInvalid: () => {
          elements.customBuildStatus.textContent = 'Graph dimming must be an integer from 0 to 100.';
        },
      });
    }
    syncThemeDraft();
  });
  themeStatic.addEventListener('change', () => {
    theme.mutable = !themeStatic.checked;
    syncThemeDraft();
  });
}

function customBuildProfile() {
  const features = {...(customBuildUnknownProfile.features || {})};
  for (const input of document.querySelectorAll('[data-build-feature]')) {
    features[input.dataset.buildFeature] = input.checked;
  }
  const settings = {...(customBuildUnknownProfile.settings || {})};
  const {draft} = customBuildSettingDraft();
  for (const [key, value] of Object.entries(draft)) settings[key] = value;
  const theme = customBuildUnknownProfile.theme || {};
  const hasMutableSettings = Object.values(draft).some((setting) => setting.mutable);
  features['settings.mutable'] = hasMutableSettings;
  features['theme.editable'] = typeof theme.mutable === 'boolean' ? theme.mutable :
    features['theme.editable'] !== false;
  return {
    ...customBuildUnknownProfile,
    format: CUSTOM_BUILD_PROFILE_FORMAT,
    schemaVersion: 3,
    features,
    settings,
    theme: {
      ...theme,
      mutable: typeof theme.mutable === 'boolean' ? theme.mutable :
        features['theme.editable'],
      colors: Array.isArray(theme.colors) && theme.colors.length === THEME_COLORS.length ?
        theme.colors : DEFAULT_THEME_RGB565.map(rgb565ToHex),
      dimming: Number.isInteger(theme.dimming) ? theme.dimming : DEFAULT_THEME_DIMMING,
    },
  };
}

function updateCustomBuildProfileView() {
  if (!elements.customFirmwareBuilder) return;
  const hasEmbeddedFirmware = typeof globalThis.EMBEDDED_FIRMWARE_B64 === 'string';
  const customSummary = elements.customFirmwareBuilder.querySelector(':scope > summary');
  if (elements.firmwareCurrentSection) {
    const previewOnly = !hasEmbeddedFirmware && !PORTAL_BUILD_API_URL;
    elements.firmwareCurrentSection.classList.toggle('firmware-current-preview', previewOnly);
    if (!hasEmbeddedFirmware) {
      elements.firmwareCurrentSection.hidden = false;
      elements.firmwareCurrentSection.open = false;
      if (elements.firmwareTools) elements.firmwareTools.open = false;
      if (elements.firmwareCurrentSummary) elements.firmwareCurrentSummary.hidden = false;
      if (elements.firmwareCurrentStaticTitle) elements.firmwareCurrentStaticTitle.hidden = true;
      if (elements.embeddedQuickFlash) elements.embeddedQuickFlash.hidden = !previewOnly;
      if (elements.quickFlashFirmware) elements.quickFlashFirmware.disabled = previewOnly;
      if (elements.stockFirmwareSection) {
        elements.stockFirmwareSection.hidden = false;
        elements.stockFirmwareSection.open = false;
      }
      elements.customFirmwareBuilder.hidden = !PORTAL_BUILD_API_URL && !previewOnly;
      elements.customFirmwareBuilder.open = false;
      if (customSummary) customSummary.hidden = false;
    } else {
      elements.firmwareCurrentSection.hidden = false;
      if (elements.firmwareCurrentSummary) elements.firmwareCurrentSummary.hidden = false;
      if (elements.firmwareCurrentStaticTitle) elements.firmwareCurrentStaticTitle.hidden = true;
      if (elements.embeddedQuickFlash) elements.embeddedQuickFlash.hidden = false;
      if (elements.quickFlashFirmware) elements.quickFlashFirmware.disabled = false;
      elements.customFirmwareBuilder.open = false;
      if (customSummary) customSummary.hidden = false;
    }
  }
  const previewOnly = typeof globalThis.EMBEDDED_FIRMWARE_B64 !== 'string' && !PORTAL_BUILD_API_URL;
  elements.customFirmwareBuilder.hidden = !PORTAL_BUILD_API_URL && !previewOnly;
  if (!PORTAL_BUILD_API_URL && !previewOnly) return;
  elements.customBuildSignIn.disabled = !PORTAL_BUILD_CONFIGURED || customBuildAuthActive;
  renderCustomBuildConfiguration();
  updateCustomBuildEstimate();
  if (elements.customBuildProfileJson && !elements.customBuildProfileJson.value) {
    elements.customBuildProfileJson.value = JSON.stringify(customBuildProfile(), null, 2);
  }
  if (!PORTAL_BUILD_CONFIGURED) {
    elements.customBuildStart.disabled = true;
    elements.customBuildStatus.textContent =
      previewOnly ? 'Preview only: settings are editable; flashing and build actions are disabled.' :
        'Custom builds are not configured for this app build.';
    return;
  }
  elements.customBuildStart.disabled = !customBuildAccessToken || customBuildPollActive;
}

function updateCustomBuildEstimate() {
  const enabled = [...document.querySelectorAll('[data-build-feature]')]
    .filter((input) => input.checked).length;
  const {draft} = customBuildSettingDraft();
  const mutableSettings = Object.values(draft).filter((setting) => setting.mutable).length;
  const themeMutable = customBuildUnknownProfile.theme?.mutable ??
    customBuildUnknownProfile.features?.['theme.editable'] !== false;
  const totalOptions = CUSTOM_BUILD_FEATURES.length + Object.keys(draft).length + 1;
  const selectedOptions = enabled + mutableSettings + Number(themeMutable);
  const estimate = 30840 + Math.round((65280 - 30840) * selectedOptions / totalOptions);
  elements.customBuildEstimate.textContent =
    `Approximate Flash use: about ${(estimate / 1024).toFixed(1)} KB ` +
    '(rough estimate from measured Full and Minimal builds; exact size follows compilation).';
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
  if (!PORTAL_BUILD_CONFIGURED || customBuildAuthActive) return;
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
  const invalidColor = elements.customBuildConfigList.querySelector('.theme-color-code[aria-invalid="true"]');
  const invalidThemeField = invalidColor ||
    elements.customBuildConfigList.querySelector('.theme-dimming-control input[aria-invalid="true"]');
  if (invalidThemeField) {
    invalidThemeField.focus();
    elements.customBuildStatus.textContent = 'Correct invalid theme values before building.';
    return;
  }
  const profile = customBuildProfile();
  const idempotencyKey = crypto.randomUUID();
  elements.customBuildStatus.textContent = 'Submitting build request…';
  elements.customBuildDownload.hidden = true;
  elements.customBuildRun.hidden = true;
  try {
    const created = await customBuildApi('/api/builds', {
      method: 'POST',
      headers: {'Idempotency-Key': idempotencyKey},
      body: JSON.stringify({profile, sourceBranch: PORTAL_BUILD_BRANCH}),
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
elements.customBuildCopyProfile?.addEventListener('click', async () => {
  const profileJson = JSON.stringify(customBuildProfile(), null, 2);
  elements.customBuildProfileJson.value = profileJson;
  try {
    await navigator.clipboard.writeText(profileJson);
    elements.customBuildStatus.textContent = 'Build profile copied to clipboard.';
  } catch {
    elements.customBuildProfileJson.focus();
    elements.customBuildProfileJson.select();
    elements.customBuildStatus.textContent = 'Select and copy the build profile; clipboard access is unavailable.';
  }
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
    for (const feature of ['settings.mutable', 'theme.editable']) {
      if (parsed.features[feature] !== undefined && typeof parsed.features[feature] !== 'boolean')
        throw new Error(`Feature ${feature} must be true or false.`);
    }
    if (parsed.settings !== undefined &&
        (!parsed.settings || typeof parsed.settings !== 'object' || Array.isArray(parsed.settings))) {
      throw new Error('settings must be a JSON object.');
    }
    for (const setting of remoteSettings) {
      if ([14, 18, 19].includes(setting.id)) continue;
      const value = parsed.settings?.[customBuildSettingKey(setting)];
      if (value === undefined) continue;
      if (!value || typeof value !== 'object' || Array.isArray(value) ||
          ('mutable' in value && typeof value.mutable !== 'boolean') ||
          ('default' in value && (!Number.isInteger(value.default) ||
            value.default < 0 || value.default >= CUSTOM_BUILD_SETTING_OPTION_COUNTS[setting.id]))) {
        throw new Error(`Invalid build setting ${customBuildSettingKey(setting)}.`);
      }
    }
    if (parsed.theme !== undefined) {
      if (!parsed.theme || typeof parsed.theme !== 'object' || Array.isArray(parsed.theme) ||
          ('mutable' in parsed.theme && typeof parsed.theme.mutable !== 'boolean') ||
          ('preset' in parsed.theme && !THEME_PRESETS[parsed.theme.preset] && parsed.theme.preset !== 'custom') ||
          ('colors' in parsed.theme && (!Array.isArray(parsed.theme.colors) ||
            parsed.theme.colors.length !== THEME_COLORS.length ||
            parsed.theme.colors.some((color) => typeof color !== 'string' || !/^#[0-9a-f]{6}$/i.test(color)))) ||
          ('dimming' in parsed.theme && (!Number.isInteger(parsed.theme.dimming) ||
            parsed.theme.dimming < 0 || parsed.theme.dimming > 100))) {
        throw new Error('Invalid build theme.');
      }
    }
    customBuildUnknownProfile = {...parsed};
    for (const input of document.querySelectorAll('[data-build-feature]')) {
      const value = parsed.features[input.dataset.buildFeature];
      if (typeof value === 'boolean') input.checked = value;
    }
    renderCustomBuildConfiguration();
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
