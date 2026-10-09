#!/usr/bin/env python3
# SPDX-FileCopyrightText: 2026 Brando Giuffrida
# SPDX-License-Identifier: GPL-3.0-or-later
"""Archive GitHub distribution metrics; no Vela client-side telemetry."""

import argparse
from datetime import datetime, timezone
import json
import os
from pathlib import Path
import urllib.error
import urllib.request


def github_json(url, token):
    headers = {
        "Accept": "application/vnd.github+json",
        "User-Agent": "vela-distribution-metrics",
        "X-GitHub-Api-Version": "2022-11-28",
    }
    if token:
        headers["Authorization"] = "Bearer " + token
    request = urllib.request.Request(url, headers=headers)
    try:
        with urllib.request.urlopen(request, timeout=30) as response:
            return json.load(response)
    except urllib.error.HTTPError as error:
        # Never include credentials or an authenticated request in the error.
        raise RuntimeError(f"GitHub API returned HTTP {error.code} for {url}") from None


def paginate(url, token):
    page = 1
    while True:
        separator = "&" if "?" in url else "?"
        result = github_json(f"{url}{separator}per_page=100&page={page}", token)
        if not isinstance(result, list):
            raise RuntimeError(f"Expected a GitHub API list from {url}")
        yield from result
        if len(result) < 100:
            break
        page += 1


def read_json(path, default):
    if not path.exists():
        return default
    with path.open(encoding="utf-8") as handle:
        return json.load(handle)


def write_json(path, value):
    path.parent.mkdir(parents=True, exist_ok=True)
    with path.open("w", encoding="utf-8") as handle:
        json.dump(value, handle, indent=2, sort_keys=True, ensure_ascii=False)
        handle.write("\n")


def merge_traffic(history, kind, response):
    daily = history.setdefault(kind, {})
    for entry in response.get(kind, []):
        day = entry["timestamp"][:10]
        daily[day] = {"count": int(entry["count"]), "uniques": int(entry["uniques"])}
    return history


def downloads_delta(previous_assets, current_assets):
    """Interval downloads; asset deletion must not create negative downloads."""
    return sum(
        max(0, info["downloads"] - previous_assets.get(asset_id, {}).get("downloads", 0))
        for asset_id, info in current_assets.items()
    )


def collect(repo, output, releases_token, traffic_token, now=None):
    now = now or datetime.now(timezone.utc)
    day = now.date().isoformat()
    timestamp = now.isoformat(timespec="seconds")
    api = f"https://api.github.com/repos/{repo}"

    # Fetch all pages of releases AND assets, not just the truncated embedded list.
    releases = list(paginate(api + "/releases", releases_token))
    assets = {}
    for release in releases:
        for asset in paginate(api + f"/releases/{release['id']}/assets", releases_token):
            assets[str(asset["id"])] = {
                "name": asset["name"],
                "release_tag": release["tag_name"],
                "downloads": int(asset["download_count"]),
            }

    traffic_path = output / "data" / "traffic-daily.json"
    history = read_json(traffic_path, {"clones": {}, "views": {}})
    traffic_status = "not_configured"
    if traffic_token:
        for kind in ("clones", "views"):
            response = github_json(api + f"/traffic/{kind}?per=day", traffic_token)
            merge_traffic(history, kind, response)
        traffic_status = "ok"
        history["last_collected_at"] = timestamp
        write_json(traffic_path, history)

    snapshots = output / "data" / "release-snapshots"
    earlier = sorted(p for p in snapshots.glob("*.json") if p.stem < day)
    previous_date = earlier[-1].stem if earlier else None
    previous = read_json(earlier[-1], {}) if earlier else None
    interval_downloads = downloads_delta(previous["assets"], assets) if previous else None

    write_json(
        snapshots / f"{day}.json",
        {
            "collected_at": timestamp,
            "releases": len(releases),
            "assets": assets,
            "total_downloads_current_assets": sum(a["downloads"] for a in assets.values()),
        },
    )
    latest = {
        "collected_at": timestamp,
        "repository": repo,
        "traffic_status": traffic_status,
        "releases": len(releases),
        "release_assets": len(assets),
        "release_asset_downloads_total": sum(a["downloads"] for a in assets.values()),
        "downloads_since_previous_snapshot": interval_downloads,
        "previous_snapshot_date": previous_date,
    }
    write_json(output / "data" / "latest.json", latest)
    (output / "README.md").write_text(
        "# Vela distribution metrics archive\n\n"
        "This branch is generated automatically by a GitHub Actions workflow. "
        "It contains aggregate GitHub repository traffic and GitHub Release asset "
        "download counters, **not telemetry collected from Vela users**.\n\n"
        "- `data/traffic-daily.json`: rolling GitHub clone/view daily counts "
        "retained across runs. GitHub exposes only the most recent 14 days. "
        "The daily `uniques` counts must not be added together to infer unique people.\n"
        "- `data/release-snapshots/YYYY-MM-DD.json`: the most recent sample "
        "on each UTC date, by release asset ID. Counts are cumulative per asset.\n"
        "- `data/latest.json`: latest totals and downloads since the preceding "
        "available day's snapshot. If there is no previous snapshot, the delta is null. "
        "That delta covers the **sampling interval**, not necessarily a calendar day.\n"
        "- A missing traffic token is reported as `not_configured`, never as zero clones.\n"
        "- GitHub Actions artifacts, source archives, local builds and installations "
        "are **not** counted as release asset downloads.\n\n"
        "The metrics branch is publicly readable because the Vela repository is public.\n",
        encoding="utf-8",
    )
    print(json.dumps(latest, indent=2, sort_keys=True))
    return latest


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--output", type=Path, required=True)
    parser.add_argument("--repo", default=os.getenv("GITHUB_REPOSITORY", "BrandoDev/vela"))
    args = parser.parse_args()
    collect(
        args.repo,
        args.output,
        os.getenv("GITHUB_TOKEN", ""),
        os.getenv("VELA_TRAFFIC_TOKEN", ""),
    )


if __name__ == "__main__":
    main()
