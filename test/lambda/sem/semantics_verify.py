#!/usr/bin/env python3
"""Four-bucket verification of the Lambda fixtures against the semantics model
(vibe/Lambda_Semantics_DSL_POC.md §13.1, Stage 5 of §15.2).

For each `foo.ls` + `foo.txt` pair the pipeline is

    foo.ls --emit-ast-dump--> temp/sem/foo.ast.mark --normalize--> term
           --ev--> model value, printed by the script printer --diff--> foo.txt

and, independently, `lambda.exe foo.ls` --diff--> foo.txt. The two verdicts
place the fixture in one of four buckets (verified / suspect golden / runtime
regression / both moved). A fixture whose lowering leaves the modelled core, or
whose model run is stuck, is *pending* with its reason; the manifest beside
this script records the verified set, the pending census and the exclusions.

The gate is "the verified set stays verified": a listed fixture that is no
longer in the verified bucket fails the run. `--update` grows the verified set
from one sweep and refreshes the pending census; it never drops a verified
entry. All scratch output goes under ./temp/sem/ (AGENTS.md rule 2).

Usage:
  python3 test/lambda/sem/semantics_verify.py                 # test/lambda, report + gate
  python3 test/lambda/sem/semantics_verify.py --test closure  # one fixture, with diffs
  python3 test/lambda/sem/semantics_verify.py --update        # promote newly verified fixtures
"""
import argparse
import difflib
import json
import os
import re
import sys
from collections import Counter
from concurrent.futures import ThreadPoolExecutor, as_completed

REPO = os.path.abspath(os.path.join(os.path.dirname(__file__), "..", "..", ".."))
sys.path.insert(0, os.path.join(REPO, "test", "interp"))
from lambda_process import run_lambda_process, run_process  # noqa: E402

SEM_DIR = "test/lambda/sem"
MANIFEST = os.path.join(SEM_DIR, "semantics-manifest.json")
MODULE_IMPORT = "import ~~.~~.test.lambda.sem.mod_sem_verify"
TEMP = "temp/sem"
EXPECT_RE = re.compile(r"^\s*//\s*expect:\s*(.+?)\s*$")
LET_RE = re.compile(r"^\s*(?:pub\s+)?let\s+([A-Za-z_][A-Za-z0-9_]*)\s*[=:]")
PLATFORM = {"darwin": "mac", "linux": "linux", "win32": "win"}.get(sys.platform, "")

BUCKETS = ("verified", "suspect", "regression", "both_moved")
BUCKET_TITLE = {
    "verified": "verified   (runtime == golden, model == golden)",
    "suspect": "SUSPECT    (runtime == golden, model != golden)",
    "regression": "regression (runtime != golden, model == golden)",
    "both_moved": "both moved (runtime != golden, model != golden)",
}


def is_helper(filename):
    """Mirrors test/test_script_discovery.hpp: helpers carry a prefix."""
    return filename.startswith("_") or filename.startswith("mod_") or filename.startswith("schema_")


def golden_for(script):
    """The sibling golden, with the per-OS override test_lambda_gtest honours."""
    stem = script[:-3]
    if PLATFORM and os.path.exists(f"{stem}.{PLATFORM}.txt"):
        return f"{stem}.{PLATFORM}.txt"
    return stem + ".txt" if os.path.exists(stem + ".txt") else None


def discover(directory):
    found = []
    for name in sorted(os.listdir(directory)):
        if not name.endswith(".ls") or is_helper(name):
            continue
        script = os.path.join(directory, name)
        if golden_for(script):
            found.append(script)
    return found


def normalize_output(text):
    """What test_lambda_gtest compares: no timing lines, no trailing whitespace."""
    lines = [ln for ln in text.splitlines() if not ln.startswith("__TIMING__:")]
    return "\n".join(ln.rstrip() for ln in lines).rstrip()


def read_golden(path):
    with open(path, encoding="utf-8", errors="replace") as f:
        return normalize_output(f.read())


def extract_expects(script):
    """`// expect: V` lines, each attached to the nearest preceding `let NAME`.
    The comment may sit on the binding's line or on the lines after it."""
    expects, current = [], None
    with open(script, encoding="utf-8", errors="replace") as f:
        for line in f:
            m = LET_RE.match(line)
            if m:
                current = m.group(1)
            code, _, comment = line.partition("//")
            m = EXPECT_RE.match("//" + comment) if comment else None
            if m and current:
                expects.append((current, m.group(1)))
    return expects


def lambda_string(text):
    return '"' + text.replace("\\", "\\\\").replace('"', '\\"') + '"'


def write(path, text):
    with open(path, "w", encoding="utf-8") as f:
        f.write(text)


class Verdict:
    def __init__(self, script):
        self.script = script
        self.stem = os.path.basename(script)[:-3]
        self.bucket = None          # one of BUCKETS, or None when pending
        self.reason = ""            # pending reason / stuck message
        self.unsupported = []
        self.expects = []
        self.model_out = None
        self.runtime_out = None
        self.golden = None


def verify_one(script, timeout):
    v = Verdict(script)
    stem = v.stem
    dump_path = f"{TEMP}/{stem}.ast.mark"
    v.golden = read_golden(golden_for(script))

    # 1. the production parser's account of the fixture
    dump = run_process(["./lambda.exe", "--no-log", "--emit-ast-dump", script], timeout)
    if dump.timed_out or "<ast-dump" not in dump.stdout:
        v.reason = f"dump {dump.status}"
        return v
    write(dump_path, dump.stdout)

    # 2. lowering census + stuckness + `// expect:` checks, as JSON
    expects = extract_expects(script)
    expect_lit = ", ".join("{n: %s, e: %s}" % (lambda_string(n), lambda_string(e)) for n, e in expects)
    diag_script = f"{TEMP}/{stem}.diag.ls"
    write(diag_script, f"{MODULE_IMPORT}\n\npn main() {{\n"
          f"  print(format(diag({lambda_string(dump_path)}, [{expect_lit}]), 'json'))\n}}\n")
    diag = run_lambda_process("./lambda.exe", diag_script, None, timeout, procedural=True, no_log=True)
    if diag.status != "ok":
        v.reason = f"diag {diag.status}: {diag.stderr.strip().splitlines()[-1] if diag.stderr.strip() else ''}"
        return v
    try:
        report = json.loads(diag.stdout)
    except json.JSONDecodeError:
        v.reason = "diag unreadable"
        return v
    v.unsupported = sorted(set(report.get("unsupported") or []))
    v.expects = report.get("expects") or []
    outcome = report.get("outcome", "")
    if outcome == "unsupported":
        v.reason = "unsupported: " + ", ".join(v.unsupported)
        return v
    if outcome != "value":
        v.reason = outcome   # "stuck: <why>"
        return v

    # 3. the model's value through the script printer, versus the golden
    model_script = f"{TEMP}/{stem}.model.ls"
    write(model_script, f"{MODULE_IMPORT}\n\nmodel_value({lambda_string(dump_path)})\n")
    model = run_lambda_process("./lambda.exe", model_script, None, timeout, no_log=True)
    if model.status != "ok":
        v.reason = f"model {model.status}"
        return v
    v.model_out = normalize_output(model.stdout)

    # 4. the runtime's own answer, on the default tier the baseline uses
    runtime = run_lambda_process("./lambda.exe", script, None, timeout, no_log=True)
    v.runtime_out = normalize_output(runtime.stdout) if runtime.status == "ok" else f"<{runtime.status}>"

    runtime_ok = v.runtime_out == v.golden
    model_ok = v.model_out == v.golden
    v.bucket = {(True, True): "verified", (True, False): "suspect",
                (False, True): "regression", (False, False): "both_moved"}[(runtime_ok, model_ok)]
    return v


def load_manifest(path):
    if not os.path.exists(path):
        return {"suite": "test/lambda", "verified": [], "excluded": {}, "pending": {}}
    with open(path, encoding="utf-8") as f:
        return json.load(f)


def save_manifest(path, manifest):
    manifest["verified"] = sorted(set(manifest["verified"]))
    manifest["excluded"] = dict(sorted(manifest["excluded"].items()))
    manifest["pending"] = dict(sorted(manifest["pending"].items()))
    with open(path, "w", encoding="utf-8") as f:
        json.dump(manifest, f, indent=2, ensure_ascii=False)
        f.write("\n")


def unified(a, b, a_name, b_name):
    return "".join(difflib.unified_diff(
        (a or "").splitlines(True), (b or "").splitlines(True), a_name, b_name))


def print_detail(v):
    print(f"== {v.script}")
    if v.bucket:
        print(f"   bucket: {v.bucket}")
    else:
        print(f"   pending: {v.reason}")
    for e in v.expects:
        print(f"   expect {e['n']}: {e['status']}  (expect: {e['expect']!r}, model: {e['model']})")
    if v.model_out is not None and v.model_out != v.golden:
        print(unified(v.golden + "\n", v.model_out + "\n", "golden", "model"), end="")
    if v.runtime_out is not None and v.runtime_out != v.golden:
        print(unified(v.golden + "\n", v.runtime_out + "\n", "golden", "runtime"), end="")


def main():
    ap = argparse.ArgumentParser(description=__doc__, formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument("--dir", default="test/lambda", help="fixture directory (default test/lambda)")
    ap.add_argument("--test", default=None, help="one fixture stem; prints its detail and diffs")
    ap.add_argument("--update", action="store_true",
                    help="grow the manifest's verified set and refresh its pending census")
    ap.add_argument("--manifest", default=MANIFEST)
    ap.add_argument("--timeout", type=float, default=30.0)
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 1))
    ap.add_argument("--verbose", action="store_true", help="print every fixture's line")
    args = ap.parse_args()

    os.chdir(REPO)
    os.makedirs(TEMP, exist_ok=True)
    manifest = load_manifest(args.manifest)
    excluded = manifest.get("excluded", {})

    scripts = discover(args.dir)
    if args.test:
        scripts = [s for s in scripts if os.path.basename(s)[:-3] == args.test]
        if not scripts:
            sys.exit(f"semantics-verify: no fixture named {args.test!r} in {args.dir}")
    else:
        scripts = [s for s in scripts if os.path.basename(s)[:-3] not in excluded]

    verdicts = {}
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {pool.submit(verify_one, s, args.timeout): s for s in scripts}
        for future in as_completed(futures):
            v = future.result()
            verdicts[v.stem] = v
            if args.verbose or args.test:
                print_detail(v)

    # --- the report ---
    by_bucket = {b: sorted(v.stem for v in verdicts.values() if v.bucket == b) for b in BUCKETS}
    pending = {v.stem: v.reason for v in verdicts.values() if v.bucket is None}
    print(f"semantics-verify: {args.dir} — {len(scripts)} fixtures, {len(excluded)} excluded")
    for b in BUCKETS:
        names = by_bucket[b]
        shown = ", ".join(names[:12]) + (" …" if len(names) > 12 else "")
        print(f"  {BUCKET_TITLE[b]}: {len(names)}" + (f"  {shown}" if names and b != "verified" else ""))
    stuck = {k: r for k, r in pending.items() if r.startswith("stuck")}
    print(f"  pending (outside the modelled core): {len(pending)}  of which stuck: {len(stuck)}")
    census = Counter()
    for r in pending.values():
        if r.startswith("unsupported: "):
            census.update(r[len("unsupported: "):].split(", "))
        else:
            census[r.split(":")[0]] += 1
    if census:
        print("  top pending reasons: " + "; ".join(f"{k} ({n})" for k, n in census.most_common(10)))
    for k, r in sorted(stuck.items())[:12]:
        print(f"    stuck: {k}: {r[len('stuck: '):]}")
    checked = [e for v in verdicts.values() for e in v.expects]
    if checked:
        tally = Counter(e["status"] for e in checked)
        print("  // expect: checks: " + ", ".join(f"{k} {n}" for k, n in sorted(tally.items())))
        for v in verdicts.values():
            for e in v.expects:
                if e["status"] == "mismatch":
                    print(f"    expect mismatch: {v.stem} {e['n']}: expected {e['expect']!r}, model {e['model']}")

    # --- the gate: the verified set stays verified ---
    if args.test:
        return 0
    verified_now = set(by_bucket["verified"])
    listed = [n for n in manifest.get("verified", []) if n not in excluded]
    regressed = [n for n in listed if n not in verified_now]
    new = sorted(verified_now - set(listed))
    print(f"manifest: {len(listed) - len(regressed)}/{len(listed)} listed fixtures still verified"
          + (f"; {len(new)} newly verifiable: {', '.join(new)}" if new else ""))
    for n in regressed:
        v = verdicts.get(n)
        why = (v.bucket or v.reason) if v else "missing from the corpus"
        print(f"  REGRESSED: {n}: {why}")

    if args.update:
        manifest["suite"] = args.dir
        manifest["verified"] = sorted(set(listed) | verified_now)
        manifest["pending"] = {k: (verdicts[k].bucket if verdicts[k].bucket else verdicts[k].reason)
                               for k in sorted(verdicts) if k not in manifest["verified"]}
        save_manifest(args.manifest, manifest)
        print(f"manifest written: {args.manifest} ({len(manifest['verified'])} verified, "
              f"{len(manifest['pending'])} pending, {len(manifest['excluded'])} excluded)")
    elif new:
        print("  (run with --update to record them)")
    return 1 if regressed else 0


if __name__ == "__main__":
    sys.exit(main())
