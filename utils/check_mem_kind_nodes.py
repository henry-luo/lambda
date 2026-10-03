#!/usr/bin/env python3
"""Check that every lam::Up<T> / lam::Shared<T> field points at an outliving level.

An Up<T> field claims its target lives in the holder's level of the ownership
tree or in an ancestor level, so the target outlives the holder. The C++ type
cannot check that claim: the wrapper sees T but not its enclosing struct. This
lint reads the claim from the Clang AST:

  * levels:  structs in namespace lam with a `parent` typedef (lib/mem_node.hpp)
  * homes:   LAM_NODE_OF(T, Level) specializations of lam::NodeOf<T>
  * claims:  every FIELD_DECL of type lam::Up<T> or lam::Shared<T> in a repo struct

A Shared<T> field makes the same claim about the shared value's owner, so it
is checked the same way.

A field is a violation when both the holder and the target have a declared
level and the target's level is neither the holder's level nor one of its
ancestors. A Stack holder (NodeStack) may point at any level. Fields whose
holder or target has no declared level are reported as unchecked, so the
coverage grows as headers are converted. The debug heap tracer checks actual
placements; this lint checks the declarations.

It also ratchets raw pointer fields: every pointer field in a struct defined in
one of HEADERS must carry a kind, unless it is listed in RAW_ALLOWLIST (fields
whose kind is still open, each with a reason). A new raw field fails; a listed
field that gained a kind must be dropped from the list.

Exit status: 0 clean, 1 violations, 2 setup failure.
"""

from __future__ import annotations

import argparse
import json
import os
import subprocess
import sys
from collections import defaultdict
from pathlib import Path

ROOT = Path(__file__).resolve().parents[1]
sys.path.insert(0, str(ROOT / "utils"))
import struct_census as census  # noqa: E402  (shared libclang loader and TU flags)

# headers whose structs carry kind fields; parsed together as one TU
HEADERS = [
    "lambda/input/css/dom_element.hpp",
    "radiant/view.hpp",
    "radiant/layout.hpp",
    "radiant/render.hpp",
]
STACK_LEVEL = "lam::NodeStack"
TU_NAME = "temp/check_mem_kind_nodes_tu.cpp"
RAW_ALLOWLIST = ROOT / "utils" / "lint" / "mem_kind_raw_fields.txt"
RAW_HEADERS = HEADERS + ["lambda/input/css/dom_node.hpp"]


def bare(spelling: str) -> str:
    """Canonical type spelling without cv-qualifiers or elaborated tags."""
    for word in ("const ", "volatile ", "struct ", "class ", "union "):
        spelling = spelling.replace(word, "")
    return spelling.strip()


def enclosing_record(cursor, ci):
    """Nearest named struct/class around a field (skips anonymous unions)."""
    rec_kinds = (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL, ci.CursorKind.UNION_DECL)
    c = cursor.semantic_parent
    while c is not None and c.kind in rec_kinds:
        if c.spelling and not c.is_anonymous():
            return c
        c = c.semantic_parent
    return None


def collect(tu, ci, cfg):
    levels = {}      # level -> parent level (None at a root)
    homes = {}       # struct -> level
    claims = []      # (holder, field, target, file, line)
    raw = {}         # "Holder::field" -> "file:line" for raw pointer fields in RAW_HEADERS
    seen = set()
    for c in tu.cursor.walk_preorder():
        if c.kind in (ci.CursorKind.STRUCT_DECL, ci.CursorKind.CLASS_DECL) and c.is_definition():
            parent_ns = c.semantic_parent
            in_lam = parent_ns is not None and parent_ns.kind == ci.CursorKind.NAMESPACE \
                and parent_ns.spelling == "lam"
            if in_lam and c.spelling == "NodeOf":
                arg = c.type.get_template_argument_type(0)
                for ch in c.get_children():
                    if ch.kind == ci.CursorKind.TYPEDEF_DECL and ch.spelling == "type":
                        homes[bare(arg.get_canonical().spelling)] = \
                            bare(ch.underlying_typedef_type.get_canonical().spelling)
                continue
            if in_lam:
                for ch in c.get_children():
                    if ch.kind == ci.CursorKind.TYPEDEF_DECL and ch.spelling == "parent":
                        p = bare(ch.underlying_typedef_type.get_canonical().spelling)
                        levels["lam::" + c.spelling] = None if p == "void" else p
                continue
        if c.kind != ci.CursorKind.FIELD_DECL or c.location.file is None:
            continue
        rel = census.owned(c.location.file.name, cfg)
        if rel is None:
            continue
        t = c.type.get_canonical()
        if rel in RAW_HEADERS:
            elem = t
            while elem.kind == ci.TypeKind.CONSTANTARRAY:
                elem = elem.element_type
            if elem.kind == ci.TypeKind.POINTER and elem.get_pointee().kind != ci.TypeKind.FUNCTIONPROTO:
                owner = enclosing_record(c, ci)
                if owner is not None:
                    raw[f"{bare(owner.type.get_canonical().spelling)}::{c.spelling}"] = f"{rel}:{c.location.line}"
        kind = bare(t.spelling).split("<", 1)[0]
        if kind not in ("lam::Up", "lam::Shared"):
            continue
        holder = enclosing_record(c, ci)
        if holder is None:
            continue
        key = (holder.get_usr(), c.spelling)
        if key in seen:
            continue
        seen.add(key)
        target = bare(t.get_template_argument_type(0).get_canonical().spelling)
        claims.append((bare(holder.type.get_canonical().spelling), c.spelling, target,
                       rel, c.location.line, kind[len("lam::"):]))
    return levels, homes, claims, raw


def ancestors_or_self(level, levels):
    out = []
    while level is not None and level not in out:
        out.append(level)
        level = levels.get(level)
    return out


def main() -> int:
    ap = argparse.ArgumentParser(description=__doc__.split("\n\n")[0])
    ap.add_argument("--verbose", action="store_true", help="list unchecked fields")
    ap.add_argument("--write-allowlist", action="store_true",
                    help="rewrite the raw-field allowlist from the current headers")
    args = ap.parse_args()

    cfg = json.loads(census.DEFAULT_CONFIG.read_text())
    os.chdir(ROOT)
    if sys.platform == "darwin" and not os.environ.get("SDKROOT"):
        # libclang has no driver to find the SDK; tu_args reads SDKROOT
        os.environ["SDKROOT"] = subprocess.run(["xcrun", "--show-sdk-path"], capture_output=True,
                                               text=True).stdout.strip()
    try:
        ci = census.load_clang(cfg)
    except SystemExit:
        print("mem-kind-nodes: skipped, libclang bindings not found (see utils/Struct_Census.md)")
        return 0
    source = "".join(f'#include "{h}"\n' for h in HEADERS)
    index = ci.Index.create()
    try:
        tu = index.parse(TU_NAME, args=census.tu_args(TU_NAME, cfg),
                         unsaved_files=[(TU_NAME, source)],
                         options=ci.TranslationUnit.PARSE_SKIP_FUNCTION_BODIES)
    except ci.TranslationUnitLoadError as e:
        print(f"mem-kind-nodes: cannot parse headers: {e}", file=sys.stderr)
        return 2
    fatal = [d for d in tu.diagnostics if d.severity >= ci.Diagnostic.Error]
    if fatal:
        for d in fatal[:10]:
            print(f"mem-kind-nodes: parse error: {d}", file=sys.stderr)
        return 2

    levels, homes, claims, raw = collect(tu, ci, cfg)
    violations, unchecked = [], defaultdict(list)
    for holder, field, target, rel, line, kind in claims:
        hl, tl = homes.get(holder), homes.get(target)
        if hl == STACK_LEVEL:
            continue
        if hl is None or tl is None:
            unchecked["holder" if hl is None else "target"].append(
                f"{rel}:{line}: {holder}::{field} -> {target}")
            continue
        if tl not in ancestors_or_self(hl, levels):
            violations.append(f"{rel}:{line}: error: {holder}::{field} is {kind}<{target}>, "
                              f"but {target} lives in {tl}, which is not {hl} or its ancestor")

    allowed = {}
    if RAW_ALLOWLIST.exists():
        for line in RAW_ALLOWLIST.read_text().splitlines():
            line = line.split("#", 1)[0].strip()
            if line:
                allowed[line.split()[0]] = line
    if args.write_allowlist:
        RAW_ALLOWLIST.write_text("".join(f"{k}  # {raw[k]}\n" for k in sorted(raw)))
        print(f"mem-kind-nodes: wrote {len(raw)} raw fields to {RAW_ALLOWLIST.relative_to(ROOT)}")
        return 0
    for key in sorted(set(raw) - set(allowed)):
        violations.append(f"{raw[key]}: error: raw pointer field {key} has no kind "
                          f"(give it lam::Own/Up/OwnArr/Counted/Shared/Foreign/Handle)")
    for key in sorted(set(allowed) - set(raw)):
        violations.append(f"{RAW_ALLOWLIST.relative_to(ROOT)}: error: {key} is no longer a raw "
                          f"pointer field; drop it from the list")

    for v in violations:
        print(v)
    n_unchecked = sum(len(v) for v in unchecked.values())
    print(f"mem-kind-nodes: {len(claims)} Up/Shared fields, {len(violations)} violations, "
          f"{n_unchecked} unchecked ({len(unchecked['holder'])} undeclared holder level, "
          f"{len(unchecked['target'])} undeclared target level); {len(raw)} raw pointer fields allowed")
    if args.verbose:
        for what, rows in unchecked.items():
            for r in rows:
                print(f"  unchecked ({what}): {r}")
    return 1 if violations else 0


if __name__ == "__main__":
    sys.exit(main())
