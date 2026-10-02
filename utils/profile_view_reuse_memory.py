#!/usr/bin/env python3
"""Capture and compare Radiant's six post-layout memory domains.

Each page is run in a fresh release process. The executable must implement
`layout ... --view-memory-profile PATH` and emit schema version 1.
"""

import argparse
import csv
import hashlib
import json
import math
import pathlib
import subprocess
import sys
import time


ROOT = pathlib.Path(__file__).resolve().parents[1]
PAGE_DIR = ROOT / "test/layout/data/page"
PROFILE_ROOT = ROOT / "temp/view_reuse_page_memory"
COMPARISON_CSV = ROOT / "temp/view_reuse_page_memory_comparison.csv"
SUMMARY_JSON = ROOT / "temp/view_reuse_page_memory_summary.json"
EXPECTED_PAGE_COUNT = 46

DOMAINS = (
    "dom.document.pool",
    "dom.node.arena",
    "view_tree.prop_pool",
    "view_tree.canonical_prop_arena",
    "view_tree.scratch_arena",
    "style.canonical.epoch.pool",
)

METRICS = {
    "physical.live_bytes": ("physical_total", "live_bytes"),
    "physical.reserved_bytes": ("physical_total", "reserved_bytes"),
    "dom.document.pool.direct_live_bytes":
        ("domains", "dom.document.pool", "direct_live_bytes"),
    "dom.document.pool.cumulative_bytes":
        ("domains", "dom.document.pool", "cumulative_bytes"),
    "dom.node.arena.active_bytes":
        ("domains", "dom.node.arena", "live_or_active_bytes"),
    "dom.node.arena.committed_bytes":
        ("domains", "dom.node.arena", "committed_bytes"),
    "view_tree.prop_pool.direct_live_bytes":
        ("domains", "view_tree.prop_pool", "direct_live_bytes"),
    "view_tree.prop_pool.cumulative_bytes":
        ("domains", "view_tree.prop_pool", "cumulative_bytes"),
    "view_tree.canonical_prop_arena.active_bytes":
        ("domains", "view_tree.canonical_prop_arena", "live_or_active_bytes"),
    "view_tree.canonical_prop_arena.committed_bytes":
        ("domains", "view_tree.canonical_prop_arena", "committed_bytes"),
    "view_tree.scratch_arena.active_bytes":
        ("domains", "view_tree.scratch_arena", "live_or_active_bytes"),
    "view_tree.scratch_arena.committed_bytes":
        ("domains", "view_tree.scratch_arena", "committed_bytes"),
    "style.canonical.epoch.pool.live_bytes":
        ("domains", "style.canonical.epoch.pool", "live_or_active_bytes"),
    "style.canonical.epoch.pool.reserved_bytes":
        ("domains", "style.canonical.epoch.pool", "reserved_bytes"),
    "composite.dom_storage_active_bytes":
        ("logical_composites", "dom_storage_active_bytes"),
    "composite.view_prop_storage_active_bytes":
        ("logical_composites", "view_prop_storage_active_bytes"),
    "composite.canonical_style_live_bytes":
        ("logical_composites", "canonical_style_live_bytes"),
}


def page_manifest():
    pages = sorted(PAGE_DIR.glob("*.html"))
    if len(pages) != EXPECTED_PAGE_COUNT:
        raise RuntimeError(
            f"page manifest has {len(pages)} files; expected {EXPECTED_PAGE_COUNT}")
    return pages


def sha256_layout(path):
    layout = json.loads(path.read_text())
    # Layout output embeds wall-clock capture time; it is metadata rather than
    # layout state and would otherwise make every cross-process result differ.
    layout.get("test_info", {}).pop("timestamp", None)
    canonical = json.dumps(
        layout, sort_keys=True, separators=(",", ":"), ensure_ascii=False)
    return hashlib.sha256(canonical.encode("utf-8")).hexdigest()


def ensure_baseline_domains(profile):
    domains = profile.setdefault("domains", {})
    zero_domain = {
        "present": False,
        "instance_count": 0,
        "reserved_bytes": 0,
        "live_or_active_bytes": 0,
        "backing_bytes": 0,
        "direct_live_bytes": 0,
        "committed_bytes": 0,
        "recyclable_bytes": 0,
        "waste_bytes": 0,
        "overhead_bytes": 0,
        "high_water_bytes": 0,
        "cumulative_bytes": 0,
    }
    for domain in DOMAINS:
        domains.setdefault(domain, dict(zero_domain))
    profile.setdefault("logical_composites", {})
    profile["logical_composites"].setdefault(
        "dom_storage_active_bytes",
        domains["dom.document.pool"].get("direct_live_bytes", 0) +
        domains["dom.node.arena"].get("live_or_active_bytes", 0))
    profile["logical_composites"].setdefault(
        "view_prop_storage_active_bytes",
        domains["view_tree.prop_pool"].get("direct_live_bytes", 0) +
        domains["view_tree.canonical_prop_arena"].get("live_or_active_bytes", 0) +
        domains["view_tree.scratch_arena"].get("live_or_active_bytes", 0))
    profile["logical_composites"].setdefault(
        "canonical_style_live_bytes",
        domains["style.canonical.epoch.pool"].get("live_or_active_bytes", 0))
    return profile


def validate_profile(profile, page, revision):
    if profile.get("schema_version") != 1:
        raise RuntimeError(f"{revision}/{page.name}: unsupported profile schema")
    if profile.get("sample_point") != "post_layout_pre_output":
        raise RuntimeError(f"{revision}/{page.name}: wrong sample point")
    ensure_baseline_domains(profile)
    if revision == "after":
        comparison = profile.get("comparability", {})
        if not comparison.get("all_six_domains_present"):
            raise RuntimeError(f"{revision}/{page.name}: a memory domain is missing")
        if comparison.get("attribution_errors", 0) != 0:
            raise RuntimeError(f"{revision}/{page.name}: allocator attribution error")


def capture_revision(executable, revision, pages):
    executable = pathlib.Path(executable).resolve()
    if not executable.is_file():
        raise RuntimeError(f"missing executable: {executable}")
    profile_dir = PROFILE_ROOT / revision
    layout_dir = PROFILE_ROOT / f"{revision}_layout"
    profile_dir.mkdir(parents=True, exist_ok=True)
    layout_dir.mkdir(parents=True, exist_ok=True)
    results = {}
    started = time.monotonic()

    for index, page in enumerate(pages, 1):
        profile_path = profile_dir / f"{page.stem}.json"
        layout_path = layout_dir / f"{page.stem}.json"
        command = [
            str(executable), "layout", str(page.relative_to(ROOT)),
            "-vw", "1200", "-vh", "800",
            "--font-dir", "test/layout/data/font",
            "--auto-close", "--no-log",
            "--view-output", str(layout_path.relative_to(ROOT)),
            "--view-memory-profile", str(profile_path.relative_to(ROOT)),
        ]
        if b"ahem" in page.read_bytes().lower():
            command.extend([
                "--css", "test/layout/data/support/fonts/ahem.css"])
        print(f"[{revision} {index:02d}/{len(pages)}] {page.name}", flush=True)
        completed = subprocess.run(
            command, cwd=ROOT, capture_output=True, text=True, timeout=60)
        if completed.returncode != 0:
            log_path = profile_dir / f"{page.stem}.failure.log"
            log_path.write_text(completed.stdout + completed.stderr)
            raise RuntimeError(
                f"{revision}/{page.name}: layout failed; see {log_path}")
        if not profile_path.is_file() or not layout_path.is_file():
            raise RuntimeError(f"{revision}/{page.name}: output missing")
        with profile_path.open() as source:
            profile = json.load(source)
        validate_profile(profile, page, revision)
        results[page.name] = {
            "profile": profile,
            "layout_sha256": sha256_layout(layout_path),
        }

    elapsed = time.monotonic() - started
    manifest_path = PROFILE_ROOT / f"{revision}_manifest.json"
    manifest_path.write_text(json.dumps({
        "revision": revision,
        "executable": str(executable),
        "page_count": len(pages),
        "elapsed_seconds": elapsed,
        "pages": [page.name for page in pages],
        "layout_sha256": {
            name: result["layout_sha256"] for name, result in results.items()},
    }, indent=2, sort_keys=True) + "\n")
    return results


def load_revision(revision, pages):
    profile_dir = PROFILE_ROOT / revision
    manifest_path = PROFILE_ROOT / f"{revision}_manifest.json"
    if not manifest_path.is_file():
        raise RuntimeError(f"missing {revision} manifest: {manifest_path}")
    manifest = json.loads(manifest_path.read_text())
    expected = [page.name for page in pages]
    if manifest.get("pages") != expected:
        raise RuntimeError(f"{revision} manifest differs from current 46-page corpus")
    results = {}
    for page in pages:
        profile_path = profile_dir / f"{page.stem}.json"
        with profile_path.open() as source:
            profile = json.load(source)
        validate_profile(profile, page, revision)
        results[page.name] = {
            "profile": profile,
            "layout_sha256": manifest["layout_sha256"][page.name],
        }
    return results


def nested_value(profile, path):
    value = profile
    for key in path:
        value = value.get(key, 0)
    return int(value or 0)


def percentile95(values):
    if not values:
        return 0
    ordered = sorted(values)
    return ordered[max(0, math.ceil(len(ordered) * 0.95) - 1)]


def median(values):
    ordered = sorted(values)
    middle = len(ordered) // 2
    if len(ordered) % 2:
        return ordered[middle]
    return (ordered[middle - 1] + ordered[middle]) / 2


def percentage(before, delta):
    if before == 0:
        return None if delta else 0.0
    return delta * 100.0 / before


def aggregate(before_results, after_results, pages):
    rows = []
    summary_metrics = {}
    for metric, path in METRICS.items():
        before_values = []
        after_values = []
        improved = unchanged = regressed = 0
        for page in pages:
            name = page.name
            before = nested_value(before_results[name]["profile"], path)
            after = nested_value(after_results[name]["profile"], path)
            delta = after - before
            before_values.append(before)
            after_values.append(after)
            if delta < 0:
                improved += 1
                status = "improved"
            elif delta > 0:
                regressed += 1
                status = "regressed"
            else:
                unchanged += 1
                status = "unchanged"
            rows.append({
                "page": name,
                "metric": metric,
                "before_bytes": before,
                "after_bytes": after,
                "delta_bytes": delta,
                "delta_percent": percentage(before, delta),
                "status": status,
                "layout_equal": (
                    before_results[name]["layout_sha256"] ==
                    after_results[name]["layout_sha256"]),
            })
        before_sum = sum(before_values)
        after_sum = sum(after_values)
        delta_sum = after_sum - before_sum
        summary_metrics[metric] = {
            "before": {
                "sum": before_sum,
                "median": median(before_values),
                "p95": percentile95(before_values),
            },
            "after": {
                "sum": after_sum,
                "median": median(after_values),
                "p95": percentile95(after_values),
            },
            "delta": {
                "sum": delta_sum,
                "percent": percentage(before_sum, delta_sum),
            },
            "pages": {
                "improved": improved,
                "unchanged": unchanged,
                "regressed": regressed,
            },
        }

    with COMPARISON_CSV.open("w", newline="") as output:
        writer = csv.DictWriter(output, fieldnames=rows[0].keys())
        writer.writeheader()
        writer.writerows(rows)

    changed_layouts = [
        page.name for page in pages
        if before_results[page.name]["layout_sha256"] !=
        after_results[page.name]["layout_sha256"]]
    summary = {
        "schema_version": 1,
        "sample_point": "post_layout_pre_output",
        "page_count": len(pages),
        "same_manifest": True,
        "layout_equal_count": len(pages) - len(changed_layouts),
        "layout_changed_count": len(changed_layouts),
        "layout_changed_pages": changed_layouts,
        "metrics": summary_metrics,
    }
    SUMMARY_JSON.write_text(json.dumps(summary, indent=2, sort_keys=True) + "\n")
    return summary


def parse_args():
    parser = argparse.ArgumentParser()
    parser.add_argument("--baseline-exe")
    parser.add_argument("--after-exe", default="./lambda.exe")
    parser.add_argument(
        "--capture", choices=("both", "before", "after", "none"),
        default="both")
    return parser.parse_args()


def main():
    args = parse_args()
    pages = page_manifest()
    if args.capture in ("both", "before"):
        if not args.baseline_exe:
            raise RuntimeError("--baseline-exe is required to capture before")
        before = capture_revision(args.baseline_exe, "before", pages)
    else:
        before = load_revision("before", pages)
    if args.capture in ("both", "after"):
        after = capture_revision(args.after_exe, "after", pages)
    else:
        after = load_revision("after", pages)
    summary = aggregate(before, after, pages)
    physical = summary["metrics"]["physical.live_bytes"]
    print(
        f"physical live: {physical['before']['sum']} -> "
        f"{physical['after']['sum']} ({physical['delta']['sum']:+d} bytes)")
    print(f"wrote {COMPARISON_CSV}")
    print(f"wrote {SUMMARY_JSON}")


if __name__ == "__main__":
    try:
        main()
    except (RuntimeError, OSError, subprocess.SubprocessError, json.JSONDecodeError) as error:
        print(f"view-reuse memory profile failed: {error}", file=sys.stderr)
        sys.exit(1)
