#!/usr/bin/env python3
"""JS in-band error-lane emission census (D8.4.3).

Compiles a corpus of JS scripts to MIR and measures the error lane as emitted:

  emit    per-script and total counts -- call instructions, inline ERROR tag
          tests, residual legacy exception-helper calls, throw/payload calls.
  attrib  attributes every emitted tag test to the helper whose result it
          tests, i.e. the worklist for exception_effect catalog tightening.

Both subcommands read MIR artifacts produced with JS_MIR_DUMP=1 and
LAMBDA_MIR_DUMP_PATH=<file>.  Note that `--no-log` must NOT be passed to
lambda.exe: mir_dump_instrumentation_enabled() gates the artifact on logging
being live, so the dump would never be written.

A/B usage (isolating one commit's effect on emission):

  utils/js_error_lane_census.py emit --bin ./lambda.exe        test/js/*.js
  utils/js_error_lane_census.py emit --bin /path/old/lambda.exe test/js/*.js

`emit` writes <outdir>/summary.json so two runs can be diffed numerically, and
`attrib` consumes the same <outdir> of .mir artifacts.

LMD_TYPE_ERROR is 27 (lambda/lambda.h); the emitted test is the three-insn
sequence `ursh rT, rV, 56` / `eq rC, rT, 27` / `bf`.
"""

from __future__ import annotations

import argparse
import collections
import concurrent.futures
import json
import os
import pathlib
import re
import subprocess
import sys

LMD_TYPE_ERROR = 27

CALL_RE = re.compile(r"^\s*call\s+(.*)$")
MOV_RE = re.compile(r"^\s*mov\s+(%[A-Za-z0-9_]+),\s*(%[A-Za-z0-9_]+)\s*$")
TAGTEST_RE = re.compile(r"^\s*eq\s+([^,]+),\s*([^,]+),\s*%d\s*$" % LMD_TYPE_ERROR)
URSH56_RE = re.compile(r"^\s*ursh\s+([^,]+),\s*([^,]+),\s*56\s*$")
FUNC_RE = re.compile(r"^([^\s:]+):\s+func\b")

# symbols the in-band model retired; any nonzero count is a Tune1 regression
LEGACY_SYMBOLS = ("js_check_exception", "js_clear_exception",
                  "js_exception_pending", "js_exception_result")


def call_operands(rest: str):
    """`call proto, target, ret, args...` -> (target, ret_reg)."""
    ops = [o.strip() for o in rest.split(",")]
    target = ops[1] if len(ops) > 1 else ""
    ret = ops[2] if len(ops) > 2 else ""
    return target, ret


def compile_one(script: pathlib.Path, binary: pathlib.Path,
                outdir: pathlib.Path, cwd: pathlib.Path, timeout: float):
    """Compile `script` and return its MIR artifact path (or None)."""
    dump = outdir / (script.stem + ".mir")
    if dump.exists():
        dump.unlink()
    env = dict(os.environ)
    env["JS_MIR_DUMP"] = "1"
    env["LAMBDA_MIR_DUMP_PATH"] = str(dump)
    env["LAMBDA_DISABLE_MIR_CACHE"] = "1"
    try:
        # the artifact is written at compile time, so a run that times out
        # during execution still yields a complete dump.
        subprocess.run([str(binary), "js", str(script)], env=env, cwd=str(cwd),
                       timeout=timeout, stdout=subprocess.DEVNULL,
                       stderr=subprocess.DEVNULL)
    except subprocess.TimeoutExpired:
        pass
    except OSError as exc:
        print(f"error: cannot run {binary}: {exc}", file=sys.stderr)
        return None
    return dump if dump.exists() else None


def scan(path: pathlib.Path):
    stats = collections.Counter()
    targets = collections.Counter()
    lines = path.read_text(errors="replace").splitlines()
    for line in lines:
        m = CALL_RE.match(line)
        if m:
            target, _ = call_operands(m.group(1))
            stats["calls"] += 1
            targets[target] += 1
            if any(sym in target for sym in LEGACY_SYMBOLS):
                stats["legacy_exc_calls"] += 1
            if "js_error_lane_payload" in target:
                stats["payload_calls"] += 1
            if "js_throw" in target:
                stats["throw_calls"] += 1
            continue
        if TAGTEST_RE.match(line):
            stats["tag_tests"] += 1
        elif URSH56_RE.match(line):
            stats["ursh56"] += 1
        if FUNC_RE.match(line):
            stats["funcs"] += 1
    stats["insns"] = len(lines)
    return stats, targets


def cmd_emit(args):
    binary = pathlib.Path(args.bin).resolve()
    cwd = pathlib.Path(args.cwd).resolve()
    outdir = pathlib.Path(args.outdir).resolve()
    outdir.mkdir(parents=True, exist_ok=True)
    scripts = [pathlib.Path(s) for s in args.scripts]

    total = collections.Counter()
    all_targets = collections.Counter()
    rows = []
    skipped = []

    def work(sc):
        return sc, compile_one(sc, binary, outdir, cwd, args.timeout)

    with concurrent.futures.ThreadPoolExecutor(max_workers=args.jobs) as pool:
        for sc, dump in pool.map(work, scripts):
            if dump is None:
                skipped.append(sc.name)
                continue
            stats, targets = scan(dump)
            total.update(stats)
            all_targets.update(targets)
            rows.append((sc.name, stats))

    head = (f"{'script':<34}{'calls':>9}{'tagtest':>9}{'legacy':>8}"
            f"{'payload':>9}{'throw':>8}{'tt/call':>9}")
    print(head)
    if not args.quiet:
        for name, s in sorted(rows, key=lambda r: -r[1]["calls"]):
            ratio = s["tag_tests"] / s["calls"] if s["calls"] else 0.0
            print(f"{name:<34}{s['calls']:>9}{s['tag_tests']:>9}"
                  f"{s['legacy_exc_calls']:>8}{s['payload_calls']:>9}"
                  f"{s['throw_calls']:>8}{ratio:>9.2f}")
    print("-" * len(head))
    ratio = total["tag_tests"] / total["calls"] if total["calls"] else 0.0
    label = f"TOTAL ({len(rows)} scripts)"
    print(f"{label:<34}{total['calls']:>9}{total['tag_tests']:>9}"
          f"{total['legacy_exc_calls']:>8}{total['payload_calls']:>9}"
          f"{total['throw_calls']:>8}{ratio:>9.2f}")
    print(f"\nMIR instructions      : {total['insns']:,}")
    print(f"emitted functions     : {total['funcs']:,}")
    print(f"legacy helper calls   : {total['legacy_exc_calls']:,}  (D8.4.3 gate: 0)")
    if skipped:
        print(f"skipped (no artifact) : {len(skipped)} -> {', '.join(skipped[:8])}"
              + (" ..." if len(skipped) > 8 else ""))

    summary = {
        "binary": str(binary),
        "scripts": len(rows),
        "total": dict(total),
        "top_targets": all_targets.most_common(40),
        "skipped": skipped,
    }
    (outdir / "summary.json").write_text(json.dumps(summary, indent=2))
    print(f"\nsummary written to {outdir / 'summary.json'}")
    return 1 if total["legacy_exc_calls"] else 0


def resolve_tested_reg(lines, index, src_reg):
    """The compared register comes from `ursh rT, rV, 56`; return rV."""
    for j in range(index - 1, max(-1, index - 6), -1):
        m = URSH56_RE.match(lines[j])
        if m and m.group(1).strip() == src_reg:
            return m.group(2).strip()
    return None


def attribute(lines, index, reg, window):
    """Walk back to the call that produced `reg`, following mov copies."""
    wanted = reg
    hops = 0
    for j in range(index - 1, max(-1, index - window), -1):
        line = lines[j]
        mc = CALL_RE.match(line)
        if mc:
            target, ret = call_operands(mc.group(1))
            if ret == wanted:
                return target
            continue
        mv = MOV_RE.match(line)
        # a plain register copy renames the carrier; keep chasing the source
        if mv and mv.group(1).strip() == wanted and hops < 8:
            wanted = mv.group(2).strip()
            hops += 1
    return None


def cmd_attrib(args):
    files = sorted(pathlib.Path(args.outdir).glob("*.mir"))
    if not files:
        print(f"error: no .mir artifacts in {args.outdir}; run `emit` first",
              file=sys.stderr)
        return 2
    attrib = collections.Counter()
    total = 0
    for path in files:
        lines = path.read_text(errors="replace").splitlines()
        for i, line in enumerate(lines):
            m = TAGTEST_RE.match(line)
            if not m:
                continue
            total += 1
            reg = resolve_tested_reg(lines, i, m.group(2).strip())
            if reg is None:
                attrib["<no-ursh>"] += 1
                continue
            attrib[attribute(lines, i, reg, args.window) or "<unresolved>"] += 1
    print(f"tag tests attributed: {total:,} over {len(files)} artifacts\n")
    print(f"{'count':>9}{'share':>8}  helper whose result is tested")
    for target, n in attrib.most_common(args.top):
        print(f"{n:>9}{100.0 * n / total:>7.1f}%  {target}")
    return 0


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    sub = ap.add_subparsers(dest="cmd", required=True)

    e = sub.add_parser("emit", help="compile a corpus and count the error lane")
    e.add_argument("scripts", nargs="+")
    e.add_argument("--bin", default="./lambda.exe")
    e.add_argument("--cwd", default=".")
    e.add_argument("--outdir", default="temp/js_error_lane")
    e.add_argument("--jobs", type=int, default=8)
    e.add_argument("--timeout", type=float, default=20.0)
    e.add_argument("--quiet", action="store_true", help="totals only")
    e.set_defaults(func=cmd_emit)

    a = sub.add_parser("attrib", help="attribute tag tests to forcing helpers")
    a.add_argument("--outdir", default="temp/js_error_lane")
    a.add_argument("--top", type=int, default=30)
    a.add_argument("--window", type=int, default=400)
    a.set_defaults(func=cmd_attrib)

    args = ap.parse_args()
    return args.func(args)


if __name__ == "__main__":
    sys.exit(main())
