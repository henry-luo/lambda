#!/usr/bin/env python3
"""Generate a Node official-test inventory report for LambdaJS."""

from __future__ import annotations

import argparse
import datetime as dt
import os
import re
from collections import Counter, defaultdict
from pathlib import Path


ROOT = Path(__file__).resolve().parents[2]
NODE_TEST_DIR = ROOT / "ref/node/test/parallel"
HARNESS_FILE = ROOT / "test/test_node_gtest.cpp"
BASELINE_FILE = ROOT / "test/node/official_baseline.txt"
SKIP_LIST_FILE = ROOT / "test/node/official_skip_list.txt"
SLOW_LIST_FILE = ROOT / "test/node/official_slow_list.txt"
TIMING_FILE = ROOT / "temp/node_official_times.tsv"
CRASHER_FILE = ROOT / "temp/_node_official_crashers.txt"
FAILURE_OUTPUT_FILE = ROOT / "temp/node_official_failures.log"
DEFAULT_OUTPUT = ROOT / "temp/node_official_report.md"


FEATURE_RE = re.compile(
    r'\{\s*"(?P<name>[^"]+)"\s*,\s*"(?P<prefix>[^"]+)"\s*,\s*'
    r'(?P<enabled>true|false)\s*,'
)
FAILURE_BLOCK_RE = re.compile(
    r"^===== (?P<name>test-[^ ]+\.js) exit=(?P<exit>-?\d+) "
    r"timed_out=(?P<timed>yes|no) elapsed=(?P<elapsed>[0-9.]+)ms =====$"
)


def read_name_list(path: Path) -> dict[str, str]:
    names: dict[str, str] = {}
    if not path.exists():
        return names
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if not stripped or stripped.startswith("#"):
            continue
        if "#" in stripped:
            name, reason = stripped.split("#", 1)
            names[name.strip()] = reason.strip()
        else:
            names[stripped] = ""
    return names


def read_baseline(path: Path) -> tuple[set[str], dict[str, str]]:
    names: set[str] = set()
    header: dict[str, str] = {}
    if not path.exists():
        return names, header
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        if line.startswith("#"):
            body = line[1:].strip()
            if ":" in body:
                key, value = body.split(":", 1)
                header[key.strip()] = value.strip()
            continue
        stripped = line.strip()
        if stripped:
            names.add(stripped)
    return names, header


def read_features(path: Path) -> list[tuple[str, str, bool]]:
    features: list[tuple[str, str, bool]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = FEATURE_RE.search(line)
        if match:
            features.append((
                match.group("name"),
                match.group("prefix"),
                match.group("enabled") == "true",
            ))
    return features


def enabled_prefixes(features: list[tuple[str, str, bool]],
                     modules: set[str] | None) -> set[str]:
    prefixes: set[str] = set()
    for name, prefix, enabled in features:
        if modules is not None:
            if name in modules:
                prefixes.add(prefix)
        elif enabled:
            prefixes.add(prefix)
    return prefixes


def extract_prefix(filename: str, prefixes: set[str]) -> str | None:
    if not filename.startswith("test-") or not filename.endswith(".js"):
        return None
    rest = filename[5:]
    for prefix in sorted(prefixes, key=len, reverse=True):
        if rest == f"{prefix}.js":
            return prefix
        if rest.startswith(prefix) and len(rest) > len(prefix):
            next_char = rest[len(prefix)]
            if next_char in "-.":
                return prefix
    return None


def read_node_tests(prefixes: set[str]) -> dict[str, str]:
    tests: dict[str, str] = {}
    if not NODE_TEST_DIR.exists():
        return tests
    for path in sorted(NODE_TEST_DIR.glob("test-*.js")):
        prefix = extract_prefix(path.name, prefixes)
        if prefix is not None:
            tests[path.name] = prefix
    return tests


def read_timing_rows(path: Path) -> list[dict[str, str]]:
    if not path.exists():
        return []
    rows: list[dict[str, str]] = []
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    if not lines:
        return rows
    headers = lines[0].split("\t")
    for line in lines[1:]:
        if not line.strip():
            continue
        cols = line.split("\t")
        row = {headers[i]: cols[i] if i < len(cols) else "" for i in range(len(headers))}
        rows.append(row)
    return rows


def read_crashers(path: Path) -> list[tuple[str, str]]:
    if not path.exists():
        return []
    crashers: list[tuple[str, str]] = []
    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        stripped = line.strip()
        if not stripped:
            continue
        cols = stripped.split("\t", 1)
        if len(cols) == 2:
            crashers.append((cols[0], cols[1]))
    return crashers


def first_failure_line(lines: list[str]) -> str:
    ignore_prefixes = (
        "raw_status=",
        "module=",
        "cwd=",
        "timeout_cmd=",
        "node_flags=",
        "child_PATH=",
        "command=",
        "--- output ---",
    )
    for line in lines:
        stripped = line.strip()
        if not stripped:
            continue
        if stripped.startswith(ignore_prefixes):
            continue
        return stripped
    return "(no output)"


def classify_failure(first_line: str, exit_code: int, timed_out: bool) -> str:
    lowered = first_line.lower()
    if timed_out:
        return "timeout"
    if exit_code > 128:
        return "crash"
    if "is not a function" in lowered or "not a function" in lowered:
        return "missing function"
    if "missing expected exception" in lowered:
        return "missing exception"
    if "cannot access" in lowered or "referenceerror" in lowered:
        return "tdz/reference error"
    if "assertionerror" in lowered:
        if "message" in lowered or "match" in lowered:
            return "assert message mismatch"
        return "assertion mismatch"
    if "expected values to be strictly" in lowered:
        return "strict equality mismatch"
    if "typeerror" in lowered:
        return "type error"
    if "unsupported" in lowered or "not implemented" in lowered:
        return "unsupported"
    if "uncaught" in lowered:
        return "uncaught"
    return "other"


def read_failure_classes(path: Path) -> tuple[Counter[str], list[tuple[str, str, str]]]:
    if not path.exists():
        return Counter(), []
    counts: Counter[str] = Counter()
    samples: list[tuple[str, str, str]] = []
    current_name = ""
    current_exit = 0
    current_timed = False
    current_lines: list[str] = []

    def flush() -> None:
        nonlocal current_name, current_lines
        if not current_name:
            return
        first = first_failure_line(current_lines)
        klass = classify_failure(first, current_exit, current_timed)
        counts[klass] += 1
        if len(samples) < 20:
            samples.append((current_name, klass, first))

    for line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        match = FAILURE_BLOCK_RE.match(line)
        if match:
            flush()
            current_name = match.group("name")
            current_exit = int(match.group("exit"))
            current_timed = match.group("timed") == "yes"
            current_lines = []
        else:
            current_lines.append(line)
    flush()
    return counts, samples


def format_table(rows: list[list[object]]) -> str:
    if not rows:
        return "_none_\n"
    header = rows[0]
    body = rows[1:]
    out = ["| " + " | ".join(str(x) for x in header) + " |"]
    out.append("| " + " | ".join("---" for _ in header) + " |")
    for row in body:
        out.append("| " + " | ".join(str(x) for x in row) + " |")
    return "\n".join(out) + "\n"


def build_report(args: argparse.Namespace) -> str:
    modules = set(args.modules.split(",")) if args.modules else None
    features = read_features(HARNESS_FILE)
    prefixes = enabled_prefixes(features, modules)
    node_tests = read_node_tests(prefixes)
    baseline, baseline_header = read_baseline(BASELINE_FILE)
    skip_list = read_name_list(SKIP_LIST_FILE)
    slow_list = read_name_list(SLOW_LIST_FILE)
    timing_rows = read_timing_rows(TIMING_FILE)
    crashers = read_crashers(CRASHER_FILE)
    class_counts, class_samples = read_failure_classes(FAILURE_OUTPUT_FILE)

    existing_names = set(node_tests.keys())
    skipped = {name for name in existing_names if name in skip_list}
    slow_excluded = {
        name for name in existing_names
        if name not in skipped and name in slow_list and not args.include_slow
    }
    active = existing_names - skipped - slow_excluded
    active_pass = active & baseline
    active_fail = active - baseline
    baseline_missing = sorted(name for name in baseline if name not in existing_names)

    prefix_rows = []
    per_prefix: dict[str, dict[str, int]] = defaultdict(lambda: defaultdict(int))
    for name, prefix in node_tests.items():
        row = per_prefix[prefix]
        row["total"] += 1
        if name in skipped:
            row["skipped"] += 1
        elif name in slow_list and not args.include_slow:
            row["slow"] += 1
        elif name in baseline:
            row["pass"] += 1
        else:
            row["fail"] += 1
    for prefix, counts in per_prefix.items():
        prefix_rows.append([
            prefix,
            counts["pass"],
            counts["fail"],
            counts["skipped"],
            counts["slow"],
            counts["total"],
        ])
    prefix_rows.sort(key=lambda row: (-int(row[2]), str(row[0])))

    timing_counts: Counter[str] = Counter()
    timing_slow_failures: list[tuple[float, str, str, str]] = []
    for row in timing_rows:
        status = row.get("status", "")
        timing_counts[status] += 1
        try:
            elapsed_ms = float(row.get("elapsed_ms", "0") or "0")
        except ValueError:
            elapsed_ms = 0.0
        if status != "pass":
            timing_slow_failures.append((
                elapsed_ms,
                row.get("test", ""),
                row.get("module", ""),
                status,
            ))
    timing_slow_failures.sort(reverse=True)

    now = dt.datetime.now(dt.timezone.utc).strftime("%Y-%m-%d %H:%M:%S UTC")
    report: list[str] = []
    report.append("# Node Official Inventory Report\n")
    report.append(f"Generated: {now}\n")
    report.append(f"Node tests: `{NODE_TEST_DIR.relative_to(ROOT)}`\n")
    report.append(f"Harness: `{HARNESS_FILE.relative_to(ROOT)}`\n")
    report.append(f"Baseline: `{BASELINE_FILE.relative_to(ROOT)}`\n")
    if modules:
        report.append(f"Module filter: `{','.join(sorted(modules))}`\n")
    report.append("\n")

    report.append("## Summary\n\n")
    report.append(format_table([
        ["Metric", "Count"],
        ["enabled test files", len(existing_names)],
        ["active tests", len(active)],
        ["baseline passes in active set", len(active_pass)],
        ["baseline-inferred failures", len(active_fail)],
        ["skip-list tests in enabled tree", len(skipped)],
        ["slow-list tests excluded", len(slow_excluded)],
        ["baseline names missing from ref/node", len(baseline_missing)],
        ["latest timing rows", len(timing_rows)],
        ["latest crash/timeout manifest rows", len(crashers)],
    ]))
    report.append("\n")

    if baseline_header:
        report.append("## Baseline Header\n\n")
        header_rows = [["Field", "Value"]]
        for key in ("Commit", "ref/node commit", "Total passing", "Total tests",
                    "Baseline comparison", "Runtime"):
            if key in baseline_header:
                header_rows.append([key, baseline_header[key]])
        report.append(format_table(header_rows))
        report.append("\n")

    report.append("## Top Failing Prefixes\n\n")
    report.append(format_table(
        [["Prefix", "Pass", "Fail", "Skipped", "Slow", "Total"]] + prefix_rows[:25]
    ))
    report.append("\n")

    report.append("## Baseline Names Missing From ref/node\n\n")
    if baseline_missing:
        for name in baseline_missing[:100]:
            report.append(f"- `{name}`\n")
        if len(baseline_missing) > 100:
            report.append(f"- ... {len(baseline_missing) - 100} more\n")
    else:
        report.append("_none_\n")
    report.append("\n")

    report.append("## Latest Timing Sample\n\n")
    if timing_rows:
        timing_rows_table = [["Status", "Count"]]
        for status, count in sorted(timing_counts.items()):
            timing_rows_table.append([status, count])
        report.append(format_table(timing_rows_table))
        report.append("\n")
        report.append("### Slowest Non-Passing Timing Rows\n\n")
        slow_rows = [["Seconds", "Status", "Prefix", "Test"]]
        for elapsed_ms, name, module, status in timing_slow_failures[:20]:
            slow_rows.append([f"{elapsed_ms / 1000.0:.2f}", status, module, f"`{name}`"])
        report.append(format_table(slow_rows))
    else:
        report.append("_No timing file found. Run `./test/test_node_gtest.exe ...` first for observed timing data._\n")
    report.append("\n")

    report.append("## Crash And Timeout Manifest\n\n")
    if crashers:
        rows = [["Kind", "Test"]]
        for kind, name in crashers[:100]:
            rows.append([kind, f"`{name}`"])
        report.append(format_table(rows))
    else:
        report.append("_none_\n")
    report.append("\n")

    report.append("## Failure Classification\n\n")
    if class_counts:
        rows = [["Class", "Count"]]
        for klass, count in class_counts.most_common():
            rows.append([klass, count])
        report.append(format_table(rows))
        report.append("\n")
        report.append("### Classification Samples\n\n")
        rows = [["Test", "Class", "First useful line"]]
        for name, klass, first in class_samples:
            escaped = first.replace("|", "\\|")
            rows.append([f"`{name}`", klass, escaped[:180]])
        report.append(format_table(rows))
    else:
        report.append("_No failure log found. Run `./test/test_node_gtest.exe ...` first for observed failure buckets._\n")
    report.append("\n")

    return "".join(report)


def parse_args() -> argparse.Namespace:
    parser = argparse.ArgumentParser(
        description="Generate a LambdaJS Node official inventory report."
    )
    parser.add_argument(
        "--output",
        default=str(DEFAULT_OUTPUT.relative_to(ROOT)),
        help="report path, relative to repo root by default",
    )
    parser.add_argument(
        "--modules",
        help="comma-separated module names using test_node_gtest.cpp module names",
    )
    parser.add_argument(
        "--include-slow",
        action="store_true",
        help="include slow-list tests in the active/failing inventory",
    )
    return parser.parse_args()


def main() -> int:
    args = parse_args()
    output = Path(args.output)
    if not output.is_absolute():
        output = ROOT / output
    output.parent.mkdir(parents=True, exist_ok=True)
    report = build_report(args)
    output.write_text(report, encoding="utf-8")
    print(f"[node-official-report] wrote {os.path.relpath(output, ROOT)}")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
