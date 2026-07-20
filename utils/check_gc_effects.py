#!/usr/bin/env python3
"""Verify that every JIT NO_GC import remains transitively allocation-free.

This is intentionally a conservative source-level call-graph check.  The JIT
registry is the classification authority: roots are read from its NO_GC rows,
then compared with the independently frozen audited list in the same file.
For project functions, direct calls are followed transitively.  Calls that
cannot be resolved to project code must be present in the small verified-leaf
set below; an unknown direct or member call fails the check.

The check does not attempt to classify all runtime functions.  Unknown imports
remain MAY_GC by construction, so only the deliberately exceptional NO_GC set
needs a proof that fails closed when its implementation changes.
"""

from __future__ import annotations

import re
import sys
from bisect import bisect_right
from dataclasses import dataclass
from pathlib import Path


ROOT = Path(__file__).resolve().parent.parent
REGISTRY = ROOT / "lambda" / "sys_func_registry.c"
SOURCE_ROOTS = (ROOT / "lambda", ROOT / "lib")
SOURCE_SUFFIXES = {".c", ".cc", ".cpp", ".h", ".hh", ".hpp"}
SKIP_SOURCE_PARTS = {
    "node_modules", "package", "sqlite", "tree-sitter", "tree-sitter-bash",
    "tree-sitter-javascript", "tree-sitter-lambda", "tree-sitter-latex",
    "tree-sitter-latex-math", "tree-sitter-python", "tree-sitter-ruby",
    "tree-sitter-typescript",
}


# C/POSIX/compiler leaves whose implementations are outside the project.  Each
# is deterministic over caller-owned memory/scalars and cannot enter Lambda GC
# or generated code.  Adding an entry is a security-relevant review action.
VERIFIED_EXTERNAL_LEAVES = {
    "abort",
    "d2it",
    "fmod",
    "isnan",
    "k2it",
    "l2it",
    "lambda_float_ptr_to_item",
    "log_debug",
    "memcpy",
    "memset",
    "mpd_free",
    "mpd_isinteger",
    "mpd_iszero",
    "mpd_qget_ssize",
    "mpd_to_sci",
    "strtod",
    "u2it",
}

# Inline Item/representation readers are methods rather than free functions,
# so their bodies are not reliably recoverable as unqualified call-graph
# nodes.  They only inspect the Item word or its already-owned payload.
VERIFIED_READER_METHODS = {
    "bool_value",
    "get_chars",
    "get_double",
    "get_decimal",
    "get_int56",
    "get_int64",
    "get_datetime",
    "get_datetime_ptr",
    "get_num_sized_as_double",
    "get_num_sized_as_int64",
    "get_num_type",
    "get_safe_binary",
    "get_safe_string",
    "get_uint64",
    "is_inline_int64",
    "type_id",
}

# Function-like language constructs and local RAII variable construction that
# the lexical call extractor can otherwise mistake for an indirect call.
NON_CALL_TOKENS = {
    "alignas", "alignof", "asm", "catch", "decltype", "do", "for", "if",
    "no_gc", "return", "sizeof", "static_assert", "switch", "while",
}

# Public allocation/collection and generated-code entry names.  Reaching one
# is always incompatible with NO_GC, even if a same-named definition was not
# found because it is supplied by another build configuration.
FORBIDDEN_PATTERNS = (
    re.compile(r"^heap_(?:alloc|calloc|alloc_class|alloc_obj|data_alloc|gc_collect|create_)") ,
    re.compile(r"^gc_(?:data_alloc|collect|collect_with_root_region)$"),
    re.compile(r"^js_(?:call_function|apply_function|apply_constructor|builtin_eval)$"),
    re.compile(r"^(?:malloc|calloc|realloc|aligned_alloc)$"),
)


@dataclass(frozen=True)
class FunctionBody:
    name: str
    path: Path
    line: int
    body: str


def strip_comments_and_literals(source: str) -> str:
    """Blank comments/literal contents while preserving offsets and newlines."""
    out = list(source)
    i = 0
    n = len(source)
    while i < n:
        if source.startswith("//", i):
            end = source.find("\n", i + 2)
            if end < 0:
                end = n
            for j in range(i, end):
                out[j] = " "
            i = end
            continue
        if source.startswith("/*", i):
            end = source.find("*/", i + 2)
            end = n if end < 0 else end + 2
            for j in range(i, end):
                if out[j] != "\n":
                    out[j] = " "
            i = end
            continue
        if source[i] in {'"', "'"}:
            quote = source[i]
            j = i + 1
            while j < n:
                if source[j] == "\\":
                    j += 2
                    continue
                if source[j] == quote:
                    j += 1
                    break
                j += 1
            for k in range(i, min(j, n)):
                if out[k] != "\n":
                    out[k] = " "
            i = j
            continue
        i += 1
    return "".join(out)


def matching_left(text: str, right: int, opening: str, closing: str) -> int:
    depth = 1
    i = right - 1
    while i >= 0:
        if text[i] == closing:
            depth += 1
        elif text[i] == opening:
            depth -= 1
            if depth == 0:
                return i
        i -= 1
    return -1


def matching_right(text: str, left: int, opening: str, closing: str) -> int:
    depth = 1
    i = left + 1
    while i < len(text):
        if text[i] == opening:
            depth += 1
        elif text[i] == closing:
            depth -= 1
            if depth == 0:
                return i
        i += 1
    return -1


def function_bodies(path: Path) -> list[FunctionBody]:
    source = path.read_text(encoding="utf-8", errors="replace")
    clean = strip_comments_and_literals(source)
    newline_offsets = [index for index, char in enumerate(source) if char == "\n"]
    result: list[FunctionBody] = []
    brace_pairs: dict[int, int] = {}
    brace_stack: list[int] = []
    paren_pairs: dict[int, int] = {}
    paren_stack: list[int] = []
    for index, char in enumerate(clean):
        if char == "{":
            brace_stack.append(index)
        elif char == "}" and brace_stack:
            brace_pairs[brace_stack.pop()] = index
        elif char == "(":
            paren_stack.append(index)
        elif char == ")" and paren_stack:
            paren_pairs[index] = paren_stack.pop()
    for brace, char in enumerate(clean):
        if char != "{":
            continue
        close_paren = brace - 1
        while close_paren >= 0 and clean[close_paren].isspace():
            close_paren -= 1
        # Permit the ordinary post-parameter qualifiers used by project code.
        if close_paren < 0 or clean[close_paren] != ")":
            window_start = max(0, brace - 160)
            candidates = [m.end() - 1 for m in re.finditer(r"\)", clean[window_start:brace])]
            if not candidates:
                continue
            close_paren = window_start + candidates[-1]
            suffix = clean[close_paren + 1:brace]
            if re.search(r"[;={}\[\]]", suffix):
                continue
        open_paren = paren_pairs.get(close_paren, -1)
        if open_paren < 0:
            continue
        name_window_start = max(0, open_paren - 180)
        name_match = re.search(
            r"(?:[A-Za-z_]\w*::)*[~A-Za-z_]\w*$",
            clean[name_window_start:open_paren],
        )
        if not name_match:
            continue
        name_start = name_window_start + name_match.start()
        qualified_name = name_match.group(0)
        name = qualified_name.rsplit("::", 1)[-1]
        if name in NON_CALL_TOKENS or name.startswith("operator"):
            continue
        # A declaration/assignment between the previous statement boundary
        # and the candidate name means this is a call inside a control block,
        # not a function definition.
        boundary = max(clean.rfind(";", 0, name_start),
                       clean.rfind("}", 0, name_start),
                       clean.rfind("{", 0, name_start))
        prefix = clean[boundary + 1:name_start]
        if "=" in prefix or re.search(r"\b(?:return|case|new)\b", prefix):
            continue
        body_end = brace_pairs.get(brace, -1)
        if body_end < 0:
            continue
        line = bisect_right(newline_offsets, name_start) + 1
        result.append(FunctionBody(name, path, line, clean[brace + 1:body_end]))
    return result


def extract_no_gc_names(registry: str) -> set[str]:
    pattern = re.compile(
        r'\{"([^"]+)"\s*,\s*FPTR\([^)]*\)\s*,\s*\{\s*JIT_EFFECT_NO_GC',
        re.MULTILINE,
    )
    return set(pattern.findall(registry))


def extract_frozen_audit_names(registry: str) -> set[str]:
    match = re.search(
        r'static const char\* audited\[\]\s*=\s*\{(?P<body>.*?)\};',
        registry,
        re.DOTALL,
    )
    if not match:
        raise ValueError("frozen audited NO_GC list not found")
    return set(re.findall(r'"([^"]+)"', match.group("body")))


def extract_calls(body: str) -> tuple[set[str], set[str]]:
    direct: set[str] = set()
    members: set[str] = set()
    for match in re.finditer(r"\b([A-Za-z_]\w*)\s*\(", body):
        name = match.group(1)
        if name in NON_CALL_TOKENS or name.isupper():
            continue
        prefix = body[max(0, match.start() - 3):match.start()]
        if re.search(r"(?:\.|->)\s*$", prefix):
            members.add(name)
        else:
            direct.add(name)
    return direct, members


def is_forbidden(name: str) -> bool:
    return any(pattern.search(name) for pattern in FORBIDDEN_PATTERNS)


def relative(path: Path) -> str:
    return str(path.relative_to(ROOT))


def main() -> int:
    registry = REGISTRY.read_text(encoding="utf-8")
    no_gc_names = extract_no_gc_names(registry)
    try:
        frozen_names = extract_frozen_audit_names(registry)
    except ValueError as exc:
        print(f"gc-effects: {exc}", file=sys.stderr)
        return 1

    errors: list[str] = []
    if no_gc_names != frozen_names:
        missing = sorted(no_gc_names - frozen_names)
        stale = sorted(frozen_names - no_gc_names)
        if missing:
            errors.append("NO_GC imports missing from frozen audit: " + ", ".join(missing))
        if stale:
            errors.append("frozen audit entries no longer NO_GC: " + ", ".join(stale))

    definitions: dict[str, list[FunctionBody]] = {}
    for source_root in SOURCE_ROOTS:
        for path in source_root.rglob("*"):
            if not path.is_file() or path.suffix not in SOURCE_SUFFIXES:
                continue
            if any(part in SKIP_SOURCE_PARTS for part in path.parts):
                continue
            for function in function_bodies(path):
                definitions.setdefault(function.name, []).append(function)

    roots = sorted(no_gc_names - VERIFIED_EXTERNAL_LEAVES)
    visited: set[str] = set()
    stack: list[tuple[str, tuple[str, ...]]] = [(name, (name,)) for name in reversed(roots)]
    while stack:
        name, path_to_name = stack.pop()
        if name in visited:
            continue
        visited.add(name)
        bodies = definitions.get(name)
        if not bodies:
            errors.append(f"unresolved audited function: {' -> '.join(path_to_name)}")
            continue
        for function in bodies:
            direct, members = extract_calls(function.body)
            for member in sorted(members - VERIFIED_READER_METHODS):
                errors.append(
                    f"unverified member call {' -> '.join(path_to_name)} -> {member} "
                    f"at {relative(function.path)}:{function.line}"
                )
            for callee in sorted(direct):
                if callee in NON_CALL_TOKENS:
                    continue
                next_path = path_to_name + (callee,)
                if is_forbidden(callee):
                    errors.append(
                        f"forbidden GC/re-entry path: {' -> '.join(next_path)} "
                        f"at {relative(function.path)}:{function.line}"
                    )
                elif callee in VERIFIED_EXTERNAL_LEAVES:
                    continue
                elif callee in definitions:
                    stack.append((callee, next_path))
                else:
                    errors.append(
                        f"unresolved call {' -> '.join(next_path)} "
                        f"at {relative(function.path)}:{function.line}"
                    )

    if errors:
        print("gc-effects: NO_GC transitive audit failed", file=sys.stderr)
        for error in sorted(set(errors)):
            print(f"  - {error}", file=sys.stderr)
        return 1

    print(
        f"gc-effects: {len(no_gc_names)} NO_GC imports verified; "
        f"{len(visited)} project call-graph nodes"
    )
    return 0


if __name__ == "__main__":
    raise SystemExit(main())
