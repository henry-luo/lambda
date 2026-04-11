#!/usr/bin/env python3
"""Strip JS comments from test262 test files to reduce I/O and speed up parsing.

Creates a parallel directory structure under ref/test262-stripped/ containing
comment-stripped versions of all .js test files. The YAML frontmatter
(/*--- ... ---*/) is preserved since the metadata parser depends on it.

Harness files are copied as-is (they're loaded separately and already cached).

Usage: python3 utils/strip_test262_comments.py
"""

import os
import sys
import shutil
import time
from concurrent.futures import ProcessPoolExecutor, as_completed

SRC_ROOT = "ref/test262"
DST_ROOT = "ref/test262-stripped"

# Directories containing test files to strip
TEST_DIRS = ["test"]

# Directories to copy as-is (harness files, etc.)
COPY_DIRS = ["harness"]


def strip_js_comments(source):
    """Strip // and /* */ comments from JS source, preserving /*--- ... ---*/ frontmatter.

    Uses a character-level state machine to handle strings and template literals.
    """
    result = []
    i = 0
    n = len(source)

    # JS line terminators: LF, CR, LS (U+2028), PS (U+2029)
    LINE_TERMINATORS = {'\n', '\r', '\u2028', '\u2029'}

    # Track last significant character for regex vs division disambiguation
    # If last_sig is in this set, '/' starts a regex, not a comment/division
    REGEX_BEFORE = set('=({[,;!&|^~+-*%<>?:')

    def last_significant():
        """Return the last non-whitespace character in result, or ''."""
        for j in range(len(result) - 1, max(len(result) - 50, -1), -1):
            if result[j] not in (' ', '\t', '\n', '\r'):
                return result[j]
        return ''

    while i < n:
        c = source[i]

        # String literals (single or double quote)
        if c in ('"', "'"):
            quote = c
            result.append(c)
            i += 1
            while i < n and source[i] != quote:
                if source[i] == '\\':
                    result.append(source[i])
                    i += 1
                    if i < n:
                        result.append(source[i])
                        i += 1
                else:
                    result.append(source[i])
                    i += 1
            if i < n:
                result.append(source[i])  # closing quote
                i += 1

        # Template literals
        elif c == '`':
            result.append(c)
            i += 1
            while i < n and source[i] != '`':
                if source[i] == '\\':
                    result.append(source[i])
                    i += 1
                    if i < n:
                        result.append(source[i])
                        i += 1
                elif source[i] == '$' and i + 1 < n and source[i + 1] == '{':
                    # Template expression - just pass through characters
                    # (nested templates are rare in test262)
                    result.append(source[i])
                    i += 1
                else:
                    result.append(source[i])
                    i += 1
            if i < n:
                result.append(source[i])  # closing backtick
                i += 1

        # Potential comment or regex
        elif c == '/' and i + 1 < n:
            next_c = source[i + 1]
            ls = last_significant()

            # If preceded by a token that implies regex context, treat as regex literal
            # Also: start of file/line (ls == '') means regex context
            is_regex_ctx = (ls in REGEX_BEFORE or ls == '' or ls == '\n')

            # Check for keyword-before-regex (return, typeof, void, etc.)
            # by looking at the preceding word in result
            if not is_regex_ctx:
                # Check if last word is a keyword that precedes regex
                tail = ''.join(result[-20:]).rstrip()
                for kw in ('return', 'typeof', 'void', 'delete', 'throw',
                           'new', 'in', 'case', 'instanceof', 'yield', 'await'):
                    if tail.endswith(kw):
                        # Verify it's a complete keyword (not part of identifier)
                        kw_start = len(tail) - len(kw)
                        if kw_start == 0 or not (tail[kw_start - 1].isalnum() or tail[kw_start - 1] == '_'):
                            is_regex_ctx = True
                            break

            if is_regex_ctx and next_c not in ('/', '*'):
                # This is a regex literal — copy everything up to unescaped /
                result.append(c)
                i += 1
                in_class = False  # inside [...]
                while i < n and source[i] not in LINE_TERMINATORS:
                    rc = source[i]
                    if rc == '\\':
                        result.append(rc)
                        i += 1
                        if i < n:
                            result.append(source[i])
                            i += 1
                        continue
                    if rc == '[':
                        in_class = True
                    elif rc == ']':
                        in_class = False
                    elif rc == '/' and not in_class:
                        result.append(rc)
                        i += 1
                        # consume regex flags
                        while i < n and source[i].isalpha():
                            result.append(source[i])
                            i += 1
                        break
                    result.append(rc)
                    i += 1

            elif is_regex_ctx and next_c == '/':
                # // is always a single-line comment in JS.
                i += 2
                while i < n and source[i] not in LINE_TERMINATORS:
                    i += 1

            elif is_regex_ctx and next_c == '*':
                # Could be regex starting with * — but that's invalid regex.
                # Check for frontmatter first.
                if source[i:i + 5] == '/*---':
                    end = source.find('---*/', i)
                    if end != -1:
                        result.append(source[i:end + 5])
                        i = end + 5
                    else:
                        result.append(c)
                        i += 1
                else:
                    # Block comment
                    i += 2
                    while i < n:
                        if source[i] == '*' and i + 1 < n and source[i + 1] == '/':
                            i += 2
                            break
                        if source[i] == '\n':
                            result.append('\n')
                        i += 1

            elif next_c == '/':
                # // is always a single-line comment in JS.
                i += 2
                while i < n and source[i] not in LINE_TERMINATORS:
                    i += 1

            elif next_c == '*':
                # Check for YAML frontmatter /*--- ... ---*/
                if source[i:i + 5] == '/*---':
                    end = source.find('---*/', i)
                    if end != -1:
                        result.append(source[i:end + 5])
                        i = end + 5
                    else:
                        result.append(c)
                        i += 1
                else:
                    # Regular block comment — skip
                    i += 2
                    while i < n:
                        if source[i] == '*' and i + 1 < n and source[i + 1] == '/':
                            i += 2
                            break
                        if source[i] == '\n':
                            result.append('\n')
                        i += 1

            else:
                # Division operator or regex literal — keep as-is
                result.append(c)
                i += 1
        else:
            result.append(c)
            i += 1

    return ''.join(result)


def collapse_blank_lines(text):
    """Collapse runs of 3+ blank lines down to 2."""
    lines = text.split('\n')
    out = []
    blank_count = 0
    for line in lines:
        if line.strip() == '':
            blank_count += 1
            if blank_count <= 2:
                out.append(line)
        else:
            blank_count = 0
            out.append(line)
    return '\n'.join(out)


def process_file(src_path, dst_path):
    """Strip comments from a single JS file. Returns (src_size, dst_size)."""
    try:
        with open(src_path, 'r', encoding='utf-8', errors='replace') as f:
            source = f.read()
    except Exception as e:
        return (0, 0, str(e))

    src_size = len(source)
    stripped = strip_js_comments(source)
    stripped = collapse_blank_lines(stripped)
    dst_size = len(stripped)

    os.makedirs(os.path.dirname(dst_path), exist_ok=True)
    with open(dst_path, 'w', encoding='utf-8') as f:
        f.write(stripped)

    return (src_size, dst_size, None)


def collect_files(src_dir, dst_dir):
    """Collect all .js files to process."""
    pairs = []
    for root, dirs, files in os.walk(src_dir):
        dirs.sort()
        for fname in sorted(files):
            if fname.endswith('.js'):
                src_path = os.path.join(root, fname)
                rel = os.path.relpath(src_path, src_dir)
                dst_path = os.path.join(dst_dir, rel)
                pairs.append((src_path, dst_path))
    return pairs


def main():
    if not os.path.isdir(SRC_ROOT):
        print(f"Error: {SRC_ROOT} not found. Run from project root.", file=sys.stderr)
        sys.exit(1)

    start_time = time.time()

    # Copy harness files as-is
    for copy_dir in COPY_DIRS:
        src = os.path.join(SRC_ROOT, copy_dir)
        dst = os.path.join(DST_ROOT, copy_dir)
        if os.path.isdir(src):
            if os.path.exists(dst):
                shutil.rmtree(dst)
            shutil.copytree(src, dst)
            print(f"Copied {copy_dir}/ as-is")

    # Collect test files to strip
    all_pairs = []
    for test_dir in TEST_DIRS:
        src = os.path.join(SRC_ROOT, test_dir)
        dst = os.path.join(DST_ROOT, test_dir)
        if os.path.isdir(src):
            pairs = collect_files(src, dst)
            all_pairs.extend(pairs)
            print(f"Found {len(pairs)} .js files in {test_dir}/")

    if not all_pairs:
        print("No files to process.", file=sys.stderr)
        sys.exit(1)

    # Process files in parallel
    total_src = 0
    total_dst = 0
    errors = 0
    num_workers = min(os.cpu_count() or 4, 16)

    print(f"Stripping comments from {len(all_pairs)} files using {num_workers} workers...")

    with ProcessPoolExecutor(max_workers=num_workers) as executor:
        futures = {
            executor.submit(process_file, src, dst): (src, dst)
            for src, dst in all_pairs
        }
        done = 0
        for future in as_completed(futures):
            done += 1
            src_size, dst_size, err = future.result()
            if err:
                errors += 1
                src_path, _ = futures[future]
                print(f"  Error: {src_path}: {err}", file=sys.stderr)
            else:
                total_src += src_size
                total_dst += dst_size
            if done % 5000 == 0:
                print(f"  ... {done}/{len(all_pairs)} files processed")

    elapsed = time.time() - start_time

    print(f"\nDone in {elapsed:.1f}s")
    print(f"  Files:    {len(all_pairs)} ({errors} errors)")
    print(f"  Original: {total_src / 1024 / 1024:.1f} MB")
    print(f"  Stripped: {total_dst / 1024 / 1024:.1f} MB")
    print(f"  Saved:    {(total_src - total_dst) / 1024 / 1024:.1f} MB ({100 * (total_src - total_dst) / total_src:.1f}%)")
    print(f"  Output:   {DST_ROOT}/")


if __name__ == "__main__":
    main()
