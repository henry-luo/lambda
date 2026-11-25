#!/usr/bin/env python3
"""Run the standalone shell pipeline cases on each Lambda backend."""

import hashlib
import os
from pathlib import Path
import shutil
import subprocess
import sys
import tempfile
import time


CASES = Path(__file__).resolve().with_name("cases")
ROOT = CASES.parents[3]
EXE = Path(sys.argv[1] if len(sys.argv) > 1 else ROOT / "lambda.exe").resolve()


def check_commands(run, work, env):
    run("reusable_counts")
    run("array_sort")
    listing = work / "listing"
    listing.mkdir()
    for name in ("pear", "apple"):
        (listing / name).touch()
    run("ls_head", cwd=listing)

    repository = work / "repository"
    repository.mkdir()
    subprocess.run(["git", "init", "--quiet", "--template=", "--object-format=sha1"],
                   cwd=repository, env=env, check=True)
    # A fixed root commit makes both Git outputs independent of the Lambda checkout.
    subprocess.run(
        ["git", "commit", "--quiet", "--allow-empty", "--no-gpg-sign", "-m", "Shell pipeline fixture"],
        cwd=repository,
        env={**env, "GIT_AUTHOR_NAME": "Lambda", "GIT_AUTHOR_EMAIL": "lambda@example.invalid",
             "GIT_COMMITTER_NAME": "Lambda", "GIT_COMMITTER_EMAIL": "lambda@example.invalid",
             "GIT_AUTHOR_DATE": "2000-01-01T00:00:00+0000",
             "GIT_COMMITTER_DATE": "2000-01-01T00:00:00+0000"},
        check=True,
    )
    run("git_oneline", cwd=repository)
    run("git_transform", cwd=repository)
    run("argument_boundaries")
    run("direct_arguments")

    run("early_head")
    run("failed_upstream", error=b"process failed")
    run("failed_launch", error=b"cannot launch")
    run("abandoned_child")
    run("one_shot", error=b"consumed once")
    run("deadline", error=b"pipeline timed out")
    run("cancel")


def check_files(run, work):
    payload = bytes(range(256)) * 4096
    run("binary_transfer", stdin=payload)
    (work / "source.dat").write_bytes(payload)
    target = work / "target.dat"
    run("file_copy")
    assert target.read_bytes() == payload
    run("append", stdin=b"\x00end")
    assert target.read_bytes() == payload + b"\x00end"
    run("atomic_write", stdin=b"\x00replacement")
    assert target.read_bytes() == b"\x00replacement"
    run("atomic_failure", error=b"process failed")
    assert target.read_bytes() == b"\x00replacement"
    run("stderr_route", error=b"process failed")
    assert b"lambda-shell-test-missing-file" in (work / "errors.log").read_bytes()


def check_records(run, work):
    run("line_map", stdin=b"apple\r\n\npe\x00ar\nlast")
    run("line_map", golden="line_map_empty")
    run("line_map", stdin=b"\n", golden="line_map_blank")
    for invalid in (b"\xff\n", b"\xc3"):
        run("line_map", stdin=invalid, golden="line_map_invalid", error=b"invalid UTF-8")
    run("captured_callbacks", stdin=b"pear\r\n\nplum\napple")
    run("mapped_head")
    run("invalid_feeder_input", stdin=b"\xff\n", error=b"invalid UTF-8")
    run("feeder_deadline", error=b"pipeline timed out")
    run("parked_callback_deadline", error=b"pipeline timed out")
    run("line_index", stdin=b"two\none\n")
    shutil.copyfile(CASES / "fragmented_stdout.sh", work / "fragmented_stdout.sh")
    run("fragmented_stdout")
    # The payload fills both pipes before the scheduler's idle watchdog.
    run("backpressure", stdin=(b"a" * 32768 + b"\n") * 32)
    run("abandoned_map")


def check_compositions(run, work):
    run("rank_log", stdin=(b"INFO api ready\nERROR db timeout\nERROR api offline\n"
                          b"ERROR db retry\nERROR web offline\nERROR api retry\nERROR db down\n"))
    run("paste_names")
    files = work / "find input"
    files.mkdir()
    for name in ("two words.txt", "with'quote.txt", "ignored.bin"):
        (files / name).touch()
    run("find_xargs")

    (work / "catalog.csv").write_bytes(b"1,pear\n2,green apple\n3,plum\n")
    run("tag_and_count")
    assert (work / "tagged items.txt").read_bytes() == b"tag:PEAR\ntag:GREEN APPLE\ntag:PLUM\n"
    destination = work / "selected items.txt"
    destination.write_bytes(b"stale file contents\n")
    run("symmetric_file")
    assert destination.read_bytes() == b"GREEN APPLE\nPLUM\n"

    run("select_codes", stdin=b"db,503\r\napi,200\r\nweb,404\r\ncache,503\r\nworker,429")
    run("structured_rows")
    run("grep_early")
    run("mapped_grep_early")
    run("grep_no_match", error=b"process failed (exit 1,")
    run("partial_failure", error=b"process failed (exit 7,")


def run_suite(backend, work):
    work.mkdir()
    env = {key: value for key, value in os.environ.items() if not key.startswith("GIT_")}
    env.update(LAMBDA_HOME=str(ROOT / "lmd"), LAMBDA_EXEC_BACKEND=backend,
               LC_ALL="C", TMPDIR=str(work), GIT_CONFIG_GLOBAL=os.devnull, GIT_CONFIG_NOSYSTEM="1")
    timings = []

    def run(name, *, stdin=b"", golden=None, error=None, cwd=work):
        expected = (CASES / f"{golden or name}.txt").read_bytes()
        start = time.perf_counter()
        result = subprocess.run(
            [str(EXE), "run", str(CASES / f"{name}.ls")],
            cwd=cwd, env=env, input=stdin, capture_output=True, timeout=15,
        )
        timings.append(time.perf_counter() - start)
        diagnostic = (backend, name, result.returncode, result.stdout[:200], result.stderr)
        assert (result.returncode != 0) == (error is not None), diagnostic
        if error is not None:
            assert error in result.stderr, diagnostic
        actual = result.stdout
        if expected.startswith(b"sha256:"):
            actual = b"sha256:" + hashlib.sha256(actual).hexdigest().encode() + b"\n"
        assert actual == expected, diagnostic
        assert b"memtrack: LEAK" not in result.stderr, diagnostic

    check_commands(run, work, env)
    check_files(run, work)
    check_records(run, work)
    check_compositions(run, work)
    print(f"{backend}: {len(timings)} pipeline runs passed in {sum(timings):.2f} s")


if __name__ == "__main__":
    (ROOT / "temp").mkdir(exist_ok=True)
    with tempfile.TemporaryDirectory(prefix="shell-piping-", dir=ROOT / "temp") as temp:
        for tier in ("interp", "jit", "auto"):
            run_suite(tier, Path(temp) / tier)
