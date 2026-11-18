#!/usr/bin/env python3
"""Commit and publish initialized repositories from the leaves upward."""

import os
from pathlib import Path
import subprocess
import sys


def git(repo, *args, check=True):
    return subprocess.run(
        ["git", "-C", str(repo), *args], text=True, stdout=subprocess.PIPE,
        check=check,
    ).stdout.strip()


def collect(repo):
    if git(repo, "rev-parse", "--show-toplevel") != str(repo):
        raise ValueError(f"submodule is not initialized: {repo}")
    if git(repo, "ls-files", "-u"):
        raise ValueError(f"resolve conflicts first: {repo}")
    children = []
    # Gitlinks, not directory traversal, determine which repositories belong here.
    entries = git(repo, "ls-files", "--stage", "-z").split("\0")
    for entry in entries:
        if entry and entry.startswith("160000 "):
            children.append(collect(repo / entry.split("\t", 1)[1]))
    return (repo, children)


def is_ancestor(repo, older, newer):
    result = subprocess.run(["git", "-C", str(repo), "merge-base", "--is-ancestor",
                             older, newer])
    if result.returncode not in (0, 1):
        raise ValueError(f"could not compare commits in {repo}")
    return result.returncode == 0


def detached_target(repo):
    # Use only a unique nearest ancestor: a default branch can be unrelated.
    refs = git(repo, "for-each-ref", "--format=%(refname) %(objectname) %(symref)",
               "refs/remotes/origin/").splitlines()
    candidates = []
    for line in refs:
        parts = line.split()
        if len(parts) != 2 or not is_ancestor(repo, parts[1], "HEAD"):
            continue
        candidates.append((parts[0], parts[1]))
    nearest = [(ref, commit) for ref, commit in candidates if not any(
        commit != other and is_ancestor(repo, commit, other)
        for _, other in candidates)]
    if len(nearest) != 1:
        raise ValueError(f"cannot select a unique origin branch for detached checkout: {repo}; switch to a branch")
    return "refs/heads/" + nearest[0][0].removeprefix("refs/remotes/origin/")


def commit_changes(repo, message):
    git(repo, "add", "-A")
    merge = git(repo, "rev-parse", "--git-path", "MERGE_HEAD")
    merge_path = Path(merge) if os.path.isabs(merge) else repo / merge
    staged = subprocess.run(["git", "-C", str(repo), "diff", "--cached", "--quiet"]).returncode
    if staged not in (0, 1):
        raise ValueError(f"could not inspect staged changes: {repo}")
    if staged or merge_path.exists():
        git(repo, "commit", "-m", message)
        return True
    return False


def prepare(node):
    repo, children = node
    dirty_child = any([prepare(child) for child in children])
    dirty = bool(git(repo, "status", "--porcelain", "--untracked-files=normal",
                     "--ignore-submodules=none")) or dirty_child
    branch = git(repo, "symbolic-ref", "--quiet", "--short", "HEAD", check=False)
    git(repo, "fetch", "origin", "--prune")
    if not branch:
        published = git(repo, "branch", "-r", "--contains", "HEAD", "--list", "origin/*")
        if dirty or not published:
            detached_target(repo)
    else:
        remote = git(repo, "config", "--get", f"branch.{branch}.remote", check=False)
        target = git(repo, "config", "--get", f"branch.{branch}.merge", check=False)
        if not remote or remote == "." or not target.startswith("refs/heads/"):
            raise ValueError(f"configure a remote upstream for {repo} branch {branch}")
        if remote != "origin":
            git(repo, "fetch", remote, "--prune")
        upstream = git(repo, "rev-parse", "--verify", "@{upstream}", check=False)
        if not upstream or not is_ancestor(repo, upstream, "HEAD"):
            raise ValueError(f"integrate the upstream changes before publishing: {repo} ({branch})")
    return dirty


def publish(node, message):
    repo, children = node
    for child in children:
        publish(child, message)
    branch = git(repo, "symbolic-ref", "--quiet", "--short", "HEAD", check=False)
    dirty = bool(git(repo, "status", "--porcelain", "--untracked-files=normal",
                     "--ignore-submodules=none"))
    if not branch and not dirty and git(repo, "branch", "-r", "--contains", "HEAD", "--list", "origin/*"):
        print(f"Already published (detached): {repo}", flush=True)
        return
    remote = git(repo, "config", "--get", f"branch.{branch}.remote") if branch else "origin"
    target = git(repo, "config", "--get", f"branch.{branch}.merge") if branch else detached_target(repo)
    print(f"Publishing {repo} ({branch or 'detached'} -> {remote}/{target})", flush=True)
    commit_changes(repo, message)
    git(repo, "push", remote, f"HEAD:{target}")


def main():
    message = (os.environ.get("message") or os.environ.get("MESSAGE", "")).strip()
    if not message:
        raise ValueError("provide a commit message: make push-lambda message='...'")
    root = Path(git(Path.cwd(), "rev-parse", "--show-toplevel")).resolve()
    tree = collect(root)
    prepare(tree)
    publish(tree, message)


if __name__ == "__main__":
    try:
        main()
    except (ValueError, subprocess.CalledProcessError) as exc:
        print(f"push-lambda: {exc}", file=sys.stderr)
        sys.exit(1)
