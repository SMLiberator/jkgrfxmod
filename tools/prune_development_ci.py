#!/usr/bin/env python3
"""Prune obsolete *development CI* GitHub Actions runs and their artifacts.

Runs from other workflows, current/in-progress jobs and the most recent
40 CI executions are protected. Retain five most recent failures even if
older than the normal keep window. A bounded batch avoids API abuse limits.

Default is dry-run; --execute must be supplied by the authorized CI job.
The GitHub API deletes run artifacts together with the workflow run.
"""
import argparse
import json
import os
import sys
import time
import urllib.error
import urllib.parse
import urllib.request

API = "https://api.github.com"


def selection(runs, keep_latest=40, keep_failures=5):
    """Return oldest-first run IDs eligible for deletion; pure and testable."""
    protected = {r["id"] for r in runs[:keep_latest]}
    failed = 0
    for r in runs:
        if r.get("conclusion") == "failure" and failed < keep_failures:
            protected.add(r["id"])
            failed += 1
        if r.get("status") != "completed":
            protected.add(r["id"])
    # Filtering by workflow, branch and repository happens on the API query;
    # be defensive if a surprising result is returned by the endpoint.
    return [r["id"] for r in reversed(runs)
            if r["id"] not in protected and
            r.get("status") == "completed"]


def request(token, method, path):
    req = urllib.request.Request(
        API + path,
        method=method,
        headers={
            "Accept": "application/vnd.github+json",
            "Authorization": "Bearer " + token,
            "X-GitHub-Api-Version": "2022-11-28",
            "User-Agent": "jkgrfxmod-dev-run-pruner",
        },
    )
    with urllib.request.urlopen(req, timeout=30) as response:
        content = response.read()
    return json.loads(content) if content else None


def list_ci_runs(token, repo, branch):
    # Snapshot the full list *before* deleting runs so page shifts can't skip
    # or accidentally reclassify any run during pagination.
    result = []
    for page in range(1, 201):
        path = (
            f"/repos/{repo}/actions/workflows/main.yml/runs?"
            + urllib.parse.urlencode({
                "branch": branch,
                "per_page": 100,
                "page": page,
            })
        )
        batch = request(token, "GET", path)["workflow_runs"]
        result.extend(batch)
        if len(batch) < 100:
            return result
    raise RuntimeError("Safety stop: more than 20,000 CI runs; refusing cleanup")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--repo", default=os.environ.get("GITHUB_REPOSITORY", ""))
    parser.add_argument("--branch", default="development")
    parser.add_argument("--keep-latest", type=int, default=40)
    parser.add_argument("--keep-failures", type=int, default=5)
    parser.add_argument("--max-deletes", type=int, default=100)
    parser.add_argument("--execute", action="store_true", help="delete, not just list")
    parser.add_argument("--expected-head-sha", default="",
                        help="prune only when this CI run still builds branch HEAD")
    args = parser.parse_args()
    if not args.repo or "/" not in args.repo:
        parser.error("A GitHub owner/repo is required")
    if args.keep_latest < 20 or args.keep_failures < 0 or not 1 <= args.max_deletes <= 125:
        parser.error("Unsafe retention configuration")
    token = os.environ.get("GH_TOKEN") or os.environ.get("GITHUB_TOKEN")
    if not token:
        parser.error("GH_TOKEN/GITHUB_TOKEN is required")

    # Rapid development pushes can overlap CI runs. Only the newest one
    # needs to prune. This avoids overlapping bulk DELETE requests and
    # lets superseded CI builds finish green rather than being cancelled.
    if args.expected_head_sha:
        branch = request(
            token, "GET",
            f"/repos/{args.repo}/branches/{urllib.parse.quote(args.branch, safe='')}"
        )
        if branch["commit"]["sha"] != args.expected_head_sha:
            print("Prune skipped: superseded by a newer development push.")
            return

    runs = list_ci_runs(token, args.repo, args.branch)
    candidates = selection(runs, args.keep_latest, args.keep_failures)
    to_delete = candidates[:args.max_deletes]
    print(
        f"Development CI cleanup: {len(runs)} runs, "
        f"{len(candidates)} eligible, {len(to_delete)} selected; "
        f"mode={'DELETE' if args.execute else 'DRY RUN'}",
        flush=True,
    )
    failures = 0
    for idx, run_id in enumerate(to_delete, 1):
        if args.execute:
            try:
                request(
                    token, "DELETE",
                    f"/repos/{args.repo}/actions/runs/{run_id}",
                )
            except urllib.error.HTTPError as exc:
                if exc.code in (404, 410):
                    # A concurrent run or authorized user already removed it.
                    continue
                print(f"Failed deleting {run_id}: HTTP {exc.code}", file=sys.stderr)
                failures += 1
                if exc.code in (403, 429) or failures >= 5:
                    raise RuntimeError(
                        "Stopping deletion on permission/rate-limit errors"
                    ) from exc
            # Keep secondary API write-point rate safely below the threshold.
            time.sleep(0.45)
        if idx <= 5 or idx == len(to_delete):
            print(f"{idx}/{len(to_delete)}: {run_id}", flush=True)
    print(
        f"Deleted {len(to_delete) - failures} eligible CI runs; "
        f"{max(0, len(candidates) - len(to_delete))} older eligible runs "
        "can be pruned by subsequent development CI runs.",
        flush=True,
    )


if __name__ == "__main__":
    main()
