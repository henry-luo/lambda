#!/usr/bin/env python3
"""Exercise script writers through the package, built-in, and CLI boundaries."""
import argparse
import difflib
import json
from pathlib import Path
import subprocess
import sys
import tempfile
import time

ROOT = Path(__file__).resolve().parents[1]
FIXTURES = ROOT / "test/input/script_formats"
TARGETS = {"asciidoc": "adoc", "man": "1", "typst": "typ",
           "eml": "eml", "vcf": "vcf", "ics": "ics"}
ALIASES = {"adoc": "asciidoc", "troff": "man", "vcard": "vcf",
           "ical": "ics", "icalendar": "ics"}
CASES = [
    ("asciidoc", "prose.md", "markdown"),
    ("man", "prose.md", "markdown"),
    ("typst", "prose.md", "markdown"),
    ("eml", "plain.eml", "eml"),
    ("eml", "multipart.eml", "eml"),
    ("vcf", "contacts.vcf", "vcf"),
    ("ics", "calendar.ics", "ics"),
]


def run(*args):
    result = subprocess.run([str(ROOT / "lambda.exe"), *map(str, args)],
                            cwd=ROOT, capture_output=True, text=True, timeout=60)
    if result.returncode:
        raise AssertionError(f"lambda {' '.join(map(str, args))} exited {result.returncode}\n"
                             + result.stdout + result.stderr)
    return result


def convert(source, input_type, target, destination):
    run("convert", source, "-f", input_type, "-t", target, "-o", destination)
    if not destination.is_file() or destination.stat().st_size == 0:
        raise AssertionError(f"Conversion produced no output: {destination}")


def read_model(source, source_type, folder, label, ordered=False):
    destination = folder / (label + ".json")
    convert(source, source_type, "json", destination)
    # D2.6.5v3: preserve duplicate map keys and their order in record comparisons.
    hook = (lambda pairs: ("object", pairs)) if ordered else None
    return json.loads(destination.read_text(encoding="utf-8"), object_pairs_hook=hook)


def mark_shape(value):
    """Ignore transparent parser spans; merge adjacent text chunks (S2.6.4)."""
    if isinstance(value, list):
        children = []
        for child in value:
            child = mark_shape(child)
            items = child if isinstance(child, list) else [child]
            for item in items:
                if isinstance(item, str) and children and isinstance(children[-1], str):
                    children[-1] += item
                else:
                    children.append(item)
        return children
    if isinstance(value, dict):
        result = {key: mark_shape(item) for key, item in value.items()}
        if result.get("$") == "span" and set(result) == {"$", "_"}:
            return result["_"]
        return result
    return value


def equal(actual, expected, label):
    if actual != expected:
        before = json.dumps(expected, ensure_ascii=False, indent=2).splitlines(True)
        after = json.dumps(actual, ensure_ascii=False, indent=2).splitlines(True)
        diff = "".join(difflib.unified_diff(before, after, "expected", "actual"))
        raise AssertionError(label + "\n" + diff)


def write_source(value_file, input_type, target, destination, route, folder):
    if route == "cli":
        convert(value_file, input_type, target, destination)
        return
    # These generated programs are process drivers; assertions live in this runner.
    expression = ("writers.format(value, {type:" + json.dumps(target) + "})^"
                  if route == "package" else "format(value," + json.dumps(target) + ")^")
    output_call = ("output(value," + json.dumps(str(destination)) + ")^"
                   if route == "output" else "output(" + expression + ","
                   + json.dumps(str(destination)) + ",\"text\")^")
    script = folder / "_write.ls"
    script.write_text(
        "import writers: lambda.doc.misc\npn main() {\n"
        "    let value = input(" + json.dumps(str(value_file)) + ","
        + json.dumps(input_type) + ")^\n"
        "    " + output_call + "\n}\n", encoding="utf-8")
    run("run", script)
    if not destination.is_file() or destination.stat().st_size == 0:
        raise AssertionError(f"{route} produced no output: {destination}")


def wire_lines(source, target):
    if target not in {"eml", "vcf", "ics"}:
        return
    wire = source.read_bytes()
    if b"\n" in wire.replace(b"\r\n", b"") or b"\r" in wire.replace(b"\r\n", b""):
        raise AssertionError("RFC output contains a non-CRLF line ending")
    limit = 998 if target == "eml" else 75
    for line in wire.split(b"\r\n"):
        # Each physical line must end at a UTF-8 scalar boundary.
        line.decode("utf-8")
        if len(line) > limit:
            raise AssertionError(f"Physical line has {len(line)} octets; limit is {limit}")


def roundtrip(target, fixture, input_type, route, folder):
    original = FIXTURES / fixture
    first = folder / ("first." + TARGETS[target])
    second = folder / ("second." + TARGETS[target])
    prose = target in {"asciidoc", "man", "typst"}
    before = read_model(original, input_type, folder, "before", ordered=not prose)
    write_source(original, input_type, target, first, route, folder)
    wire_lines(first, target)
    after = read_model(first, target, folder, "after", ordered=not prose)
    # Record maps are compared in full, including ordered entries and raw MIME.
    if prose:
        before, after = mark_shape(before), mark_shape(after)
    equal(after, before, "parse/write/parse changed the document")
    write_source(first, target, target, second, route, folder)
    equal(second.read_bytes().hex(), first.read_bytes().hex(),
          "a second parse/write changed the canonical source")


def alias_roundtrip(alias, canonical, route, folder):
    case = next(case for case in CASES if case[0] == canonical)
    original = FIXTURES / case[1]
    canonical_file, alias_file = folder / "canonical.out", folder / "alias.out"
    write_source(original, case[2], canonical, canonical_file, route, folder)
    write_source(original, case[2], alias, alias_file, route, folder)
    equal(alias_file.read_bytes().hex(), canonical_file.read_bytes().hex(),
          "alias output differs from its canonical target")
    equal(read_model(alias_file, canonical, folder, "alias-model", ordered=True),
          read_model(canonical_file, canonical, folder, "canonical-model", ordered=True),
          "alias output reparses differently")


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--report", type=Path)
    parser.add_argument("--format", choices=TARGETS)
    parser.add_argument("--route", choices=["package", "builtin", "output", "cli"])
    options = parser.parse_args()
    (ROOT / "temp").mkdir(exist_ok=True)
    routes = [options.route] if options.route else ["package", "builtin", "output", "cli"]
    results = []
    with tempfile.TemporaryDirectory(prefix="script-formats-", dir=ROOT / "temp") as temporary:
        workspace = Path(temporary)
        jobs = [(f"{target}/{Path(fixture).stem}/{route}", roundtrip,
                 (target, fixture, input_type, route))
                for target, fixture, input_type in CASES
                if not options.format or target == options.format for route in routes]
        jobs += [(f"alias/{alias}/{route}", alias_roundtrip, (alias, target, route))
                 for alias, target in ALIASES.items()
                 if not options.format or target == options.format for route in routes if route != "output"]
        for index, (name, check, arguments) in enumerate(jobs):
            folder = workspace / str(index)
            folder.mkdir()
            started = time.monotonic()
            try:
                check(*arguments, folder)
                result = {"name": name, "pass": True}
            except (AssertionError, ValueError, OSError, subprocess.TimeoutExpired) as error:
                result = {"name": name, "pass": False, "details": str(error)}
            result["duration_ms"] = round((time.monotonic() - started) * 1000)
            results.append(result)
            print(("PASS " if result["pass"] else "FAIL ") + name, flush=True)
            if not result["pass"]:
                print(result["details"], flush=True)
    passed = sum(result["pass"] for result in results)
    report = {"summary": {"passed": passed, "failed": len(results) - passed,
                          "total": len(results)}, "results": results}
    if options.report:
        options.report.parent.mkdir(parents=True, exist_ok=True)
        options.report.write_text(json.dumps(report, indent=2) + "\n", encoding="utf-8")
    print(f"Script format round trips: {passed}/{len(results)} passed")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
