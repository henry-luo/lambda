#!/usr/bin/env python3
"""Run Lizard duplication checks with documented file and block exclusions."""

import argparse
import json
import re
import shutil
import subprocess
import sys
from collections import defaultdict
from itertools import combinations
from pathlib import Path


MODULES = ("lib", "lambda", "radiant")
LOCATION_RE = re.compile(r"^(.+):(\d+) ~ (\d+)$")
RATCHET_FIELDS = ("family_count", "union_duplicate_lines")


class ConfigError(Exception):
    pass


def parse_args():
    parser = argparse.ArgumentParser(
        description="Report Lizard duplicate blocks after applying documented exclusions."
    )
    parser.add_argument(
        "modules",
        nargs="*",
        metavar="MODULE",
        help="modules to scan (default: lib lambda radiant)",
    )
    parser.add_argument(
        "--full",
        action="store_true",
        help="print every remaining Lizard duplicate location after the summary",
    )
    args = parser.parse_args()
    unknown = sorted(set(args.modules) - set(MODULES))
    if unknown:
        parser.error(
            "unknown module(s): %s (choose from %s)"
            % (", ".join(unknown), ", ".join(MODULES))
        )
    return args


def load_config(path):
    try:
        with path.open(encoding="utf-8") as handle:
            config = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigError("cannot read %s: %s" % (path, error)) from error

    if config.get("version") != 1:
        raise ConfigError("%s must have version 1" % path)
    for section in ("file_exclusions", "block_exclusions"):
        if not isinstance(config.get(section), list):
            raise ConfigError("%s must contain a %s list" % (path, section))
    return config


def load_baselines(path):
    try:
        with path.open(encoding="utf-8") as handle:
            config = json.load(handle)
    except (OSError, json.JSONDecodeError) as error:
        raise ConfigError("cannot read %s: %s" % (path, error)) from error

    if config.get("version") != 1 or not isinstance(config.get("baselines"), dict):
        raise ConfigError("%s must have version 1 and a baselines object" % path)
    valid_names = {
        "+".join(selection)
        for selection_size in range(1, len(MODULES) + 1)
        for selection in combinations(MODULES, selection_size)
    }
    for name, baseline in config["baselines"].items():
        if not isinstance(name, str) or not isinstance(baseline, dict):
            raise ConfigError("%s contains an invalid baseline entry" % path)
        if name not in valid_names:
            raise ConfigError("%s contains an invalid module selection: %s" % (path, name))
        for field in RATCHET_FIELDS:
            if type(baseline.get(field)) is not int or baseline[field] < 0:
                raise ConfigError("baseline %s must contain a non-negative %s" % (name, field))
    return config["baselines"]


def validate_rule(rule, section, selected_modules):
    reason = rule.get("reason")
    modules = rule.get("modules")
    if not isinstance(reason, str) or not reason.strip():
        raise ConfigError("every %s entry must have a non-empty reason" % section)
    if not isinstance(modules, list) or not modules:
        raise ConfigError("every %s entry must have a non-empty modules list" % section)
    unknown = set(modules) - set(MODULES)
    if unknown:
        raise ConfigError("unknown module(s) in %s: %s" % (section, ", ".join(sorted(unknown))))
    return bool(set(modules) & set(selected_modules))


def active_file_exclusions(config, selected_modules):
    patterns = []
    for rule in config["file_exclusions"]:
        active = validate_rule(rule, "file_exclusions", selected_modules)
        pattern = rule.get("pattern")
        if not isinstance(pattern, str) or not pattern:
            raise ConfigError("every file_exclusions entry must have a pattern")
        if active:
            patterns.append(pattern)
    return patterns


def find_unique_marker(lines, marker, path, label):
    matches = [index for index, line in enumerate(lines, 1) if marker in line]
    if len(matches) != 1:
        raise ConfigError(
            "%s marker %r in %s must match exactly once; found %d"
            % (label, marker, path, len(matches))
        )
    return matches[0]


def resolve_region(root, region, rule_id, region_index):
    relative_path = region.get("file")
    start_marker = region.get("start")
    end_marker = region.get("end")
    if not all(isinstance(value, str) and value for value in
               (relative_path, start_marker, end_marker)):
        raise ConfigError("block rule %s has an incomplete region" % rule_id)

    relative = Path(relative_path)
    if relative.is_absolute() or ".." in relative.parts:
        raise ConfigError("block rule %s has an invalid file path" % rule_id)
    source_path = root / relative
    try:
        lines = source_path.read_text(encoding="utf-8").splitlines()
    except OSError as error:
        raise ConfigError("cannot read region source %s: %s" % (relative_path, error)) from error

    start_line = find_unique_marker(lines, start_marker, relative_path, "start")
    end_line = next(
        (index for index in range(start_line, len(lines) + 1)
         if end_marker in lines[index - 1]),
        None,
    )
    if end_line is None:
        raise ConfigError(
            "end marker %r was not found after the start marker in %s"
            % (end_marker, relative_path)
        )
    return {
        "key": "%s:%d" % (rule_id, region_index),
        "file": relative.as_posix(),
        "start": start_line,
        "end": end_line,
    }


def active_block_exclusions(config, selected_modules, root):
    rules = []
    seen_ids = set()
    for rule in config["block_exclusions"]:
        active = validate_rule(rule, "block_exclusions", selected_modules)
        rule_id = rule.get("id")
        if not isinstance(rule_id, str) or not rule_id:
            raise ConfigError("every block_exclusions entry must have an id")
        if rule_id in seen_ids:
            raise ConfigError("duplicate block exclusion id: %s" % rule_id)
        seen_ids.add(rule_id)
        regions = rule.get("regions")
        allow_within_region = rule.get("allow_within_region", False)
        if not isinstance(allow_within_region, bool):
            raise ConfigError("block rule %s allow_within_region must be boolean" % rule_id)
        minimum_regions = 1 if allow_within_region else 2
        if not isinstance(regions, list) or len(regions) < minimum_regions:
            raise ConfigError(
                "block rule %s must contain at least %d region(s)"
                % (rule_id, minimum_regions)
            )
        if active:
            rules.append({
                "id": rule_id,
                "reason": rule["reason"],
                "allow_within_region": allow_within_region,
                "regions": [
                    resolve_region(root, region, rule_id, index)
                    for index, region in enumerate(regions)
                ],
            })
    return rules


def parse_duplicate_blocks(output):
    lines = output.splitlines()
    blocks = []
    index = 0
    while index < len(lines):
        if lines[index].strip() != "Duplicate block:":
            index += 1
            continue

        index += 1
        locations = []
        while index < len(lines):
            line = lines[index].strip()
            if line.startswith("^"):
                break
            match = LOCATION_RE.match(line)
            if match:
                locations.append({
                    "text": line,
                    "file": match.group(1).replace("\\", "/").removeprefix("./"),
                    "start": int(match.group(2)),
                    "end": int(match.group(3)),
                })
            index += 1
        if locations:
            blocks.append(locations)
        index += 1
    return blocks


def block_file_set(block):
    return tuple(sorted({location["file"] for location in block}))


def merge_intervals(intervals):
    merged = []
    for start, end in sorted(intervals):
        if not merged or start > merged[-1][1] + 1:
            merged.append([start, end])
        else:
            merged[-1][1] = max(merged[-1][1], end)
    return [(start, end) for start, end in merged]


def cluster_duplicate_families(blocks):
    """Merge overlapping Lizard windows without crossing file-set boundaries."""
    parents = list(range(len(blocks)))

    def find(index):
        while parents[index] != index:
            parents[index] = parents[parents[index]]
            index = parents[index]
        return index

    def union(first, second):
        first_root = find(first)
        second_root = find(second)
        if first_root != second_root:
            parents[second_root] = first_root

    blocks_by_file_set = defaultdict(list)
    for block_index, block in enumerate(blocks):
        blocks_by_file_set[block_file_set(block)].append(block_index)

    for file_set, block_indexes in blocks_by_file_set.items():
        for file_name in file_set:
            intervals = []
            for block_index in block_indexes:
                intervals.extend(
                    (location["start"], location["end"], block_index)
                    for location in blocks[block_index]
                    if location["file"] == file_name
                )
            active = []
            for start, end, block_index in sorted(intervals):
                active = [entry for entry in active if entry[0] >= start]
                # An overlap in any participating file connects two windows from
                # the same file set into one review family.
                for other_index in {entry[1] for entry in active}:
                    union(block_index, other_index)
                active.append((end, block_index))

    family_blocks = defaultdict(list)
    for block_index, block in enumerate(blocks):
        family_blocks[find(block_index)].append(block)

    families = []
    for grouped_blocks in family_blocks.values():
        intervals_by_file = defaultdict(list)
        for block in grouped_blocks:
            for location in block:
                intervals_by_file[location["file"]].append(
                    (location["start"], location["end"])
                )
        merged_by_file = {
            file_name: merge_intervals(intervals)
            for file_name, intervals in intervals_by_file.items()
        }
        families.append({
            "files": tuple(sorted(merged_by_file)),
            "intervals": merged_by_file,
            "blocks": grouped_blocks,
        })

    return sorted(
        families,
        key=lambda family: (
            family["files"],
            min(
                (start, end)
                for intervals in family["intervals"].values()
                for start, end in intervals
            ),
        ),
    )


def duplicate_metrics(raw_block_count, blocks, families):
    intervals_by_file = defaultdict(list)
    family_counts_by_file = defaultdict(int)
    pair_counts = defaultdict(int)

    for block in blocks:
        for location in block:
            intervals_by_file[location["file"]].append(
                (location["start"], location["end"])
            )
    for family in families:
        for file_name in family["files"]:
            family_counts_by_file[file_name] += 1
        for pair in combinations(family["files"], 2):
            pair_counts[pair] += 1

    union_lines_by_file = {
        file_name: sum(end - start + 1 for start, end in merge_intervals(intervals))
        for file_name, intervals in intervals_by_file.items()
    }
    same_file_count = sum(len(family["files"]) == 1 for family in families)
    return {
        "raw_block_count": raw_block_count,
        "remaining_block_count": len(blocks),
        "family_count": len(families),
        "same_file_family_count": same_file_count,
        "cross_file_family_count": len(families) - same_file_count,
        "union_duplicate_lines": sum(union_lines_by_file.values()),
        "union_lines_by_file": union_lines_by_file,
        "family_counts_by_file": dict(family_counts_by_file),
        "pair_counts": dict(pair_counts),
    }


def exclusion_for_block(block, rules):
    for rule in rules:
        matched_regions = set()
        for location in block:
            region = next(
                (candidate for candidate in rule["regions"]
                 if candidate["file"] == location["file"]
                 and candidate["start"] <= location["start"]
                 and candidate["end"] >= location["end"]),
                None,
            )
            if region is None:
                break
            matched_regions.add(region["key"])
        else:
            # Executable copies must span distinct regions; explicitly declarative
            # tables may opt into a narrow single-region exclusion.
            if rule["allow_within_region"] or len(matched_regions) >= 2:
                return rule
    return None


def ratchet_failures(metrics, baseline):
    return [
        "%s grew from %d to %d" % (field, baseline[field], metrics[field])
        for field in RATCHET_FIELDS
        if metrics[field] > baseline[field]
    ]


def print_full_locations(blocks):
    print()
    print("Full duplicate locations")
    print("------------------------")
    if not blocks:
        print("No duplicate blocks remain after exclusions.")
        return
    for block in blocks:
        print("Duplicate block:")
        print("--------------------------")
        for location in block:
            print(location["text"])
        print("^^^^^^^^^^^^^^^^^^^^^^^^^^")


def print_report(metrics, blocks, excluded_counts, modules, file_exclusion_count,
                 baseline, full):
    print("Filtered Lizard duplicate report")
    print("Modules: %s" % ", ".join(modules))
    print()
    print("Summary")
    print("-------")
    print("Raw Lizard duplicate blocks: %d" % metrics["raw_block_count"])
    print("Remaining duplicate blocks: %d" % metrics["remaining_block_count"])
    print("First-party clone families: %d" % metrics["family_count"])
    print("Same-file clone families: %d" % metrics["same_file_family_count"])
    print("Cross-file clone families: %d" % metrics["cross_file_family_count"])
    print("Union duplicate lines: %d" % metrics["union_duplicate_lines"])
    print("Excluded known false-positive blocks: %d" % sum(excluded_counts.values()))
    print("File exclusion rules passed to Lizard: %d" % file_exclusion_count)

    print()
    print("Top files by union duplicate lines")
    print("----------------------------------")
    top_files = sorted(
        metrics["union_lines_by_file"],
        key=lambda file_name: (
            -metrics["union_lines_by_file"][file_name],
            file_name,
        ),
    )[:10]
    if not top_files:
        print("  none")
    for file_name in top_files:
        print(
            "  %6d lines | %4d families | %s"
            % (
                metrics["union_lines_by_file"][file_name],
                metrics["family_counts_by_file"][file_name],
                file_name,
            )
        )

    print()
    print("Top cross-file pairs by clone families")
    print("---------------------------------------")
    top_pairs = sorted(
        metrics["pair_counts"],
        key=lambda pair: (-metrics["pair_counts"][pair], pair),
    )[:10]
    if not top_pairs:
        print("  none")
    for first, second in top_pairs:
        print(
            "  %4d families | %s <> %s"
            % (metrics["pair_counts"][(first, second)], first, second)
        )

    print()
    print("Reviewed block exclusions")
    print("--------------------------")
    if not excluded_counts:
        print("  none")
    for rule_id in sorted(excluded_counts):
        print("  %6d blocks | %s" % (excluded_counts[rule_id], rule_id))

    print()
    if baseline is None:
        print("Ratchet: not configured for this module selection")
    else:
        failures = ratchet_failures(metrics, baseline)
        if failures:
            print("Ratchet: FAIL")
            for failure in failures:
                print("  %s" % failure)
        else:
            print(
                "Ratchet: PASS (families %d <= %d; union lines %d <= %d)"
                % (
                    metrics["family_count"], baseline["family_count"],
                    metrics["union_duplicate_lines"], baseline["union_duplicate_lines"],
                )
            )

    if full:
        print_full_locations(blocks)


def build_lizard_command(lizard, file_exclusions, selected_modules):
    # Without an explicit language, Lizard scans vendored scripts and corrupts the C/C++ baseline.
    command = [lizard, "-Eduplicate", "-l", "cpp"]
    for pattern in file_exclusions:
        command.extend(("-x", pattern))
    command.extend(selected_modules)
    return command


def main():
    args = parse_args()
    selected_modules = tuple(module for module in MODULES if not args.modules or module in args.modules)
    root = Path(__file__).resolve().parents[2]
    config_path = Path(__file__).with_name("exclude.json")
    baseline_path = Path(__file__).with_name("baseline.json")

    try:
        config = load_config(config_path)
        baselines = load_baselines(baseline_path)
        file_exclusions = active_file_exclusions(config, selected_modules)
        block_exclusions = active_block_exclusions(config, selected_modules, root)
    except ConfigError as error:
        print("dedup configuration error: %s" % error, file=sys.stderr)
        return 2

    lizard = shutil.which("lizard")
    if not lizard:
        print("lizard is not installed or not available on PATH", file=sys.stderr)
        return 2

    command = build_lizard_command(lizard, file_exclusions, selected_modules)
    result = subprocess.run(
        command,
        cwd=root,
        stdout=subprocess.PIPE,
        stderr=subprocess.PIPE,
        text=True,
    )
    if result.returncode not in (0, 1):
        print("lizard failed with exit code %d" % result.returncode, file=sys.stderr)
        if result.stderr:
            print(result.stderr.rstrip(), file=sys.stderr)
        return result.returncode

    blocks = parse_duplicate_blocks(result.stdout)
    remaining = []
    excluded_counts = {}
    for block in blocks:
        rule = exclusion_for_block(block, block_exclusions)
        if rule is None:
            remaining.append(block)
        else:
            excluded_counts[rule["id"]] = excluded_counts.get(rule["id"], 0) + 1

    families = cluster_duplicate_families(remaining)
    metrics = duplicate_metrics(len(blocks), remaining, families)
    baseline = baselines.get("+".join(selected_modules))
    print_report(
        metrics,
        remaining,
        excluded_counts,
        selected_modules,
        len(file_exclusions),
        baseline,
        args.full,
    )
    return 1 if baseline is not None and ratchet_failures(metrics, baseline) else 0


if __name__ == "__main__":
    sys.exit(main())
