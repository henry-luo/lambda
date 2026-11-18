#!/usr/bin/env python3
"""Commit initialized Lambda repositories from nested leaves to the root."""

import os
from pathlib import Path
import subprocess
import sys

from push_lambda import collect, commit_changes, git


def commit_tree(node, message):
    repo, children = node
    for child in children:
        commit_tree(child, message)
    if commit_changes(repo, message):
        print(f"Committed {repo}", flush=True)
    else:
        print(f"No changes to commit in {repo}", flush=True)


def main():
    message = (os.environ.get("message") or os.environ.get("MESSAGE", "")).strip()
    if not message:
        raise ValueError("provide a commit message: make commit-lambda message='...'")
    root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel")).resolve()
    commit_tree(collect(root), message)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, OSError, subprocess.CalledProcessError) as exc:
        print(f"commit-lambda: {exc}", file=sys.stderr)
        sys.exit(1)
