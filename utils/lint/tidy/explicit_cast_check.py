#!/usr/bin/env python3
"""
explicit_cast_check.py — type-aware detection of `(int)float_expr` casts.

This is the explicit-cast half of the proposal §4.3 "int-cast-type-aware"
rule. clang-tidy's built-in `bugprone-narrowing-conversions` deliberately
does *not* fire on explicit casts (the programmer asked for it), but in the
Radiant layout codebase a `(int)view->width` cast IS the bug — pixel-grid
truncation that should have been a `roundf`/`floorf`/`ceilf` or an explicit
`INT_CAST_OK` marker.

The pattern equivalent (`no-int-cast-radiant` in ast-grep) catches all `(int)`
casts and requires ~284 `// INT_CAST_OK:` markers to suppress legitimate
cases. This tool uses libclang's actual type info to fire *only* when the
operand's type is float / double, so most of those markers can be removed.

Usage:
  explicit_cast_check.py <file>...        # emits NDJSON to stdout

Each finding has the same shape as ast-grep + alint records, so run.sh
can splice them into the unified pipeline without further translation.

Limitations:
  - Files that fail to parse (missing platform headers, etc.) still produce
    findings from whichever parts of the AST were built; partial-tree
    behaviour mirrors ast-grep / tree-sitter.
  - `static_cast<int>(float_expr)` is also flagged (same hazard).
  - Casts inside template instantiations may double-fire — accepted noise.
"""
import argparse
import json
import os
import sys

import clang.cindex
from clang.cindex import CursorKind, TypeKind

# Compile flags applied to each file — mirrors the existing `make tidy` target.
# Includes the platform shim so GLFW/etc. headers don't hard-error the parse.
COMPILE_ARGS = [
    "-std=c++17",
    "-I.",
    "-Ilib",
    "-Ilib/mem-pool/include",
    "-Ilambda",
    "-Iradiant",
    "-Iinclude",
    # Force-include math so floorf/sinf/etc. resolve to a concrete float-return
    # type. Without this, the AST reports `<dependent>` and the source-type walk
    # mis-classifies `(int)floorf(x)` as non-float (false negative).
    "-include", "math.h",
    # Silence noisy partial-parse diagnostics — we only care about our matchers.
    "-w",
]

# Integer destination kinds we flag when narrowing from float.
INT_KINDS = {
    TypeKind.BOOL, TypeKind.CHAR_U, TypeKind.UCHAR, TypeKind.CHAR16, TypeKind.CHAR32,
    TypeKind.USHORT, TypeKind.UINT, TypeKind.ULONG, TypeKind.ULONGLONG, TypeKind.UINT128,
    TypeKind.CHAR_S, TypeKind.SCHAR, TypeKind.WCHAR, TypeKind.SHORT, TypeKind.INT,
    TypeKind.LONG, TypeKind.LONGLONG, TypeKind.INT128,
}

AUDIT_NON_FLOAT = False  # set by --audit-non-float

# Source kinds we treat as float (narrowing from any of these to int is the bug).
FLOAT_KINDS = {TypeKind.FLOAT, TypeKind.DOUBLE, TypeKind.LONGDOUBLE}

# Source kinds we treat as DEFINITELY non-float. Used by the marker-cleanup
# audit: only remove an INT_CAST_OK marker if the tool can prove the source
# isn't float. Anything else (DEPENDENT / OVERLOAD / unresolved template) is
# inconclusive and the marker must stay (conservative).
NON_FLOAT_CONCRETE_KINDS = INT_KINDS | {
    TypeKind.ENUM,
    TypeKind.POINTER,
    TypeKind.LVALUEREFERENCE,
    TypeKind.RVALUEREFERENCE,
    TypeKind.RECORD,         # struct/class
    TypeKind.VOID,
}


def canonical_kind(t):
    """Strip typedefs / qualifiers and return the canonical TypeKind."""
    return t.get_canonical().kind


def is_int(t):
    return canonical_kind(t) in INT_KINDS


def is_float(t):
    return canonical_kind(t) in FLOAT_KINDS


def is_definitely_non_float(t):
    """True iff `t` is a concrete non-float type — used by --audit-non-float
    to decide whether a marker is provably unnecessary. DEPENDENT, OVERLOAD,
    and other unresolved kinds return False (inconclusive → keep the marker)."""
    return canonical_kind(t) in NON_FLOAT_CONCRETE_KINDS


def cast_source_type(cast_cursor):
    """Resolve the source type of a cast by descending through implicit-
    conversion wrappers.

    libclang reports the IMMEDIATE child of a CSTYLE_CAST_EXPR with the *cast*
    type, not the source. The real source type sits one or more
    UNEXPOSED_EXPR / implicit-conversion nodes deeper. We walk down through
    those wrappers, taking the type of the first descendant whose canonical
    kind differs from the cast's destination kind.
    """
    dest_kind = cast_cursor.type.get_canonical().kind

    # First non-type child of the cast: this is the operand (still typed as dest).
    operand = None
    for child in cast_cursor.get_children():
        if child.kind not in (CursorKind.TYPE_REF, CursorKind.TEMPLATE_REF,
                              CursorKind.NAMESPACE_REF):
            operand = child
            break
    if operand is None:
        return None

    # Walk down through implicit conversions until the type kind changes.
    node = operand
    seen = 0
    while seen < 8:
        kind = node.type.get_canonical().kind
        if kind != dest_kind:
            return node.type
        children = list(node.get_children())
        if not children:
            return node.type   # nothing deeper to look at; this is the bottom
        node = children[0]
        seen += 1
    return node.type


def has_int_cast_ok_marker(cursor):
    """Mirror ast-grep suppression — line containing `// INT_CAST_OK` is OK."""
    loc = cursor.location
    if loc.file is None:
        return False
    try:
        with open(loc.file.name, "rb") as fh:
            for i, raw in enumerate(fh, start=1):
                if i == loc.line:
                    return b"INT_CAST_OK" in raw
                if i > loc.line:
                    break
    except OSError:
        return False
    return False


def make_record(cursor, source_t, dest_t, rel_path, rule_id="int-cast-type-aware"):
    loc = cursor.location
    extent = cursor.extent
    snippet = ""
    try:
        with open(loc.file.name, "rb") as fh:
            lines = fh.readlines()
            if 0 < loc.line <= len(lines):
                snippet = lines[loc.line - 1].decode("utf-8", "replace").rstrip()
    except OSError:
        pass
    return {
        "ruleId": rule_id,
        "severity": "warning",
        "message": (
            f"Explicit cast from {source_t.spelling} to {dest_t.spelling} — "
            f"silent truncation. Drop the cast, use roundf/floorf/ceilf, or "
            f"mark with // INT_CAST_OK: <reason>."
        ),
        "file": rel_path,
        "range": {
            "start": {"line": loc.line - 1, "column": loc.column - 1},
            "end":   {"line": extent.end.line - 1, "column": extent.end.column - 1},
            "byteOffset": {"start": extent.start.offset, "end": extent.end.offset},
        },
        "text": cursor.spelling or snippet.strip(),
        "lines": snippet,
        "metadata": {"suppress": "INT_CAST_OK"},
        "backend": "clang-tidy",
    }


def walk(cursor, rel_path, repo_root, hits):
    # Skip declarations from outside the file we asked for (system headers, etc.)
    if cursor.location.file is not None:
        try:
            cur_path = os.path.relpath(cursor.location.file.name, repo_root)
        except ValueError:
            cur_path = None
        if cur_path != rel_path and cursor.kind != CursorKind.TRANSLATION_UNIT:
            # Still descend (some matches are inside includes that delegate),
            # but only RECORD if the cursor itself is in our file.
            in_file = False
        else:
            in_file = True
    else:
        in_file = False

    if in_file and cursor.kind in (CursorKind.CSTYLE_CAST_EXPR,
                                   CursorKind.CXX_STATIC_CAST_EXPR):
        dest_t = cursor.type
        source_t = cast_source_type(cursor)
        if source_t is not None and is_int(dest_t):
            if AUDIT_NON_FLOAT:
                # Marker-cleanup mode: report casts whose source is provably
                # non-float (so the marker covering them is unnecessary).
                if is_definitely_non_float(source_t):
                    hits.append(make_record(cursor, source_t, dest_t, rel_path,
                                            rule_id="non-float-cast-marker-orphan"))
            elif is_float(source_t):
                if not has_int_cast_ok_marker(cursor):
                    hits.append(make_record(cursor, source_t, dest_t, rel_path))

    for child in cursor.get_children():
        walk(child, rel_path, repo_root, hits)


def check_file(path, repo_root, index):
    rel_path = os.path.relpath(path, repo_root)
    try:
        tu = index.parse(path, args=COMPILE_ARGS,
                         options=clang.cindex.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES * 0)
    except clang.cindex.TranslationUnitLoadError as exc:
        sys.stderr.write(f"explicit_cast_check: {rel_path}: load error: {exc}\n")
        return []
    hits = []
    walk(tu.cursor, rel_path, repo_root, hits)
    return hits


def main():
    ap = argparse.ArgumentParser(description=__doc__)
    ap.add_argument("files", nargs="+")
    ap.add_argument("--repo-root", default=os.getcwd())
    ap.add_argument("--audit", action="store_true",
                    help="report ALL float→int casts, ignoring INT_CAST_OK markers.")
    ap.add_argument("--audit-non-float", action="store_true",
                    help="report only int-target casts whose source is PROVABLY "
                         "non-float (resolved to int/enum/pointer/etc.). "
                         "Lines reported by this mode are safe to delete the "
                         "INT_CAST_OK marker from. Lines NOT reported include "
                         "both genuine float→int casts AND any unresolved "
                         "(template, missing-header) cases — markers there stay.")
    args = ap.parse_args()

    if args.audit or args.audit_non_float:
        global has_int_cast_ok_marker
        has_int_cast_ok_marker = lambda cursor: False
    global AUDIT_NON_FLOAT
    AUDIT_NON_FLOAT = args.audit_non_float

    index = clang.cindex.Index.create()
    for f in args.files:
        for record in check_file(os.path.abspath(f), args.repo_root, index):
            sys.stdout.write(json.dumps(record) + "\n")


if __name__ == "__main__":
    main()
