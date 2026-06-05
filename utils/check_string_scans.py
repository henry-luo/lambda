#!/usr/bin/env python3
"""
check_string_scans.py — lint for unsafe open-coded string scanning.

Flags the leaf operations that have repeatedly caused parser over-reads, and
points each at the centralized, NUL-safe replacement in lib/str.h (§17):

  - strchr(<set>, *p) / strchr(<set>, p[i])   membership that matches '\0'
  - isspace/isdigit/...(*p) on a char*         UB for bytes >= 0x80 (signed char)

These two are high-confidence and reported as errors by default. A softer,
opt-in class (--all) also reports raw run/skip loops that *usually* have a
guard but are worth reviewing:

  - while (*p == <marker>) ...                 run loop; prefer str(n)_count_run

Suppress an intentional local parser with a searchable trailing comment:

  // STR_SCAN_LOCAL_OK: <reason>

Usage:
  utils/check_string_scans.py [paths...]      # default: lambda/ radiant/ lib/
  utils/check_string_scans.py --all           # include soft run-loop warnings
  utils/check_string_scans.py --quiet         # only print the summary line

Exit status: non-zero if any error-level finding is present (CI-friendly).
"""

import os
import re
import sys

DEFAULT_ROOTS = ["lambda", "radiant", "lib"]
SRC_EXT = (".c", ".cpp", ".h", ".hpp", ".cc", ".cxx")

SUPPRESS = "STR_SCAN_LOCAL_OK"

# directories whose contents are generated or third-party — never our problem.
SKIP_DIR_SUBSTR = (
    "tree-sitter",            # generated parsers + their scanners
    "/sqlite/",               # vendored SQLite amalgamation
    "/test/",                 # tests carry deliberate edge cases
    "/tests/",
    "ref/",                   # vendored references
    "third_party",
    "node_modules",
    "/build/",
    "/temp/",
)

# files that define the helpers themselves, or are otherwise exempt.
SKIP_FILE = (
    "lib/str.c",
    "lib/str.h",
    "lib/strview.c",          # low-level cstr primitives
)

# ── error-level patterns ──────────────────────────────────────────────────

# strchr(<set>, *p) — membership test on the *current byte*. This is the
# high-confidence bug shape: when *p == '\0', strchr matches the set literal's
# terminator. Reported as an error.
RE_STRCHR_MEMBER = re.compile(
    r"\bstrchr\s*\([^,]+,\s*\*\s*[A-Za-z_]\w*"
)

# strchr(<set>, p[i]) — index form. Often a legitimate search for a delimiter
# byte (e.g. strchr(haystack, sep[0])), so it is a soft warning, not an error.
RE_STRCHR_INDEX = re.compile(
    r"\bstrchr\s*\([^,]+,\s*[A-Za-z_]\w*\s*\["
)

# ctype on a raw char deref/index: isspace(*p), isdigit(p[i]), etc.
# a cast like isdigit((unsigned char)*p) is correct and is excluded.
CTYPE = r"is(?:space|digit|alnum|alpha|upper|lower|xdigit|punct|cntrl|print|graph|blank)"
RE_CTYPE_RAW = re.compile(
    r"\b" + CTYPE + r"\s*\(\s*(?:\*\s*[A-Za-z_]|[A-Za-z_]\w*\s*\[)"
)
RE_CTYPE_CAST_OK = re.compile(r"\b" + CTYPE + r"\s*\(\s*\(\s*unsigned\s+char\s*\)")

# ── soft-level pattern (only with --all) ──────────────────────────────────

# while (*p == <something>)  — a run loop; safe only with a prior non-NUL guard.
RE_RUN_LOOP = re.compile(r"while\s*\(\s*\*\s*[A-Za-z_]\w*\s*==")

ERROR_RULES = [
    (RE_STRCHR_MEMBER, None,
     "strchr(set, *p) matches '\\0' (terminator) — use str_char_in_set(*p, set)"),
    (RE_CTYPE_RAW, RE_CTYPE_CAST_OK,
     "ctype on char* is UB for bytes >= 0x80 — use str_char_is_* or cast to (unsigned char)"),
]


def iter_source_files(roots):
    for root in roots:
        if os.path.isfile(root):
            yield root
            continue
        for dirpath, _dirs, files in os.walk(root):
            for name in files:
                if not name.endswith(SRC_EXT):
                    continue
                path = os.path.join(dirpath, name)
                norm = "/" + path.replace(os.sep, "/").lstrip("./")
                if any(s in norm for s in SKIP_DIR_SUBSTR):
                    continue
                if any(path.replace(os.sep, "/").endswith(s) for s in SKIP_FILE):
                    continue
                yield path


def scan_file(path, include_soft):
    findings = []  # (lineno, severity, message, text)
    try:
        with open(path, "r", encoding="utf-8", errors="replace") as f:
            lines = f.readlines()
    except OSError:
        return findings
    for i, line in enumerate(lines, 1):
        if SUPPRESS in line:
            continue
        # ignore comment-only lines cheaply (best-effort, not a full C parser)
        stripped = line.lstrip()
        if stripped.startswith("//") or stripped.startswith("*"):
            continue
        for regex, exempt, msg in ERROR_RULES:
            if regex.search(line) and not (exempt and exempt.search(line)):
                findings.append((i, "error", msg, line.rstrip()))
        if include_soft and RE_RUN_LOOP.search(line):
            findings.append((i, "warn", "run loop — prefer str(n)_count_run or guard marker != '\\0'",
                             line.rstrip()))
        if include_soft and RE_STRCHR_INDEX.search(line):
            findings.append((i, "warn", "strchr(set, p[i]) — if this is membership not search, use str_char_in_set",
                             line.rstrip()))
    return findings


def main(argv):
    args = [a for a in argv[1:] if not a.startswith("-")]
    flags = {a for a in argv[1:] if a.startswith("-")}
    include_soft = "--all" in flags
    quiet = "--quiet" in flags
    roots = args if args else DEFAULT_ROOTS

    total_err = 0
    total_warn = 0
    files_with_findings = 0
    for path in sorted(set(iter_source_files(roots))):
        findings = scan_file(path, include_soft)
        if not findings:
            continue
        files_with_findings += 1
        if not quiet:
            for lineno, sev, msg, text in findings:
                tag = "error" if sev == "error" else "warn "
                print(f"{path}:{lineno}: {tag}: {msg}")
                print(f"    {text.strip()}")
        total_err += sum(1 for f in findings if f[1] == "error")
        total_warn += sum(1 for f in findings if f[1] == "warn")

    print(f"\ncheck-string-scan: {total_err} error(s), {total_warn} warning(s) "
          f"across {files_with_findings} file(s).")
    if total_err:
        print("Fix with lib/str.h §17 helpers, or annotate intentional parsers "
              f"with a trailing  // {SUPPRESS}: <reason>  comment.")
    return 1 if total_err else 0


if __name__ == "__main__":
    sys.exit(main(sys.argv))
