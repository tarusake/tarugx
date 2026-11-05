#!/usr/bin/env python3
# SPDX-License-Identifier: Apache-2.0 WITH SHL-2.1
"""Reject commits that contain information unsuitable for the public repository."""

from __future__ import annotations

import argparse
from pathlib import PurePosixPath
import re
import subprocess
import sys


ZERO = "0" * 40
ALLOW_MARKER = "public-check: allow"
FORBIDDEN_PARTS = {".local", "build", "docs"}
FORBIDDEN_NAMES = {
    ".env",
    "id_dsa",
    "id_ecdsa",
    "id_ed25519",
    "id_rsa",
}
FORBIDDEN_SUFFIXES = (".key", ".p12", ".pfx", ".pem")
PATTERNS = (
    ("private key", re.compile(r"-----BEGIN (?:[A-Z0-9]+ )?PRIVATE KEY-----")),
    ("GitHub token", re.compile(r"\b(?:github_pat_|gh[oprsu]_[A-Za-z0-9_]{20,})")),  # public-check: allow
    ("AWS access key", re.compile(r"\b(?:AKIA|ASIA)[A-Z0-9]{16}\b")),
    ("Slack token", re.compile(r"\bxox[baprs]-[A-Za-z0-9-]{10,}\b")),
    ("credential in URL", re.compile(r"(?:https?|ssh)://[^\s/:]+:[^\s/@]+@")),
    (
        "assigned secret",
        re.compile(
            r"(?i)\b(?:api[_-]?key|access[_-]?token|auth[_-]?token|password|passwd|secret)"
            r"\s*[:=]\s*['\"]?[^\s'\"$<{][^\s'\"]{7,}"
        ),
    ),
    (
        "private network address",
        re.compile(
            r"\b(?:10(?:\.\d{1,3}){3}|192\.168(?:\.\d{1,3}){2}|"
            r"172\.(?:1[6-9]|2\d|3[01])(?:\.\d{1,3}){2})\b"
        ),
    ),
    ("local home path", re.compile(r"/(?:home|Users)/[A-Za-z0-9._-]+/")),
)


def git(*args: str, text: bool = True) -> str | bytes:
    return subprocess.check_output(("git", *args), text=text, stderr=subprocess.DEVNULL)


def commits(base: str, head: str) -> list[str]:
    if base != ZERO:
        revision_args = (f"{base}..{head}",)
    else:
        # For a new branch, inspect every commit that is not already public on origin.
        revision_args = (head, "--not", "--remotes=origin")
    output = git("rev-list", "--reverse", *revision_args)
    return output.splitlines()


def changed_paths(commit: str) -> list[str]:
    output = git(
        "diff-tree", "--root", "--no-commit-id", "--name-only",
        "--diff-filter=AMCR", "-r", commit,
    )
    return output.splitlines()


def forbidden_path(path: str) -> str | None:
    item = PurePosixPath(path)
    if FORBIDDEN_PARTS.intersection(item.parts):
        return "private/local directory"
    if item.name in FORBIDDEN_NAMES or item.name.startswith(".env."):
        return "credential file name"
    if item.name.lower().endswith(FORBIDDEN_SUFFIXES):
        return "private credential/key file"
    return None


def scan_blob(commit: str, path: str) -> list[tuple[int, str]]:
    try:
        blob = git("show", f"{commit}:{path}", text=False)
    except subprocess.CalledProcessError:
        return []
    if b"\0" in blob[:8192]:
        return []
    text = blob.decode("utf-8", errors="replace")
    findings = []
    for number, line in enumerate(text.splitlines(), 1):
        if ALLOW_MARKER in line:
            continue
        for label, pattern in PATTERNS:
            if pattern.search(line):
                findings.append((number, label))
    return findings


def main() -> int:
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("base", help="remote/base commit, or forty zeroes for a new ref")
    parser.add_argument("head", help="local/head commit")
    args = parser.parse_args()

    failures: list[str] = []
    seen: set[tuple[str, str]] = set()
    try:
        revisions = commits(args.base, args.head)
    except subprocess.CalledProcessError:
        print("public check: unable to determine commits", file=sys.stderr)
        return 2

    for commit in revisions:
        for path in changed_paths(commit):
            key = (commit, path)
            if key in seen:
                continue
            seen.add(key)
            reason = forbidden_path(path)
            if reason:
                failures.append(f"{commit[:12]} {path}: {reason}")
                continue
            for line, label in scan_blob(commit, path):
                failures.append(f"{commit[:12]} {path}:{line}: {label}")

    if failures:
        print("Public information check FAILED:", file=sys.stderr)
        for failure in failures:
            print(f"  {failure}", file=sys.stderr)
        print(
            f"Use '{ALLOW_MARKER}' only for an intentionally public example value.",
            file=sys.stderr,
        )
        return 1

    print(f"Public information check passed ({len(revisions)} commit(s)).")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
