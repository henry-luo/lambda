#!/usr/bin/env python3
"""Extract the Lambda code carried by `doc/**/*.md` into runnable `.ls` files.

The doc set is a test corpus, not decoration (Doc_Convention.md 8.1). Code
reaches a reader two ways, and this extractor handles both:

  * a fenced block, whose info string declares how it is meant to be checked
    -- bare `lambda`, or `lambda error=E211` / `expr` / `type` / `no-run`;
  * a row of a table annotated `<!-- code-fence: lambda type -->`, whose first
    column is a single inline code span.

Each extracted unit is written to `temp/docblocks/` already wrapped for its
declared kind (`expr` -> `(...)`, `type` -> `type x = ...`), so a runner can
compile the file as-is. `temp/docblocks_index.json` maps every unit back to
its `file:line` and records its directives; `no-run` units are indexed but
not written, since nothing should try to compile them.

Run from anywhere -- paths resolve against the repo root.
"""

from __future__ import annotations

import json
import re
from pathlib import Path

ROOT = Path(__file__).resolve().parent.parent
DOCS = ROOT / "doc"
OUT = ROOT / "temp" / "docblocks"
INDEX = ROOT / "temp" / "docblocks_index.json"

FENCE = re.compile(r"^```lambda(?:\s+(.*?))?\s*$")
# a table opts in with this comment on the line above it; the comment and the
# table must both start at column 0, since an indented line is an indented
# code block in GFM and never a table
TABLE_META = re.compile(r"^<!--\s*code-fence:\s*lambda\b([^>]*?)-->\s*$")
CODE_SPAN = re.compile(r"^`([^`]+)`$")

WRAP = {"type": "type x = {}", "expr": "({})", None: "{}"}


def kind_of(directives):
    """The wrap-selecting directive, or None for a plain script block."""
    return next((d for d in directives if d in ("type", "expr")), None)


def wrap(directives, code):
    return WRAP[kind_of(directives)].format(code)


def split_row(row):
    """Split a table row on unescaped pipes -- `int \\| string` stays one cell."""
    return [c.strip() for c in re.split(r"(?<!\\)\|", row.strip().strip("|"))]


def scan(path):
    """Yield (start_line, directives, code) for every unit in one document."""
    lines = path.read_text(encoding="utf-8", errors="replace").splitlines()
    pending = None  # directives from a table annotation awaiting its table
    i = 0
    while i < len(lines):
        raw, stripped = lines[i], lines[i].strip()

        meta = TABLE_META.match(raw)
        if meta:
            pending = meta.group(1).split()
            i += 1
            continue

        if pending is not None and raw.startswith("|"):
            start = i
            rows = []
            while i < len(lines) and lines[i].startswith("|"):
                rows.append((i + 1, lines[i]))
                i += 1
            for lineno, row in rows[2:]:  # skip header + separator
                cells = split_row(row)
                if not cells:
                    continue
                m = CODE_SPAN.match(cells[0])
                if m:  # a cell that is anything else is prose, and is skipped
                    yield lineno, pending, m.group(1).replace(r"\|", "|")
            pending = None
            continue

        # a fence may be indented inside a list item, so match on the strip
        fence = FENCE.match(stripped)
        if fence:
            directives = (fence.group(1) or "").split()
            start, buf = i + 1, []
            i += 1
            while i < len(lines) and not lines[i].strip().startswith("```"):
                buf.append(lines[i])
                i += 1
            i += 1
            yield start, directives, "\n".join(buf)
            continue

        if stripped:
            pending = None  # a table annotation only binds to the next table
        i += 1


def main():
    OUT.mkdir(parents=True, exist_ok=True)
    for stale in OUT.glob("*.ls"):
        stale.unlink()

    index, written, skipped = [], 0, 0
    for doc in sorted(DOCS.rglob("*.md")):
        for start, directives, code in scan(doc):
            entry = {
                "file": str(doc.relative_to(ROOT)),
                "line": start,
                "directives": directives,
                "kind": kind_of(directives),
                "expect_error": next(
                    (d.split("=", 1)[1] for d in directives if d.startswith("error=")),
                    None,
                ),
            }
            if "no-run" in directives:
                entry["path"] = None  # indexed for reporting, never compiled
                skipped += 1
            else:
                path = OUT / f"b{len(index):04d}.ls"
                path.write_text(wrap(directives, code) + "\n", encoding="utf-8")
                entry["path"] = str(path.relative_to(ROOT))
                written += 1
            index.append(entry)

    INDEX.write_text(json.dumps(index, indent=1), encoding="utf-8")
    print(f"extracted {written} unit(s) to {OUT.relative_to(ROOT)}"
          f" ({skipped} no-run skipped); index: {INDEX.relative_to(ROOT)}")


if __name__ == "__main__":
    main()
