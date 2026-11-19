const API_VERSION = "2026-03-10";
const PROFILE_FORMAT = "geiger-custom-firmware-profile";
const WORKFLOW_FILE = "build-custom-firmware.yml";
const PROFILE_FEATURES = [
  "logs.enabled", "device.graphs", "device.debug",
  "indication.flashlight", "indication.modes", "alarms.rate",
  "alarms.dose", "theme.editable", "settings.mutable",
];
const PROFILE_SETTINGS = {
  led: 6, clicks: 6, "rate-screen": 4, "rate-light": 4, "rate-sound": 4,
  "led-bright": 9, "screen-bright": 9, "screen-timer": 6, "lead-zero": 2,
  unit: 3, "rate-prefix": 4, "dose-prefix": 4, "rate-row-2": 6,
  "log-memory": 3, hv: 2, "rate-custom": 9, "alarm-stop": 2, tone: 2,
  "long-press": 6, "dose-row-2": 8, "dose-period": 4, "dose-screen": 4,
  "dose-light": 4, "dose-sound": 4, "dose-custom": 8, "led-time": 9,
  "batt-format": 3, "led-color": 2, "rate-accuracy": 6,
};
const PROFILE_THEMES = ["dark", "light", "ocean", "amber", "violet", "monokai", "dracula", "nord", "solarized", "custom"];

function response(request, env, body, status = 200, extra = {}) {
  const origin = request.headers.get("Origin") || "";
  const allowed = origin === env.PORTAL_ORIGIN;
  return new Response(body === null ? null : JSON.stringify(body), {
    status,
    headers: {
      "Content-Type": "application/json; charset=utf-8",
      "Cache-Control": "no-store",
      "Access-Control-Allow-Origin": allowed ? origin : "null",
      "Access-Control-Allow-Methods": "GET, POST, OPTIONS",
      "Access-Control-Allow-Headers": "Authorization, Content-Type, Idempotency-Key",
      "Vary": "Origin",
      ...extra,
    },
  });
}

function fail(request, env, status, message) {
  return response(request, env, {error: message}, status);
}

function base64Url(bytes) {
  let binary = "";
  for (const byte of bytes) binary += String.fromCharCode(byte);
  return btoa(binary).replace(/=/g, "").replace(/\+/g, "-").replace(/\//g, "_");
}

function pemBytes(pem) {
  const body = pem.replace(/-----BEGIN [^-]+-----|-----END [^-]+-----|\s/g, "");
  return Uint8Array.from(atob(body), (character) => character.charCodeAt(0));
}

function pkcs1ToPkcs8(key) {
  if (key.length > 0xffff) throw new Error("GitHub App key is too large.");
  const contentLength = key.length + 22;
  const header = Uint8Array.of(
    0x30, 0x82, contentLength >> 8, contentLength & 0xff,
    0x02, 0x01, 0x00,
    0x30, 0x0d, 0x06, 0x09, 0x2a, 0x86, 0x48, 0x86, 0xf7, 0x0d, 0x01, 0x01, 0x01,
    0x05, 0x00, 0x04, 0x82, key.length >> 8, key.length & 0xff,
  );
  const wrapped = new Uint8Array(header.length + key.length);
  wrapped.set(header);
  wrapped.set(key, header.length);
  return wrapped;
}

async function appJwt(env) {
  const now = Math.floor(Date.now() / 1000);
  const header = base64Url(new TextEncoder().encode(JSON.stringify({alg: "RS256", typ: "JWT"})));
  const payload = base64Url(new TextEncoder().encode(JSON.stringify({
    iat: now - 30, exp: now + 540, iss: String(env.GITHUB_APP_ID),
  })));
  const signingInput = `${header}.${payload}`;
  const rawKey = pemBytes(env.GITHUB_APP_PRIVATE_KEY);
  const pkcs8 = env.GITHUB_APP_PRIVATE_KEY.includes("BEGIN RSA PRIVATE KEY") ?
    pkcs1ToPkcs8(rawKey) : rawKey;
  const key = await crypto.subtle.importKey("pkcs8", pkcs8, {
    name: "RSASSA-PKCS1-v1_5", hash: "SHA-256",
  }, false, ["sign"]);
  const signature = await crypto.subtle.sign("RSASSA-PKCS1-v1_5", key,
    new TextEncoder().encode(signingInput));
  return `${signingInput}.${base64Url(new Uint8Array(signature))}`;
}

async function installationToken(env) {
  const jwt = await appJwt(env);
  const result = await fetch(
    `https://api.github.com/app/installations/${env.GITHUB_INSTALLATION_ID}/access_tokens`,
    {method: "POST", headers: githubHeaders(jwt), body: JSON.stringify({
      permissions: {actions: "write", contents: "read"},
    })});
  const data = await result.json();
  if (!result.ok || !data.token) throw new Error(`Could not authorize GitHub App (${result.status}).`);
  return data.token;
}

function githubHeaders(token) {
  return {
    "Accept": "application/vnd.github+json",
    "Authorization": `Bearer ${token}`,
    "X-GitHub-Api-Version": API_VERSION,
    "User-Agent": "Geiger-Custom-Firmware-Builder",
  };
}

function bearer(request) {
  const match = /^Bearer\s+(.+)$/i.exec(request.headers.get("Authorization") || "");
  return match?.[1] || "";
}

async function readJson(request) {
  const contentLength = Number(request.headers.get("Content-Length") || 0);
  if (contentLength > 32768) throw new Error("Request body is too large.");
  const text = await request.text();
  if (new TextEncoder().encode(text).length > 32768) throw new Error("Request body is too large.");
  return JSON.parse(text);
}

async function authenticatedUser(request, env) {
  const token = bearer(request);
  if (!token) throw new Response("GitHub sign-in is required.", {status: 401});
  const result = await fetch("https://api.github.com/user", {headers: githubHeaders(token)});
  if (!result.ok) throw new Response("GitHub sign-in has expired.", {status: 401});
  const user = await result.json();
  const allowed = (env.ALLOWED_GITHUB_USERS || "").split(",")
    .map((name) => name.trim().toLowerCase()).filter(Boolean);
  if (!allowed.includes(String(user.login || "").toLowerCase())) {
    throw new Response("This GitHub account is not enabled for custom builds.", {status: 403});
  }
  return String(user.login);
}

function validateProfile(profile) {
  if (!profile || typeof profile !== "object" || Array.isArray(profile) ||
      profile.format !== PROFILE_FORMAT ||
      !Number.isInteger(profile.schemaVersion) || profile.schemaVersion < 1 ||
      !profile.features || typeof profile.features !== "object" ||
      Array.isArray(profile.features)) {
    throw new Error("Invalid firmware build profile format.");
  }
  for (const key of PROFILE_FEATURES) {
    if (key in profile.features && typeof profile.features[key] !== "boolean") {
      throw new Error(`Feature ${key} must be true or false.`);
    }
  }
  if (profile.settings !== undefined &&
      (!profile.settings || typeof profile.settings !== "object" || Array.isArray(profile.settings))) {
    throw new Error("settings must be a JSON object.");
  }
  for (const [key, setting] of Object.entries(profile.settings || {})) {
    if (!(key in PROFILE_SETTINGS)) continue;
    if (!setting || typeof setting !== "object" || Array.isArray(setting) ||
        ("mutable" in setting && typeof setting.mutable !== "boolean") ||
        ("default" in setting && (!Number.isInteger(setting.default) ||
          setting.default < 0 || setting.default >= PROFILE_SETTINGS[key]))) {
      throw new Error(`Invalid build setting ${key}.`);
    }
  }
  if (profile.theme !== undefined &&
      (!profile.theme || typeof profile.theme !== "object" || Array.isArray(profile.theme) ||
       ("mutable" in profile.theme && typeof profile.theme.mutable !== "boolean") ||
       ("preset" in profile.theme && !PROFILE_THEMES.includes(profile.theme.preset)) ||
       ("colors" in profile.theme && (!Array.isArray(profile.theme.colors) ||
         profile.theme.colors.length !== 7 ||
         profile.theme.colors.some((color) => typeof color !== "string" ||
           !/^#[0-9a-f]{6}$/i.test(color)))) ||
       ("dimming" in profile.theme && (!Number.isInteger(profile.theme.dimming) ||
         profile.theme.dimming < 0 || profile.theme.dimming > 100)))) {
    throw new Error("Invalid build theme.");
  }
  return profile;
}

async function authDeviceStart(request, env) {
  if (!env.GITHUB_OAUTH_CLIENT_ID) return fail(request, env, 503, "GitHub sign-in is not configured.");
  const ip = request.headers.get("CF-Connecting-IP") || "unknown";
  const rateKey = `device-start:${ip}`;
  if (await env.BUILD_JOBS.get(rateKey))
    return fail(request, env, 429, "Wait a minute before requesting another GitHub code.");
  await env.BUILD_JOBS.put(rateKey, "1", {expirationTtl: 60});
  const result = await fetch("https://github.com/login/device/code", {
    method: "POST",
    headers: {"Accept": "application/json", "Content-Type": "application/json"},
    body: JSON.stringify({client_id: env.GITHUB_OAUTH_CLIENT_ID, scope: "read:user"}),
  });
  const data = await result.json();
  if (!result.ok || data.error) return fail(request, env, 502, "Could not start GitHub sign-in.");
  return response(request, env, data);
}

async function authDevicePoll(request, env) {
  if (!env.GITHUB_OAUTH_CLIENT_ID) return fail(request, env, 503, "GitHub sign-in is not configured.");
  const body = await readJson(request);
  if (typeof body.device_code !== "string" || body.device_code.length > 256) {
    return fail(request, env, 400, "Invalid device authorization code.");
  }
  const form = new URLSearchParams({
    client_id: env.GITHUB_OAUTH_CLIENT_ID,
    device_code: body.device_code,
    grant_type: "urn:ietf:params:oauth:grant-type:device_code",
  });
  const result = await fetch("https://github.com/login/oauth/access_token", {
    method: "POST", headers: {"Accept": "application/json"}, body: form,
  });
  return response(request, env, await result.json(), result.ok ? 200 : 502);
}

async function createBuild(request, env) {
  const user = await authenticatedUser(request, env);
  const body = await readJson(request);
  const sourceBranch = body.sourceBranch;
  if (sourceBranch !== "dev" && sourceBranch !== "main") {
    return fail(request, env, 400, "Custom build source branch must be dev or main.");
  }
  const profile = validateProfile(body.profile);
  const idem = request.headers.get("Idempotency-Key") || "";
  if (!/^[a-zA-Z0-9_-]{12,100}$/.test(idem)) {
    return fail(request, env, 400, "A valid Idempotency-Key is required.");
  }
  const idemKey = `idem:${user.toLowerCase()}:${idem}`;
  const previousId = await env.BUILD_JOBS.get(idemKey);
  if (previousId) {
    const previous = await env.BUILD_JOBS.get(`job:${previousId}`, "json");
    if (previous) return response(request, env, {
      buildId: previousId, statusUrl: `/api/builds/${previousId}`, reused: true,
    }, 200);
  }
  const cooldownKey = `cooldown:${user.toLowerCase()}`;
  if (await env.BUILD_JOBS.get(cooldownKey)) {
    return fail(request, env, 429, "Please wait before starting another build.");
  }
  const token = await installationToken(env);
  const endpoint = `https://api.github.com/repos/${env.REPOSITORY_OWNER}/${env.REPOSITORY_NAME}` +
    `/actions/workflows/${WORKFLOW_FILE}/dispatches`;
  const dispatch = await fetch(endpoint, {
    method: "POST",
    headers: githubHeaders(token),
    body: JSON.stringify({ref: sourceBranch, inputs: {profile_json: JSON.stringify(profile)}}),
  });
  const result = await dispatch.json().catch(() => ({}));
  if (!dispatch.ok || !result.workflow_run_id) {
    return fail(request, env, 502, `GitHub did not start the build (${dispatch.status}).`);
  }
  const buildId = crypto.randomUUID();
  const record = {
    owner: user,
    runId: result.workflow_run_id,
    runUrl: result.html_url || result.run_url || "",
    createdAt: new Date().toISOString(),
  };
  await Promise.all([
    env.BUILD_JOBS.put(`job:${buildId}`, JSON.stringify(record), {expirationTtl: 86400}),
    env.BUILD_JOBS.put(idemKey, buildId, {expirationTtl: 86400}),
    env.BUILD_JOBS.put(cooldownKey, buildId, {expirationTtl: 300}),
  ]);
  return response(request, env, {
    buildId, statusUrl: `/api/builds/${buildId}`, runUrl: record.runUrl, reused: false,
  }, 202);
}

async function buildStatus(request, env, buildId) {
  const user = await authenticatedUser(request, env);
  if (!/^[0-9a-f-]{36}$/i.test(buildId)) return fail(request, env, 404, "Build not found.");
  const record = await env.BUILD_JOBS.get(`job:${buildId}`, "json");
  if (!record || record.owner.toLowerCase() !== user.toLowerCase()) {
    return fail(request, env, 404, "Build not found.");
  }
  const token = await installationToken(env);
  const base = `https://api.github.com/repos/${env.REPOSITORY_OWNER}/${env.REPOSITORY_NAME}`;
  const result = await fetch(`${base}/actions/runs/${record.runId}`, {
    headers: githubHeaders(token),
  });
  const run = await result.json();
  if (!result.ok) return fail(request, env, 502, "Could not read GitHub build status.");
  const output = {
    buildId,
    status: run.status === "completed" ? run.conclusion : run.status,
    conclusion: run.conclusion,
    runUrl: run.html_url || record.runUrl,
  };
  if (run.status === "completed" && run.conclusion === "success") {
    const artifactsResponse = await fetch(`${base}/actions/runs/${record.runId}/artifacts`, {
      headers: githubHeaders(token),
    });
    const artifacts = await artifactsResponse.json();
    const artifact = (artifacts.artifacts || []).find((item) =>
      item.name === `custom-firmware-${record.runId}` && !item.expired);
    if (artifact) {
      output.artifactUrl = artifact.archive_download_url;
      output.expiresAt = artifact.expires_at;
    }
  }
  return response(request, env, output);
}

export default {
  async fetch(request, env) {
    const url = new URL(request.url);
    if (request.method === "OPTIONS") return response(request, env, null, 204);
    if (url.origin === "null" || request.headers.get("Origin") !== env.PORTAL_ORIGIN) {
      return fail(request, env, 403, "Origin is not allowed.");
    }
    try {
      if (request.method === "POST" && url.pathname === "/api/auth/device")
        return await authDeviceStart(request, env);
      if (request.method === "POST" && url.pathname === "/api/auth/poll")
        return await authDevicePoll(request, env);
      if (request.method === "POST" && url.pathname === "/api/builds")
        return await createBuild(request, env);
      const match = /^\/api\/builds\/([0-9a-f-]{36})$/.exec(url.pathname);
      if (request.method === "GET" && match)
        return await buildStatus(request, env, match[1]);
      return fail(request, env, 404, "Not found.");
    } catch (error) {
      if (error instanceof Response) return fail(request, env, error.status, await error.text());
      if (error instanceof SyntaxError) return fail(request, env, 400, "Request body must be valid JSON.");
      return fail(request, env, 400, error.message || "Invalid request.");
    }
  },
};
