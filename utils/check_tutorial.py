#!/usr/bin/env python3
"""Replay the tutorial in doc/tutorial/ and check every output it shows.

Each chapter (doc/tutorial/NN_*.md) runs in a fresh sandbox under
temp/tutorial_run/<chapter>/, in document order. Files in doc/tutorial/data/
are copied into every sandbox first. Within a chapter:

  * a ```lambda block whose first line is a `// name.ls` comment is saved in
    the sandbox as name.ls;
  * any fenced block with a `file=NAME` directive (```json file=books.json) is
    saved as NAME;
  * a ```bash block is run line by line; only lines starting with `lambda `
    (mapped to the repo's lambda.exe) or `cat ` are executed, other lines
    (`cd`, comments) are skipped. `lambda view` and `lambda edit` open a
    window and are never run unless they pass `--headless`; a ```bash norun
    block is not run at all;
  * a ```text block right after a ```bash block is that block's expected
    stdout, compared exactly after trimming trailing spaces and blank lines.
    `text partial` instead requires each expected line to appear, in order,
    in stdout+stderr (for diagnostics); `text nocheck` is not compared;
  * a ```text repl block is a REPL transcript: the `λ> ` lines are piped into
    the REPL and the other lines must equal its output.

The doc-example gate (make check-doc-code) separately compiles every lambda
fence; this checker is about results. Exit status is 0 only when every
checked output matches.

    python3 utils/check_tutorial.py                 # every chapter
    python3 utils/check_tutorial.py --filter 03     # chapters whose name contains 03
"""

from __future__ import annotations

import argparse
import os
import re
import shutil
import subprocess
import sys
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
TUTORIAL = ROOT / "doc" / "tutorial"
DATA = TUTORIAL / "data"
RUN = ROOT / "temp" / "tutorial_run"
LAMBDA = ROOT / "lambda.exe"
FENCE = re.compile(r"^(\s*)```(\S*)\s*(.*?)\s*$")
FILE_COMMENT = re.compile(r"^//\s*([\w.\-/]+\.ls)\s*$")
LOG_LINE = re.compile(r"^\d\d:\d\d:\d\d \[")
REPL_BANNER = 3  # "Lambda Script REPL", "Type help ...", "Multi-line input ..."


def blocks(path: Path):
    """Yield (line, lang, directives, body) for every fenced block."""
    lines = path.read_text(encoding="utf-8").splitlines()
    i = 0
    while i < len(lines):
        m = FENCE.match(lines[i])
        if not m:
            i += 1
            continue
        lang, directives, start = m.group(2), m.group(3).split(), i + 1
        body = []
        i += 1
        while i < len(lines) and not lines[i].strip().startswith("```"):
            body.append(lines[i])
            i += 1
        i += 1
        yield start, lang, directives, "\n".join(body)


def env():
    e = dict(os.environ)
    e["LAMBDA_HOME"] = str(ROOT / "lmd")  # packages and schemas, wherever the sandbox is
    return e


def normalize(text: str, sandbox: Path) -> list[str]:
    text = text.replace(str(sandbox) + "/", "").replace(str(sandbox), ".")
    out = [ln.rstrip() for ln in text.splitlines()]
    out = [ln for ln in out if not LOG_LINE.match(ln) and "memtrack" not in ln]
    while out and out[-1] == "":
        out.pop()
    while out and out[0] == "":
        out.pop(0)
    return out


def run_line(line: str, sandbox: Path):
    if line.startswith("lambda "):
        args = line[len("lambda "):].split()
        if args and args[0] in ("view", "edit") and "--headless" not in args:
            return None  # would open a window
        argv = [str(LAMBDA)] + args
    elif line.startswith("cat "):
        argv = ["cat"] + line[4:].split()
    else:
        return None
    try:
        p = subprocess.run(argv, cwd=sandbox, capture_output=True, text=True,
                           errors="replace", timeout=120, env=env())
        return p.stdout, p.stderr
    except subprocess.TimeoutExpired:
        return "", "TIMEOUT"


def check_chapter(chapter: Path) -> list[str]:
    sandbox = RUN / chapter.stem
    shutil.rmtree(sandbox, ignore_errors=True)
    sandbox.mkdir(parents=True)
    if DATA.is_dir():
        for f in DATA.iterdir():
            if f.is_file():
                shutil.copy(f, sandbox / f.name)
    failures, pending, checked = [], None, 0
    for line, lang, directives, body in blocks(chapter):
        where = f"{chapter.relative_to(ROOT)}:{line}"
        target = next((d.split("=", 1)[1] for d in directives if d.startswith("file=")), None)
        first = body.splitlines()[0] if body.strip() else ""
        if lang == "lambda" and FILE_COMMENT.match(first):
            target = FILE_COMMENT.match(first).group(1)
        if target:
            dest = sandbox / target
            dest.parent.mkdir(parents=True, exist_ok=True)
            dest.write_text(body + "\n", encoding="utf-8")
        if lang == "bash" and "norun" in directives:
            pending = None
            continue
        if lang == "bash":
            outs, errs, ran = [], [], False
            for cmd in body.splitlines():
                cmd = cmd.strip()
                res = run_line(cmd, sandbox) if cmd and not cmd.startswith("#") else None
                if res is not None:
                    ran = True
                    outs.append(res[0])
                    errs.append(res[1])
            pending = (where, "".join(outs), "".join(errs)) if ran else None
            continue
        if lang == "text" and "repl" in directives:
            inputs = [ln[3:] for ln in body.splitlines() if ln.startswith("λ> ")]
            expected = [ln.rstrip() for ln in body.splitlines()
                        if not ln.startswith("λ> ") and ln.strip()][REPL_BANNER:] \
                if body.splitlines() and body.splitlines()[0].startswith("Lambda Script REPL") else \
                [ln.rstrip() for ln in body.splitlines() if not ln.startswith("λ> ") and ln.strip()]
            p = subprocess.run([str(LAMBDA)], input="\n".join(inputs) + "\n", cwd=sandbox,
                               capture_output=True, text=True, errors="replace", timeout=120, env=env())
            actual = []
            for ln in normalize(p.stdout, sandbox):
                # piped stdin still echoes prompts on some terminals: drop them
                ln = re.sub(r"^(?:(?:λ>|>|\.\.) ?)+", "", ln) if re.match(r"^(λ>|>|\.\.)( |$)", ln) else ln
                if ln.strip():
                    actual.append(ln)
            actual = actual[REPL_BANNER:]
            checked += 1
            if actual != expected:
                failures.append(f"{where}: REPL transcript differs\n  expected: {expected}\n  actual:   {actual}")
            pending = None
            continue
        if lang == "text" and pending is not None:
            cmd_where, out, err = pending
            pending = None
            if "nocheck" in directives:
                continue
            expected = [ln.rstrip() for ln in body.splitlines()]
            while expected and expected[-1] == "":
                expected.pop()
            checked += 1
            if "partial" in directives:
                actual = normalize(out + "\n" + err, sandbox)
                pos = 0
                for exp in expected:
                    while pos < len(actual) and actual[pos] != exp:
                        pos += 1
                    if pos == len(actual):
                        failures.append(f"{where}: line not found in output of {cmd_where}: {exp!r}\n  actual: {actual}")
                        break
                    pos += 1
            else:
                actual = normalize(out, sandbox)
                if actual != expected:
                    failures.append(f"{where}: output of {cmd_where} differs\n  expected: {expected}\n  actual:   {actual}"
                                    + (f"\n  stderr:   {normalize(err, sandbox)[:6]}" if err.strip() else ""))
            continue
        pending = None
    print(f"{chapter.name}: {checked} output(s) checked, {len(failures)} failed")
    return failures


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--filter", metavar="SUBSTR", help="only chapters whose file name contains SUBSTR")
    args = ap.parse_args()
    if not LAMBDA.exists():
        sys.exit("lambda.exe not found; run `make build` first")
    chapters = sorted(p for p in TUTORIAL.glob("[0-9][0-9]_*.md"))
    if args.filter:
        chapters = [p for p in chapters if args.filter in p.name]
    failures = []
    for ch in chapters:
        failures += check_chapter(ch)
    for f in failures:
        print("FAIL " + f)
    print(("OK" if not failures else "FAIL") + f": {len(failures)} failure(s) across {len(chapters)} chapter(s)")
    sys.exit(1 if failures else 0)


if __name__ == "__main__":
    main()
