#!/usr/bin/env python3
"""Replay the Word port's model goldens and native Radiant workflows."""
import argparse
import difflib
import json
import os
import re
from pathlib import Path
import subprocess
import sys

ROOT = Path(__file__).resolve().parents[3]
DEMO = ROOT / "test/demo/word"
ARTIFACTS = ROOT / "temp/word/tests"


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lambda-exe", default=str(ROOT / "lambda.exe"))
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--model-only", action="store_true")
    modes.add_argument("--ui-only", action="store_true")
    parser.add_argument("--report")
    args = parser.parse_args()
    executable = str(Path(args.lambda_exe).resolve())
    ARTIFACTS.mkdir(parents=True, exist_ok=True)
    results = []

    def run(name, command, env=None, timeout=120):
        try:
            result = subprocess.run(command, cwd=ROOT, env=env, capture_output=True,
                                    text=True, timeout=timeout)
            (ARTIFACTS / f"{name}.stdout").write_text(result.stdout)
            (ARTIFACTS / f"{name}.stderr").write_text(result.stderr)
            return result
        except subprocess.TimeoutExpired:
            return None

    def record(name, passed, detail=""):
        results.append({"name": name, "passed": passed, "detail": detail})
        print(f"{'PASS' if passed else 'FAIL'} {name}" + (f": {detail}" if detail else ""), flush=True)

    for tier in (() if args.ui_only else ("interp", "auto", "jit")):
        for script in sorted((DEMO / "tests").glob("*_test.ls")):
            name = f"{script.stem}-{tier}"
            env = {**os.environ, "LAMBDA_EXEC_BACKEND": tier}
            result = run(name, [executable, str(script.relative_to(ROOT))], env)
            expected = script.with_suffix(".txt").read_text().rstrip()
            passed = result is not None and result.returncode == 0 and result.stdout.rstrip() == expected
            detail = ""
            if result is None:
                detail = "timeout"
            elif not passed:
                detail = "".join(difflib.unified_diff(expected.splitlines(True), result.stdout.rstrip().splitlines(True)))
                if not detail:
                    detail = f"exit {result.returncode}; see {ARTIFACTS.relative_to(ROOT)}/{name}.stderr"
            record(name, passed, detail)

    if not args.model_only:
        # These are this harness's generated files, never the default user document.
        for filename in ("smoke.word.json", "workflow.word.json", "workflow.html", "workflow.pdf",
                         "layout.word.json", "layout.html", "layout.pdf", "comments.word.json", "comments.html"):
            (ROOT / "temp/word" / filename).unlink(missing_ok=True)
        for fixture in ("smoke", "editing", "files", "table_caret", "scroll_hit", "tables",
                        "pictures_links", "paragraph_search", "layout", "review"):
            result_path = ARTIFACTS / f"{fixture}.json"
            result_path.unlink(missing_ok=True)
            result = run(fixture, [executable, "view", "test/demo/word/word.ls", "--headless",
                                  "--event-file", f"test/demo/word/tests/{fixture}.json",
                                  "--event-result", str(result_path)],
                         {**os.environ, "LAMBDA_EXEC_BACKEND": "auto"})
            report = json.loads(result_path.read_text()) if result_path.exists() else {}
            counts = report.get("assertions", {})
            passed = (result is not None and result.returncode == 0 and report.get("result") == "PASS"
                      and counts.get("passed", 0) > 0 and counts.get("failed", 1) == 0)
            record(fixture, passed, f"{counts.get('passed', 0)} assertions")
        pdf = ROOT / "temp/word/workflow.pdf"
        html = ROOT / "temp/word/workflow.html"
        snapshot = ROOT / "temp/word/workflow.word.json"
        record("exported-files", pdf.exists() and pdf.read_bytes().startswith(b"%PDF-") and
               html.exists() and "A document worth keeping." in html.read_text() and
               snapshot.exists() and json.loads(snapshot.read_text()).get("format") == "lambda-word")
        pages = ROOT / "temp/word/layout.pdf"
        pdf_data = pages.read_bytes() if pages.exists() else b""
        boxes = re.findall(rb"/MediaBox\s*\[([^]]+)\]", pdf_data)
        record("page-setup-pdf", len(boxes) == 2 and
               all([float(n) for n in box.split()] == [0, 0, 792, 612] for box in boxes) and
               re.search(rb"/Count\s+2\b", pdf_data) is not None,
               "two landscape Letter pages with an explicit page break")
        comments = ROOT / "temp/word/comments.html"
        record("comment-html", comments.exists() and
               all(value in comments.read_text() for value in
                   ("lambda-word-metadata", "data-word-comment=", "Please make this clearer.")))

    passed = sum(entry["passed"] for entry in results)
    report = {"summary": {"passed": passed, "failed": len(results)-passed, "total": len(results)}, "results": results}
    report_path = Path(args.report) if args.report else ARTIFACTS / "results.json"
    if not report_path.is_absolute():
        report_path = ROOT / report_path
    report_path.parent.mkdir(parents=True, exist_ok=True)
    report_path.write_text(json.dumps(report, indent=2) + "\n")
    return 0 if passed == len(results) else 1


if __name__ == "__main__":
    sys.exit(main())
