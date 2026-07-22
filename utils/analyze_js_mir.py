#!/usr/bin/env python3
"""Summarize LambdaJS textual MIR and common Stack API frame metrics."""

from __future__ import annotations

import collections
import pathlib
import re
import sys


FUNC_RE = re.compile(r"^([^\s:]+):\s+func(?:\s+(.*))?$")
SUMMARY_RE = re.compile(r"^#\s+(\d+)\s+args?,\s+(\d+)\s+locals?,\s+(\d+)\s+globals?")
FRAME_RE = re.compile(
    r"mir-function: function=([^ ]+) entry=(\d+) bound=(\d+) "
    r"instructions=(\d+) roots=(\d+) root_stores=(\d+) "
    r"scalar_homes=(\d+) number_scratch=(\d+) safepoints=(\d+)"
)


def call_target(line: str) -> str | None:
    if not line.startswith("call\t"):
        return None
    operands = line.split("\t", 1)[1].split(",")
    return operands[1].strip() if len(operands) > 1 else None


def parse_frames(path: pathlib.Path) -> dict[tuple[str, int], dict[str, int]]:
    frames: dict[tuple[str, int], dict[str, int]] = {}
    if not path.exists():
        return frames
    for line in path.read_text(errors="replace").splitlines():
        match = FRAME_RE.search(line)
        if not match:
            continue
        name, entry, bound, insns, roots, stores, homes, scratch, safepoints = (
            match.groups()
        )
        frames[(name, int(entry))] = {
            "entry": int(entry),
            "bound": int(bound),
            "frame_insns": int(insns),
            "roots": int(roots),
            "root_stores": int(stores),
            "scalar_homes": int(homes),
            "number_scratch": int(scratch),
            "safepoints": int(safepoints),
        }
    return frames


def frame_for_function(function_name: str,
                       frames: dict[tuple[str, int], dict[str, int]]) -> dict | None:
    if function_name.endswith("_body"):
        return frames.get((function_name[:-5], 1))
    for entry in (2, 3, 0):
        frame = frames.get((function_name, entry))
        if frame:
            return frame
    return None


def parse_dump(path: pathlib.Path) -> list[dict]:
    functions: list[dict] = []
    current: dict | None = None
    for raw in path.read_text(errors="replace").splitlines():
        match = FUNC_RE.match(raw)
        if match:
            current = {
                "name": match.group(1),
                "args": 0,
                "locals": 0,
                "instructions": [],
                "opcodes": collections.Counter(),
                "callees": collections.Counter(),
            }
            functions.append(current)
            continue
        if current is None:
            continue
        stripped = raw.strip()
        if stripped == "endfunc":
            current = None
            continue
        summary = SUMMARY_RE.match(stripped)
        if summary:
            current["args"] = int(summary.group(1))
            current["locals"] = int(summary.group(2))
            continue
        if not raw.startswith("\t") or raw.startswith("\tlocal\t"):
            continue
        opcode = stripped.split(None, 1)[0]
        current["instructions"].append(stripped)
        current["opcodes"][opcode] += 1
        target = call_target(stripped)
        if target:
            current["callees"][target] += 1
    return functions


def enrich(function: dict, frame: dict[str, int] | None) -> None:
    callees = function["callees"]
    function.update(frame or {})
    function.setdefault("entry", -1)
    function.setdefault("bound", 0)
    function.setdefault("roots", 0)
    function.setdefault("root_stores", 0)
    function.setdefault("scalar_homes", 0)
    function.setdefault("number_scratch", 0)
    function.setdefault("safepoints", 0)
    function["insns"] = len(function["instructions"])
    function["calls"] = function["opcodes"]["call"]
    function["exception_polls"] = callees["js_check_exception"]
    function["eval_ops"] = sum(
        count for name, count in callees.items() if name.startswith("js_eval_local_")
    )
    function["arg_ops"] = sum(
        count for name, count in callees.items() if name.startswith("js_args_")
    )
    function["tdz_checks"] = callees["js_check_tdz"]
    function["conversions"] = sum(
        count for name, count in callees.items()
        if "box" in name or "unbox" in name or name in (
            "lambda_mir_double_bits", "js_profiled_push_d",
            "lambda_item_adopt_scalar_home",
        )
    )


def aggregate(functions: list[dict]) -> dict:
    keys = (
        "insns", "locals", "calls", "exception_polls", "eval_ops", "arg_ops",
        "tdz_checks", "conversions", "roots", "root_stores", "scalar_homes",
        "number_scratch", "safepoints",
    )
    result = {key: sum(int(f.get(key, 0)) for f in functions) for key in keys}
    result["functions"] = len(functions)
    return result


def print_group(name: str, functions: list[dict]) -> None:
    total = aggregate(functions)
    print("\t".join(str(value) for value in (
        name, total["functions"], total["insns"], total["locals"],
        total["calls"], total["exception_polls"], total["eval_ops"],
        total["arg_ops"], total["tdz_checks"], total["conversions"],
        total["roots"], total["root_stores"], total["scalar_homes"],
        total["safepoints"],
    )))


def main() -> int:
    check = "--check" in sys.argv[1:]
    paths = [pathlib.Path(arg) for arg in sys.argv[1:] if arg != "--check"]
    if not paths:
        paths = sorted(pathlib.Path("temp/js_mir_profile").glob("*.mir"))
    functions: list[dict] = []
    for path in paths:
        frames = parse_frames(path.with_suffix(".log"))
        for function in parse_dump(path):
            enrich(function, frame_for_function(function["name"], frames))
            function["file"] = path.stem
            functions.append(function)

    print("group\tfunctions\tinsns\tlocals\tcalls\texception_polls\teval_ops"
          "\targ_ops\ttdz_checks\tconversions\troots\troot_stores"
          "\tscalar_homes\tsafepoints")
    print_group("all", functions)
    for entry, name in ((0, "public"), (1, "boxed_body"),
                        (2, "native_body"), (3, "resume")):
        print_group(name, [f for f in functions if f["entry"] == entry])

    print("\nfile\tfunction\tentry\tbound\tinsns\tlocals\tcalls\texception_polls"
          "\teval_ops\targ_ops\ttdz_checks\tconversions\troots\troot_stores"
          "\tscalar_homes\tsafepoints")
    for function in functions:
        print("\t".join(str(function[key]) for key in (
            "file", "name", "entry", "bound", "insns", "locals", "calls",
            "exception_polls", "eval_ops", "arg_ops", "tdz_checks",
            "conversions", "roots", "root_stores", "scalar_homes", "safepoints",
        )))
    if check:
        errors: list[str] = []
        boxed = [f for f in functions if f["entry"] == 1]
        if any(f["eval_ops"] for f in boxed):
            errors.append("default no-eval corpus emitted eval-local operations")
        for function in functions:
            calls = function["callees"]
            saves = calls["js_args_save"]
            restores = calls["js_args_restore"]
            pushes = calls["js_args_push"]
            # Exceptional edges restore the oldest active mark in addition to
            # each call's normal-path restore, so restores may exceed saves.
            if saves > pushes or restores < saves:
                errors.append(f"{function['name']}: unowned argument watermark")
            instructions = function["instructions"]
            for index in range(len(instructions) - 1):
                if call_target(instructions[index]) == "js_check_exception" and \
                        call_target(instructions[index + 1]) == "js_check_exception":
                    errors.append(f"{function['name']}: adjacent exception polls")
        binding = next((f for f in functions if "tuneBindings" in f["name"] and
                        f["entry"] == 1), None)
        if not binding or binding["tdz_checks"] != 0:
            errors.append("dominated tuneBindings assignment retained a TDZ check")
        if not any(f["callees"]["js_new_object_with_shape"] for f in functions):
            errors.append("static object-shape path was not emitted")
        if not any(f["callees"]["js_array_define_dense_element_direct"]
                   for f in functions):
            errors.append("fresh-array dense-store path was not emitted")
        if not any(f["callees"]["js_get_iterator_lazy"] and
                   f["callees"]["js_iterator_step"] for f in functions):
            errors.append("guarded built-in iterator path was not emitted")
        if not any(f["callees"]["js_finalize_function"] for f in functions):
            errors.append("fused function finalization path was not emitted")
        if errors:
            for error in errors:
                print(f"mir-check: {error}", file=sys.stderr)
            return 1
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
