#!/usr/bin/env python3
"""Run Lizard duplication checks with documented file and block exclusions."""

import argparse
import json
import re
import shutil
import subprocess
import sys
from pathlib import Path


MODULES = ("lib", "lambda", "radiant")
LOCATION_RE = re.compile(r"^(.+):(\d+) ~ (\d+)$")


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


def print_report(blocks, excluded_counts, modules, file_exclusion_count):
    print("Filtered Lizard duplicate report")
    print("Modules: %s" % ", ".join(modules))
    print()
    if blocks:
        for block in blocks:
            print("Duplicate block:")
            print("--------------------------")
            for location in block:
                print(location["text"])
            print("^^^^^^^^^^^^^^^^^^^^^^^^^^")
    else:
        print("No duplicate blocks remain after exclusions.")

    excluded_total = sum(excluded_counts.values())
    print()
    print("Summary")
    print("-------")
    print("Remaining duplicate blocks: %d" % len(blocks))
    print("Excluded known false-positive blocks: %d" % excluded_total)
    print("File exclusion rules passed to Lizard: %d" % file_exclusion_count)
    for rule_id in sorted(excluded_counts):
        print("  %s: %d" % (rule_id, excluded_counts[rule_id]))


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

    try:
        config = load_config(config_path)
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

    print_report(remaining, excluded_counts, selected_modules, len(file_exclusions))
    return 0


if __name__ == "__main__":
    sys.exit(main())
