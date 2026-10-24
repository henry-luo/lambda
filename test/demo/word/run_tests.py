#!/usr/bin/env python3
"""Replay the Word port's model goldens and native Radiant workflows."""
import argparse
import difflib
import json
import os
import re
import shutil
from pathlib import Path
import subprocess
import sys
import xml.etree.ElementTree as ET

ROOT = Path(__file__).resolve().parents[3]
DEMO = ROOT / "test/demo/word"
ARTIFACTS = ROOT / "temp/word/tests"
UI_FIXTURES = ("typing", "smoke", "editing", "bindings", "files", "table_caret", "scroll_hit", "tables", "table_selection", "table_drag", "table_design", "table_layout",
                        "pictures_links", "paragraph_search", "navigation", "editing_ribbon", "home_ribbon", "ribbon_appearance", "citations", "citation_types", "selection_toolbar", "native_fonts", "layout", "columns", "margins", "review", "insert_pages", "bookmarks_symbols", "shapes",
                        "stories", "advanced_objects", "equations", "equation_caret", "illustrations", "changes", "object_transform", "object_move", "object_layout", "shape_format", "clipboard_objects", "viewport", "ruler", "ruler_edit", "view_modes", "page_break_typing", "context_menu", "context_items", "lists", "empty_list_reentry", "print")


def pdf_words(xml):
    try:
        return [[{"text": n.text or "", "x": float(n.attrib["xMin"]), "y": float(n.attrib["yMin"])}
                 for n in page.iter() if n.tag.endswith("}word")]
                for page in ET.fromstring(xml).iter() if page.tag.endswith("}page")]
    except (ET.ParseError, KeyError, ValueError):
        return []


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument("--lambda-exe", default=str(ROOT / "lambda.exe"))
    modes = parser.add_mutually_exclusive_group()
    modes.add_argument("--model-only", action="store_true")
    modes.add_argument("--ui-only", action="store_true")
    parser.add_argument("--report")
    parser.add_argument("--ui-start-at", choices=UI_FIXTURES,
                        help="Resume UI workflows after interruption, retaining previously generated export files")
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
            result = run(name, [executable, "--no-log", str(script.relative_to(ROOT))], env)
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
        for filename in (() if args.ui_start_at else ("smoke.word.json", "workflow.word.json", "workflow.html", "workflow.pdf",
                         "layout.word.json", "layout.html", "layout.pdf", "comments.word.json", "comments.html",
                         "blank-pages.pdf", "cover-pages.pdf", "insert-pages.word.json", "insert-pages.html",
                         "bookmarks.word.json", "bookmarks.html", "shapes.word.json", "shapes.html", "shapes.pdf",
                         "stories.word.json", "stories.html", "stories.pdf", "changes.word.json", "changes.html", "changes.pdf",
                         "advanced-objects.word.json", "advanced-objects.html", "advanced-objects.pdf",
                         "illustrations.word.json", "illustrations.html", "illustrations.pdf",
                         "table-design.word.json", "table-design.html", "table-design.pdf",
                         "table-layout.word.json", "table-layout.html", "table-layout.pdf",
                         "citations.word.json", "citations.html", "citations.pdf",
                         "citations-ieee.html", "citations-ieee.pdf",
                         "columns.word.json", "columns.html", "columns.pdf", "columns-long.pdf", "columns-long.xhtml",
                         "columns-default.svg", "columns-explicit.svg", "columns-invalid.svg",
                         "margins.word.json", "margins.html", "margins.pdf", "margins-facing.html", "margins-facing.pdf", "margins-columns.pdf",
                         "margins-normal.pdf", "margins-narrow.pdf", "margins-moderate.pdf", "margins-wide.pdf", "margins-mirrored.pdf",
                         "object-layout.word.json", "object-layout.html", "object-layout.pdf",
                         "object-layout-inline.pdf", "object-layout-square.pdf", "object-layout-topBottom.pdf",
                         "object-layout-front.pdf", "object-layout-behind.pdf",
                         "shape-format.word.json", "shape-format.html", "shape-format.pdf",
                         "equations.word.json", "equations.html", "equations.pdf",
                         "transforms.word.json", "transforms.html", "transforms.pdf", "lists.word.json", "lists.html", "print.pdf", "print-nup.pdf")):
            (ROOT / "temp/word" / filename).unlink(missing_ok=True)
        fixtures = UI_FIXTURES
        if args.ui_start_at:
            fixtures = fixtures[fixtures.index(args.ui_start_at):]
        for fixture in fixtures:
            result_path = ARTIFACTS / f"{fixture}.json"
            result_path.unlink(missing_ok=True)
            result = run(fixture, [executable, "--no-log", "view", "test/demo/word/word.ls", "--headless",
                                  "--event-file", f"test/demo/word/tests/{fixture}.json",
                                  "--event-result", str(result_path)],
                         {**os.environ, "LAMBDA_EXEC_BACKEND": "auto"})
            report = json.loads(result_path.read_text()) if result_path.exists() else {}
            counts = report.get("assertions", {})
            passed = (result is not None and result.returncode == 0 and report.get("result") == "PASS"
                      and counts.get("passed", 0) > 0 and counts.get("failed", 1) == 0)
            record(fixture, passed, f"{counts.get('passed', 0)} assertions")
        for feature in ("columns", "margins", "object_layout", "shape_format", "equations"):
            exported = run(feature + "-export", [executable, "--no-log", "run", f"test/demo/word/tests/proc/_{feature}_export.ls"],
                           {**os.environ, "LAMBDA_WORD_EXPORT_UI": "1"})
            record(feature + "-export-api", exported is not None and exported.returncode == 0 and
                   exported.stdout.strip() == (DEMO / f"tests/proc/_{feature}_export.txt").read_text().strip())
        long_pdf = ROOT / "temp/word/columns-long.pdf"
        column_data = long_pdf.read_bytes() if long_pdf.exists() else b""
        record("columns-long-pdf", column_data.startswith(b"%PDF-") and
               len(re.findall(rb"/MediaBox\s*\[", column_data)) == 5,
               "100 paragraphs flow across five sheets without a blank leading page")
        if shutil.which("pdftotext") and long_pdf.exists():
            extracted = run("columns-pdf-text", ["pdftotext", "-layout", str(long_pdf), "-"])
            pages = extracted.stdout.split("\f")[:-1] if extracted is not None else []
            record("columns-pdf-text", len(pages) == 5 and
                   sorted(int(n) for n in re.findall(r"Paragraph (\d+):", extracted.stdout)) == list(range(1, 101)) and
                   all("A & B < C" in text and f"Page {i+1} of 5" in text for i, text in enumerate(pages)),
                   "every paragraph once, repeated headers, and resolved page fields")
            bbox = run("columns-pdf-geometry", ["pdftotext", "-bbox", str(long_pdf), "-"])
            positions = []
            if bbox is not None and bbox.returncode == 0:
                words = pdf_words(bbox.stdout)
                positions = sorted({round(n["x"], 1) for n in (words[0] if words else [])
                                    if re.match(r"Paragraph \d+:", n["text"])})
            record("columns-pdf-geometry", len(positions) == 2 and positions[1]-positions[0] > 200,
                   "paragraphs occupy two distinct columns on the first sheet")
        if shutil.which("pdftotext"):
            for stem, expected in (("normal", [72]), ("narrow", [36]), ("moderate", [54]), ("wide", [144]),
                                   ("mirrored", [90]), ("facing", [90, 72, 90]), ("columns", [90, 72, 90])):
                result = run(f"margins-{stem}-bbox", ["pdftotext", "-bbox", str(ROOT / f"temp/word/margins-{stem}.pdf"), "-"])
                pages = pdf_words(result.stdout) if result is not None and result.returncode == 0 else []
                edges = [[n["x"] for n in words if n["text"].startswith(("Margin", "Marker"))] for words in pages]
                record(f"margins-{stem}-geometry", len(edges) == len(expected) and
                       all(len(xs) == 1 and abs(xs[0]-x) < 0.2 for xs, x in zip(edges, expected)),
                       "physical text edges match the configured margins in PDF points")
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
        printed = ROOT / "temp/word/print.pdf"
        print_data = printed.read_bytes() if printed.exists() else b""
        print_boxes = re.findall(rb"/MediaBox\s*\[([^]]+)\]", print_data)
        record("print-pdf", len(print_boxes) == 3 and
               all([round(float(n)) for n in box.split()] == [0, 0, 595, 420] for box in print_boxes),
               "print overrides: three landscape A5 pages, matching the measured preview")
        nup = ROOT / "temp/word/print-nup.pdf"
        nup_data = nup.read_bytes() if nup.exists() else b""
        nup_boxes = re.findall(rb"/MediaBox\s*\[([^]]+)\]", nup_data)
        record("print-nup-pdf", len(nup_boxes) == 1 and
               [round(float(n)) for n in nup_boxes[0].split()] == [0, 0, 420, 595] and b"/Image" in nup_data,
               "pages 2-3 two per sheet: one turned A5 sheet carrying the rendered sheet image")
        comments = ROOT / "temp/word/comments.html"
        record("comment-html", comments.exists() and
               all(value in comments.read_text() for value in
                   ("lambda-word-metadata", "data-word-comment=", "Please make this clearer.")))
        for filename, page_count in (("blank-pages", 3), ("cover-pages", 4)):
            path = ROOT / "temp/word" / f"{filename}.pdf"
            data = path.read_bytes() if path.exists() else b""
            record(f"{filename}-pdf", data.startswith(b"%PDF-") and
                   len(re.findall(rb"/MediaBox\s*\[", data)) == page_count and
                   re.search(rb"/Count\s+" + str(page_count).encode() + rb"\b", data) is not None,
                   f"{page_count} pages including explicit blank/cover pages")
        bookmarks = ROOT / "temp/word/bookmarks.html"
        record("bookmark-html", bookmarks.exists() and all(value in bookmarks.read_text() for value in
               ('id="word-bookmark-Intro"', 'href="#word-bookmark-Intro"', "Hello Introduction±")))
        shapes = ROOT / "temp/word/shapes.html"
        shape_pdf = ROOT / "temp/word/shapes.pdf"
        record("shape-exports", shapes.exists() and all(value in shapes.read_text() for value in
               ('data-word-shape=', 'data:image/svg+xml;base64,', 'width="240"', 'alt="Oval"')) and
               shape_pdf.exists() and shape_pdf.read_bytes().startswith(b"%PDF-"))
        for stem, markers, page_count in (
                ("stories", ('counter(page)', 'counter(pages)', '@page:first', 'Studio handbook'), 2),
                ("advanced-objects", ('data-word-equation=', 'data-word-wordart=', 'data-word-shape=', 'E=mc^2', 'Make something'), 1),
                ("illustrations", ('data-word-icon=', 'data-word-smartart=', 'data-word-chart=', 'Updated hours', '👩‍🎨'), 1),
                ("table-design", ('data-word-table-style="purple"', 'data-word-banded-rows="true"', 'data-word-banded-columns="true"', '#ddccee', '2pt dashed #2563eb'), 1),
                ("table-layout", ('data-word-repeat-header="true"', '<thead>', 'data-word-formula="=SUM(LEFT)"', 'padding-left: 8pt', 'table-layout: auto'), 1),
                ("citations", ('data-word-references', 'data-word-citation=', 'data-word-bibliography=', 'Revised engine notes', '1844'), 1),
                ("citations-ieee", ('data-word-citation=', 'Open Studio', 'Turing', 'doi.org/10.1000/test', '&lt;making&gt;'), 1),
                ("columns", ('data-word-columns', 'column-count:2;', 'column-gap:8.5mm;', 'column-rule:0.5pt solid', 'Every Thursday'), 1),
                ("margins", ('data-word-page', '@page:left', 'margin-left:25.4mm;', 'margin-right:31.75mm;', 'Full width header'), 3),
                ("object-layout", ('data-word-layout=', 'data-word-group=', 'rotate(90deg)', 'Layout works.'), 1),
                ("changes", ('data-word-change-kind="delete"', 'data-word-change-kind="insert"', 'Alex'), 1),
                ("transforms", ('data-word-rotation="-30"', 'rotate(-30deg)', 'Resize me', 'width="225"'), 1)):
            exported = ROOT / "temp/word" / f"{stem}.html"
            pdf_path = exported.with_suffix(".pdf")
            data = pdf_path.read_bytes() if pdf_path.exists() else b""
            record(f"{stem}-exports", exported.exists() and
                   all(marker in exported.read_text() for marker in markers) and data.startswith(b"%PDF-") and
                   len(re.findall(rb"/MediaBox\s*\[", data)) == page_count,
                   f"metadata preserved in HTML; {page_count} PDF page(s)")

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
