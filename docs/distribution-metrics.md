# GitHub distribution metrics

This job collects **GitHub-side aggregate statistics only**. It makes no changes
to Vela's binaries, build system or runtime. It runs daily at 03:17 UTC and
can also be triggered from Actions → Distribution metrics → Run workflow.

## What it records

- Clone counts and daily unique cloners, and page views, for the rolling
  14-day GitHub Traffic API window (only when configured).
- Cumulative download counts for GitHub Release assets; snapshots per UTC
  day and download deltas between available snapshots.
- No GitHub Actions artifact downloads, local builds, package manager installs
  or source archive downloads (GitHub doesn't expose these as Release asset counts).

The output is kept on a **separate public branch, `metrics`**: its
`README.md`, `data/latest.json`, `data/traffic-daily.json` (when traffic
is enabled), and `data/release-snapshots/`. The first successful workflow
run creates this branch.

## One-time authorization for GitHub Traffic

The standard GitHub Actions `GITHUB_TOKEN` generally cannot access the
Traffic API: that endpoint requires repository **Administration: read**.
Create a **fine-grained personal access token**, with access restricted to
only `BrandoDev/vela` and repository permission **Administration: Read-only**.
Then add it as an Actions repository secret named `VELA_TRAFFIC_TOKEN` under
Settings → Secrets and variables → Actions.

The token is never written into output files. If it isn't configured, the
workflow still collects Release download counts, marks Traffic as
`not_configured`, and does **not** claim clone counts are zero. If a token
is configured but invalid or cannot access the Traffic API, the job fails;
fix the token and rerun. This prevents incomplete traffic data being
silently described as complete.

The workflow requires GitHub Actions to have permission to write repository
contents for its `metrics` branch. Repository Settings → Actions → General →
Workflow permissions must allow the job to write, and any matching rulesets
must permit the push. No third-party analytics provider is required.

## Accuracy

GitHub Traffic covers approximately the past 14 days; if an execution
gap exceeds that window, missing history cannot be recovered from this API.
Daily `uniques` should not be summed to count distinct users over a month.
Release download counters are per asset, and may include automated requests.
Asset deletion or republishing can change what appears in the latest totals;
old snapshots remain archived. The first sample is only a baseline, not
a valid daily increment. Subsequent increments cover the period between
snapshots, even when more than one day elapsed. No historical download
timing can be reconstructed before the first snapshot.

The workflow is active only once merged into the repository's default branch,
because GitHub schedules workflows from the default branch.
