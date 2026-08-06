#!/usr/bin/env python3
"""Summarize finalized Lambda/C2MIR MIR artifacts for Tune12.

The input is the text emitted by ``LAMBDA_MIR_DUMP_PATH``.  Each ``--input``
has the form ``label=path``; multiple artifacts can therefore be compared in
one deterministic report without depending on a particular compiler build.
The report is an audit signal, not a performance metric.
"""

import argparse
import json
import re
import sys
from pathlib import Path


FUNCTION_RE = re.compile(r"^(?P<name>[^:\s]+):\s+func\b")
END_FUNC_RE = re.compile(r"^endfunc\b")
CALL_RE = re.compile(r"\bcall\s+(?:[^,]+,\s*)?(?P<target>[^,\s]+)")
LABEL_RE = re.compile(r"^(?P<label>L[0-9A-Fa-f]+):")


def new_function(name):
    return {
        "name": name,
        "lines": 0,
        "instructions": 0,
        "calls": 0,
        "call_targets": {},
        "categories": {
            "index": 0,
            "checked_set": 0,
            "type_check": 0,
            "numeric_admission": 0,
            "boxing_unboxing": 0,
            "side_root": 0,
            "scalar_home": 0,
        },
        "memory_loads": 0,
        "memory_stores": 0,
        "labels": 0,
        "entry_calls": 0,
        "labeled_calls": 0,
    }


def call_category(target):
    target = target.lower()
    if "fn_index" in target or "item_at" in target:
        return "index"
    if "array_set" in target or "set_checked" in target:
        return "checked_set"
    if "type_check" in target or "validate_" in target:
        return "type_check"
    if "numeric_boundary" in target or "boundary_admit" in target:
        return "numeric_admission"
    if ("box" in target or "unbox" in target or "it2" in target or
            "2it" in target or "int2it" in target):
        return "boxing_unboxing"
    if "root" in target:
        return "side_root"
    if "scalar_home" in target or "number_frame" in target:
        return "scalar_home"
    return None


def is_memory_operand(operand):
    return "(" in operand and ")" in operand


def parse_mir(path):
    current = None
    functions = []
    for raw_line in path.read_text(encoding="utf-8", errors="replace").splitlines():
        line = raw_line.strip()
        match = FUNCTION_RE.match(line)
        if match:
            current = new_function(match.group("name"))
            functions.append(current)
            continue
        if current is None:
            continue
        current["lines"] += 1
        if END_FUNC_RE.match(line):
            current = None
            continue
        if not line or line.startswith("#") or line.startswith("local\t"):
            continue
        current["instructions"] += 1
        if LABEL_RE.match(line):
            current["labels"] += 1
        parts = line.split("\t", 1)
        opcode = parts[0].split(None, 1)[0].lower()
        operands = parts[1] if len(parts) == 2 else ""
        operand_list = [part.strip() for part in operands.split(",")]
        if opcode in ("mov", "dmov") and operand_list:
            if is_memory_operand(operand_list[0]):
                current["memory_stores"] += 1
            elif any(is_memory_operand(part) for part in operand_list[1:]):
                current["memory_loads"] += 1
        call = CALL_RE.search(line) if opcode == "call" else None
        if call:
            target = call.group("target")
            current["calls"] += 1
            targets = current["call_targets"]
            targets[target] = targets.get(target, 0) + 1
            category = call_category(target)
            if category:
                current["categories"][category] += 1
            if current["labels"]:
                current["labeled_calls"] += 1
            else:
                current["entry_calls"] += 1
    return functions


def summarize(functions):
    total = new_function("<module>")
    total["name"] = "<module>"
    for function in functions:
        for key in ("lines", "instructions", "calls", "memory_loads",
                    "memory_stores", "labels", "entry_calls", "labeled_calls"):
            total[key] += function[key]
        for target, count in function["call_targets"].items():
            total["call_targets"][target] = total["call_targets"].get(target, 0) + count
        for category, count in function["categories"].items():
            total["categories"][category] += count
    return total


def parse_input(spec):
    if "=" not in spec:
        raise ValueError("--input must be label=path")
    label, path = spec.split("=", 1)
    if not label or not path:
        raise ValueError("--input must be label=path")
    return label, Path(path)


def render_markdown(reports):
    lines = [
        "# Tune12 MIR gap report",
        "",
        "Generated from finalized MIR artifacts; counts are static audit signals.",
        "",
        "| Artifact | Function | Lines | Instructions | Calls | Loads | Stores | Entry calls | Labeled calls |",
        "|---|---|---:|---:|---:|---:|---:|---:|---:|",
    ]
    for label, report in reports.items():
        for function in report["functions"] + [report["summary"]]:
            lines.append(
                f"| {label} | `{function['name']}` | {function['lines']} | "
                f"{function['instructions']} | {function['calls']} | "
                f"{function['memory_loads']} | {function['memory_stores']} | "
                f"{function['entry_calls']} | {function['labeled_calls']} |"
            )
    lines.extend(["", "## Call categories", "", "| Artifact | Function | "
                  "Index | Checked set | Type check | Numeric admission | "
                  "Box/unbox | Side root | Scalar home |", "|---|---|---:|---:|---:|---:|---:|---:|---:|"])
    for label, report in reports.items():
        for function in report["functions"] + [report["summary"]]:
            categories = function["categories"]
            lines.append(
                f"| {label} | `{function['name']}` | {categories['index']} | "
                f"{categories['checked_set']} | {categories['type_check']} | "
                f"{categories['numeric_admission']} | {categories['boxing_unboxing']} | "
                f"{categories['side_root']} | {categories['scalar_home']} |"
            )
    return "\n".join(lines) + "\n"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--input", action="append", required=True,
                        help="artifact label and path, for example candidate=temp/mir.txt")
    parser.add_argument("--format", choices=("markdown", "json"), default="markdown")
    parser.add_argument("--output", help="write the report here; stdout by default")
    args = parser.parse_args()
    reports = {}
    try:
        for spec in args.input:
            label, path = parse_input(spec)
            if not path.is_file():
                raise ValueError(f"MIR artifact does not exist: {path}")
            functions = parse_mir(path)
            reports[label] = {"path": str(path), "functions": functions,
                              "summary": summarize(functions)}
    except (OSError, ValueError) as error:
        print(f"analyze_mir_gap: {error}", file=sys.stderr)
        return 2

    if args.format == "json":
        rendered = json.dumps(reports, indent=2, sort_keys=True) + "\n"
    else:
        rendered = render_markdown(reports)
    if args.output:
        Path(args.output).write_text(rendered, encoding="utf-8")
    else:
        print(rendered, end="")
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
