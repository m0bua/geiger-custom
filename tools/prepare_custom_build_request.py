"""Validate a GitHub issue build request and prepare its firmware profile."""

from __future__ import annotations

import json
import os
import re
import sys
import urllib.parse
import urllib.request
import uuid
from datetime import datetime, timedelta, timezone
from pathlib import Path

from firmware_profile import normalize_profile, write_profile


REQUEST_MARKER = "<!-- geiger-custom-firmware-request:v1 -->"
MAX_ISSUE_BODY_BYTES = 64 * 1024
MAX_PROFILE_BYTES = 32 * 1024
MAX_BUILDS_PER_USER_PER_DAY = 3


def api_get(url: str, token: str) -> dict:
    request = urllib.request.Request(url, headers={
        "Accept": "application/vnd.github+json",
        "Authorization": f"Bearer {token}",
        "X-GitHub-Api-Version": "2022-11-28",
        "User-Agent": "Geiger-Custom-Firmware-Builder",
    })
    with urllib.request.urlopen(request, timeout=20) as response:
        return json.load(response)


def enforce_issue_rate_limit(repository: str, author: str, token: str) -> None:
    cutoff = datetime.now(timezone.utc) - timedelta(days=1)
    query = urllib.parse.urlencode({
        "creator": author,
        "state": "all",
        "since": cutoff.isoformat().replace("+00:00", "Z"),
        "sort": "created",
        "direction": "desc",
        "per_page": 100,
    })
    url = f"https://api.github.com/repos/{repository}/issues?{query}"
    issues = api_get(url, token)
    accepted = 0
    for issue in issues:
        if issue.get("pull_request") or not str(issue.get("title", "")).startswith("[Custom firmware]"):
            continue
        try:
            created = datetime.fromisoformat(issue["created_at"].replace("Z", "+00:00"))
        except (KeyError, ValueError):
            continue
        if created >= cutoff:
            accepted += 1
            # The just-opened request is already visible in this API response.
            if accepted > MAX_BUILDS_PER_USER_PER_DAY:
                raise ValueError("Build request limit reached: up to 3 custom builds per GitHub account in 24 hours.")


def issue_request(event: dict) -> tuple[str, str, dict, str]:
    issue = event.get("issue") or {}
    body = issue.get("body")
    title = issue.get("title")
    author = (issue.get("user") or {}).get("login")
    if not isinstance(body, str) or len(body.encode("utf-8")) > MAX_ISSUE_BODY_BYTES:
        raise ValueError("The GitHub issue body is missing or too large.")
    if not isinstance(title, str) or not title.startswith("[Custom firmware]"):
        raise ValueError("This issue is not a custom firmware build request.")
    if not isinstance(author, str) or not author:
        raise ValueError("Could not identify the GitHub account that opened this request.")
    if body.count(REQUEST_MARKER) != 1:
        raise ValueError("The custom firmware request marker is missing or duplicated.")

    branch = re.search(r"(?m)^Source branch: (dev|main)\s*$", body)
    request_id = re.search(r"(?m)^Request ID: ([0-9a-fA-F-]{36})\s*$", body)
    profile_block = re.search(r"(?ms)^```json\s*\n(.*?)\n```\s*$", body)
    if not branch or not request_id or not profile_block:
        raise ValueError("The request must contain a source branch, request ID, and JSON profile.")
    try:
        normalized_id = str(uuid.UUID(request_id.group(1)))
        raw_profile = profile_block.group(1)
        if len(raw_profile.encode("utf-8")) > MAX_PROFILE_BYTES:
            raise ValueError("Build profile exceeds the 32 KiB limit.")
        profile = normalize_profile(json.loads(raw_profile))
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"Invalid firmware profile: {error}") from error
    return branch.group(1), normalized_id, profile, author


def dispatch_request(event: dict, profile_text: str) -> tuple[str, str, dict, str]:
    inputs = event.get("inputs") or {}
    branch = os.environ.get("DISPATCH_BRANCH") or inputs.get("source_branch")
    if branch not in ("dev", "main"):
        raise ValueError("Select dev or main as the firmware source branch.")
    if not profile_text or len(profile_text.encode("utf-8")) > MAX_PROFILE_BYTES:
        raise ValueError("Build profile is missing or exceeds the 32 KiB limit.")
    try:
        profile = normalize_profile(json.loads(profile_text))
    except (json.JSONDecodeError, ValueError) as error:
        raise ValueError(f"Invalid firmware profile: {error}") from error
    return branch, str(uuid.uuid4()), profile, ""


def main() -> int:
    event_path = Path(os.environ["GITHUB_EVENT_PATH"])
    event = json.loads(event_path.read_text(encoding="utf-8"))
    event_name = os.environ.get("GITHUB_EVENT_NAME", "")
    if event_name == "issues":
        branch, request_id, profile, author = issue_request(event)
        enforce_issue_rate_limit(
            os.environ["GITHUB_REPOSITORY"], author, os.environ["GH_TOKEN"])
    elif event_name == "workflow_dispatch":
        branch, request_id, profile, author = dispatch_request(
            event, os.environ.get("PROFILE_JSON", ""))
    else:
        raise ValueError(f"Unsupported request event: {event_name!r}.")

    output = Path(".pio/scratch/submitted-profile.json")
    output.parent.mkdir(parents=True, exist_ok=True)
    write_profile(output, profile)
    with Path(os.environ["GITHUB_OUTPUT"]).open("a", encoding="utf-8") as stream:
        stream.write(f"source_branch={branch}\n")
        stream.write(f"request_id={request_id}\n")
        stream.write(f"issue_number={(event.get('issue') or {}).get('number', '')}\n")
    return 0


if __name__ == "__main__":
    try:
        raise SystemExit(main())
    except Exception as error:
        print(f"Request rejected: {error}", file=sys.stderr)
        raise SystemExit(1)
