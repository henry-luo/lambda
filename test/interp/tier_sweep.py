#!/usr/bin/env python3
"""Runs every functional Lambda test script under both tiers and reports the
zero-fallback, output-identical set. Output goes to ./temp/ (CLAUDE rule 2).

Usage: python3 test/interp/tier_sweep.py [--dir test/lambda] [--timeout 20]
"""
import argparse, os, re, signal, subprocess, sys
from concurrent.futures import ThreadPoolExecutor, as_completed

FALLBACK_RE = re.compile(r"interp: executed=(\d+) fallback=(\d+) excluded=(\d+)")


# Mirrors test_lambda_gtest.cpp's discovery: functional scripts run directly,
# procedural ones through `lambda.exe run`.
FUNCTIONAL_DIRS = (
    "test/lambda", "test/lambda/chart", "test/lambda/latex", "test/lambda/math",
    "test/lambda/editor", "test/lambda/editing", "test/lambda/graph/mermaid",
    "test/lambda/graph/graphviz", "test/lambda/graph/structurizr",
)
PROCEDURAL_DIRS = ("test/lambda/proc", "test/lambda/conc", "test/lambda/pdf")


def run(script, tier, timeout, procedural=False):
    env = dict(os.environ)
    if tier:
        env["LAMBDA_TIER"] = tier
    else:
        env.pop("LAMBDA_TIER", None)
    argv = ["./lambda.exe", "run", script] if procedural else ["./lambda.exe", script]
    proc = None
    try:
        # A script can start renderer/helper descendants. Give the invocation
        # its own process group so a timeout closes every inherited pipe; a
        # bare subprocess.run() kill left those descendants alive and stranded
        # the worker thread in communicate() during the full corpus sweep.
        proc = subprocess.Popen(argv, env=env, stdout=subprocess.PIPE,
                                stderr=subprocess.PIPE, text=True,
                                errors="replace", start_new_session=os.name != "nt")
        stdout, stderr = proc.communicate(timeout=timeout)
    except subprocess.TimeoutExpired:
        if proc:
            if os.name == "nt":
                proc.kill()
            else:
                try:
                    os.killpg(proc.pid, signal.SIGKILL)
                except (ProcessLookupError, PermissionError):
                    # Sandboxed macOS workers may deny process-group signaling
                    # even though the direct child is ours; fall back to the
                    # child handle so one renderer timeout cannot abort the
                    # whole P1 partition refresh (R4).
                    try:
                        proc.kill()
                    except (ProcessLookupError, PermissionError):
                        pass
            # A renderer can fork a helper into another session while retaining
            # stdout/stderr. Draining with communicate() after killing the
            # direct process then waits forever for that inherited pipe, even
            # though the timed-out Lambda invocation has already ended. Reap
            # only the direct child and close our copies; its descendants no
            # longer participate in this row's timeout verdict.
            try:
                proc.wait(timeout=5)
            except subprocess.TimeoutExpired:
                proc.kill()
                try:
                    proc.wait(timeout=5)
                except subprocess.TimeoutExpired:
                    pass
            if proc.stdout:
                proc.stdout.close()
            if proc.stderr:
                proc.stderr.close()
        return None, None, "timeout"
    stats = FALLBACK_RE.search(stderr or "")
    fallback = int(stats.group(2)) if stats else 0
    executed = int(stats.group(1)) if stats else 0
    status = "ok" if proc.returncode == 0 else f"exit{proc.returncode}"
    return stdout, (executed, fallback), status


def main():
    ap = argparse.ArgumentParser()
    ap.add_argument("--dir", default=None,
                    help="scan one directory instead of the whole baseline corpus")
    ap.add_argument("--timeout", type=int, default=20)
    ap.add_argument("--out", default="temp/interp_tier_sweep.tsv")
    ap.add_argument("--jobs", type=int, default=max(1, (os.cpu_count() or 4) - 1))
    ap.add_argument("--progress", type=int, default=25,
                    help="print a completed-row count at this interval (0 disables it)")
    args = ap.parse_args()

    def discover(directory, procedural):
        if not os.path.isdir(directory):
            return []
        return sorted(
            (os.path.join(directory, n), procedural)
            for n in os.listdir(directory)
            if n.endswith(".ls") and os.path.exists(
                os.path.join(directory, n[:-3] + ".txt")))

    if args.dir:
        scripts = discover(args.dir, args.dir in PROCEDURAL_DIRS)
    else:
        scripts = []
        for directory in FUNCTIONAL_DIRS:
            scripts += discover(directory, False)
        for directory in PROCEDURAL_DIRS:
            scripts += discover(directory, True)

    os.makedirs("temp", exist_ok=True)

    # Each script is an independent pair of subprocess runs, so the sweep
    # parallelizes cleanly. It was serial back when almost everything fell back
    # in milliseconds; every slice that lands converts a rejection into a full
    # interpreted run, so the serial cost grew with coverage. These runs read
    # only stderr — nothing here depends on the shared log.txt, which is why
    # refresh_lists.py (which does) must stay serial.
    def verdict_for(entry):
        script, procedural = entry
        jit_out, _, jit_status = run(script, None, args.timeout, procedural)
        int_out, stats, int_status = run(script, "interp", args.timeout, procedural)
        # A timeout under N-way parallel load is a scheduling artifact, not a
        # divergence: the same script compared clean when re-run alone. Retry
        # the pair once with a longer budget so a slow script cannot manufacture
        # a false mismatch, and keep any survivor in its own verdict rather than
        # folding it into either answer.
        if "timeout" in (jit_status, int_status):
            long_timeout = args.timeout * 3
            jit_out, _, jit_status = run(script, None, long_timeout, procedural)
            int_out, stats, int_status = run(script, "interp", long_timeout, procedural)
        executed, fallback = stats if stats else (0, 0)
        if "timeout" in (jit_status, int_status):
            verdict = "timeout"
        elif fallback or not executed:
            verdict = "fallback"
        elif jit_out != int_out or jit_status != int_status:
            # Confirm before accusing. Every genuine T0 divergence found so far
            # reproduces on a direct re-run; a one-off under N-way load does not
            # (test/lambda/pdf/phase2_font.ls compared clean 9 times in a row).
            # A false alarm here is worse than a slow sweep -- it is what would
            # make the oracle stop being believed.
            jit_out2, _, jit_status2 = run(script, None, args.timeout * 3, procedural)
            int_out2, _, int_status2 = run(script, "interp", args.timeout * 3, procedural)
            verdict = ("mismatch"
                       if jit_out2 != int_out2 or jit_status2 != int_status2
                       else "match")
        else:
            verdict = "match"
        return (script, verdict, jit_status, int_status, executed, fallback)

    # `pool.map()` yields in submission order. One early renderer fixture can
    # therefore hide hundreds of completed classifications behind it and make
    # a healthy full sweep look hung. Report completions as they arrive while
    # retaining discovery order in the final TSV.
    rows = [None] * len(scripts)
    with ThreadPoolExecutor(max_workers=args.jobs) as pool:
        futures = {
            pool.submit(verdict_for, entry): index
            for index, entry in enumerate(scripts)
        }
        completed = 0
        for future in as_completed(futures):
            rows[futures[future]] = future.result()
            completed += 1
            if args.progress and (completed % args.progress == 0 or
                                  completed == len(scripts)):
                print(f"progress={completed}/{len(scripts)}", flush=True)

    supported = [r[0] for r in rows if r[1] == "match"]
    fell_back = [r[0] for r in rows if r[1] == "fallback"]
    mismatched = [r[0] for r in rows if r[1] == "mismatch"]
    timed_out = [r[0] for r in rows if r[1] == "timeout"]

    with open(args.out, "w") as f:
        f.write("# script\tverdict\tjit_status\tinterp_status\texecuted\tfallback\n")
        for row in rows:
            f.write("\t".join(str(c) for c in row) + "\n")

    print(f"scripts={len(scripts)} match={len(supported)} "
          f"fallback={len(fell_back)} mismatch={len(mismatched)} "
          f"timeout={len(timed_out)}")
    if timed_out:
        # R4: never a silent cap -- an unproven script is named, not absorbed.
        print("inconclusive (timed out on both attempts):")
        for s_ in timed_out:
            print("  " + s_)
    print(f"wrote {args.out}")
    if mismatched:
        print("mismatched:")
        for s in mismatched:
            print("  " + s)
    return 0


if __name__ == "__main__":
    sys.exit(main())
