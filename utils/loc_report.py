#!/usr/bin/env python3
"""
loc_report.py — C/C++ lines-of-code report for the Lambda repo.

Counts *effective* source lines: comments, blank lines, and pure logging
statements (log_debug/log_info/... and printf-family debug calls) are stripped
before counting. Multi-line log calls are consumed as a unit.

Usage:
    ./utils/loc_report.py                # full report
    ./utils/loc_report.py lambda/py lib  # count only the given sub-dirs (repo-relative)
    ./utils/loc_report.py --raw          # also show raw physical line counts
    ./utils/loc_report.py --json         # machine-readable output
    ./utils/loc_report.py --unclassified # list files that fell into "other"

Module taxonomy lives in MODULES below — edit there to reshape the report.
"""

import argparse
import json
import os
import re
import sys
from collections import OrderedDict

REPO_ROOT = os.path.dirname(os.path.dirname(os.path.abspath(__file__)))

SOURCE_EXTS = ('.c', '.h', '.cc', '.cpp', '.hpp', '.cxx', '.hh', '.inc', '.m', '.mm')

# directories never counted (build output, deps, generated caches)
SKIP_DIRS = {
    'build', 'build_temp', 'node_modules', 'mac-deps', 'temp', 'release',
    '.git', 'ref', 'site', 'vibe', 'doc', 'data', 'patches', 'include',
}

# logging / debug-print calls that count as noise rather than logic
LOG_CALL = re.compile(
    r'^\s*(?:'
    r'log_debug|log_info|log_warn|log_warning|log_error|log_trace|log_fatal|log_notice'
    r'|printf|fprintf|vfprintf|puts|fputs|perror'
    r'|std::cout|std::cerr|std::clog'
    r'|DEBUG_PRINT|LOG_DEBUG|LOG_INFO|LOG_ERROR|TRACE'
    r')\s*(?:\(|<<)'
)


def strip_comments(text):
    """Remove // and /* */ comments, honouring string and char literals."""
    out = []
    i, n = 0, len(text)
    while i < n:
        c = text[i]
        if c == '"' or c == "'":
            quote = c
            out.append(c)
            i += 1
            while i < n:
                out.append(text[i])
                if text[i] == '\\':
                    if i + 1 < n:
                        out.append(text[i + 1])
                        i += 2
                        continue
                elif text[i] == quote:
                    i += 1
                    break
                i += 1
            continue
        if c == '/' and i + 1 < n:
            if text[i + 1] == '/':
                while i < n and text[i] != '\n':
                    i += 1
                continue
            if text[i + 1] == '*':
                i += 2
                while i + 1 < n and not (text[i] == '*' and text[i + 1] == '/'):
                    # keep newlines so line structure survives
                    if text[i] == '\n':
                        out.append('\n')
                    i += 1
                i = min(i + 2, n)
                continue
        out.append(c)
        i += 1
    return ''.join(out)


def count_effective_lines(path):
    """Return (effective, raw) line counts for one source file."""
    try:
        with open(path, 'r', encoding='utf-8', errors='replace') as f:
            text = f.read()
    except OSError:
        return 0, 0

    raw = text.count('\n') + (1 if text and not text.endswith('\n') else 0)
    lines = strip_comments(text).split('\n')

    effective = 0
    i = 0
    while i < len(lines):
        line = lines[i].strip()
        if not line:
            i += 1
            continue
        if LOG_CALL.match(line):
            # consume the whole call, however many lines it spans
            depth = 0
            seen_open = False
            while i < len(lines):
                for ch in lines[i]:
                    if ch == '(':
                        depth += 1
                        seen_open = True
                    elif ch == ')':
                        depth -= 1
                i += 1
                if seen_open and depth <= 0:
                    break
                if not seen_open and lines[i - 1].rstrip().endswith(';'):
                    break  # stream-style `std::cout << x;`
            continue
        effective += 1
        i += 1
    return effective, raw


# ---------------------------------------------------------------------------
# module taxonomy
#
# Each entry: (label, matcher). A matcher is checked against the repo-relative
# path. First match wins, so order matters. `dirs` matches a path prefix;
# `files` matches basename prefixes within `under`.
# ---------------------------------------------------------------------------

def under(prefix):
    return lambda p: p == prefix or p.startswith(prefix + '/')


def in_dir_with_prefix(directory, prefixes):
    def match(p):
        d, base = os.path.split(p)
        return d == directory and base.startswith(tuple(prefixes))
    return match


def in_dir(directory):
    return lambda p: os.path.dirname(p) == directory


def any_of(*matchers):
    return lambda p: any(m(p) for m in matchers)


# tree-sitter: vendored runtime vs. generated language parsers
TREE_SITTER = [
    ('tree-sitter runtime (vendored)', under('lambda/tree-sitter')),
    ('generated language parsers', lambda p: p.startswith('lambda/tree-sitter-')),
]

JS_SUBMODULES = [
    ('js dom / web platform', in_dir_with_prefix('lambda/js', [
        'js_dom', 'js_cssom', 'js_canvas', 'js_clipboard', 'js_history',
        'js_xhr', 'js_fetch', 'js_formdata', 'js_url_module',
    ])),
    ('node compat', in_dir_with_prefix('lambda/js', [
        'js_fs', 'js_path', 'js_os', 'js_http', 'js_https', 'js_net', 'js_dns',
        'js_tls', 'js_child_process', 'js_stream', 'js_buffer', 'js_util',
        'js_querystring', 'js_readline', 'js_string_decoder', 'js_crypto',
        'js_events', 'js_assert', 'js_zlib', 'js_permission',
    ])),
    ('core js engine', under('lambda/js')),
]

RADIANT_SUBMODULES = [
    ('layout', in_dir_with_prefix('radiant', [
        'layout', 'grid_', 'block_context', 'intrinsic_sizing', 'view_',
        'resolve_css_style', 'resolve_htm_style', 'css_prop_table',
        'stacking_order',
    ])),
    ('render', in_dir_with_prefix('radiant', [
        'render', 'display_list', 'paint_ir', 'retained_display_list',
        'surface', 'tile_pool', 'font', 'glyph_sampling', 'text_encoding',
        'image_surface_generation', 'rdt_vector', 'rdt_video', 'gif_player',
        'lottie_player', 'animation', 'css_animation', 'frame_clock',
    ])),
    ('event / interaction', in_dir_with_prefix('radiant', [
        'event', 'editing', 'text_edit', 'text_control', 'clipboard',
        'context_menu', 'ime_', 'scroller', 'state_machine', 'state_store',
        'state_schema', 'dom_range',
    ])),
    ('shell / dom / misc', under('radiant')),
]

LIB_SUBMODULES = [
    ('sqlite (vendored)', under('lib/sqlite')),
    ('woff2 (vendored)', under('lib/font/woff2')),
    ('font engine', under('lib/font')),
    ('gc heap', under('lib/gc')),
    ('core utilities', under('lib')),
]

# buckets that are third-party or machine-generated, reported separately from
# the hand-written total
VENDORED = {
    'sqlite (vendored)', 'woff2 (vendored)',
    'tree-sitter runtime (vendored)', 'generated language parsers',
}

MODULES = OrderedDict([
    ('lib', LIB_SUBMODULES),
    ('lambda', [
        ('tree-sitter', TREE_SITTER),
        ('input parsers', [('input parsers', under('lambda/input'))]),
        ('output formatters', [('output formatters', under('lambda/format'))]),
        ('js runtime', JS_SUBMODULES),
        ('bash runtime', [('bash runtime', under('lambda/bash'))]),
        ('python runtime', [('python runtime', under('lambda/py'))]),
        ('ruby runtime', [('ruby runtime', under('lambda/rb'))]),
        ('typescript runtime', [('typescript runtime', under('lambda/ts'))]),
        ('lambda runtime', [('lambda runtime', under('lambda'))]),
    ]),
    ('radiant', RADIANT_SUBMODULES),
])


def classify(path):
    """Return (top, module, submodule) for a repo-relative path."""
    for top, groups in MODULES.items():
        if not path.startswith(top + '/') and path != top:
            continue
        if top == 'lambda':
            for module, subs in groups:
                for label, matcher in subs:
                    if matcher(path):
                        return top, module, label
        else:
            for label, matcher in groups:
                if matcher(path):
                    return top, label, label
        return top, 'other', 'other'
    return None


def walk_sources(root):
    for dirpath, dirnames, filenames in os.walk(root):
        dirnames[:] = [
            d for d in dirnames
            if d not in SKIP_DIRS and not d.startswith('.')
        ]
        for name in filenames:
            if name.endswith(SOURCE_EXTS):
                full = os.path.join(dirpath, name)
                yield os.path.relpath(full, root)


def main():
    ap = argparse.ArgumentParser(description=__doc__,
                                 formatter_class=argparse.RawDescriptionHelpFormatter)
    ap.add_argument('--raw', action='store_true', help='also show raw physical lines')
    ap.add_argument('--json', action='store_true', help='emit JSON')
    ap.add_argument('--unclassified', action='store_true',
                    help='list files that landed in an "other" bucket')
    ap.add_argument('paths', nargs='*', metavar='DIR',
                    help='restrict counting to these repo-relative sub-dirs '
                         '(e.g. lambda/py lib); default: whole repo')
    args = ap.parse_args()

    # normalize sub-dir filters to clean repo-relative prefixes
    prefixes = [os.path.normpath(p).lstrip('./') for p in args.paths]
    for p in prefixes:
        if not os.path.isdir(os.path.join(REPO_ROOT, p)):
            ap.error(f'not a directory in repo: {p}')

    def selected(rel):
        if not prefixes:
            return True
        return any(rel == p or rel.startswith(p + '/') for p in prefixes)

    # tree: top -> module -> submodule -> [files, eff, raw]
    tree = OrderedDict()
    others = []

    for rel in walk_sources(REPO_ROOT):
        if not selected(rel):
            continue
        hit = classify(rel)
        if hit is None:
            if not prefixes:
                continue
            # explicitly requested dir outside the module taxonomy (e.g. test/):
            # bucket by its top-level dir so the count is still reported
            top = rel.split('/', 1)[0]
            hit = (top, 'other', 'other')
        top, module, sub = hit
        eff, raw = count_effective_lines(os.path.join(REPO_ROOT, rel))
        bucket = tree.setdefault(top, OrderedDict()).setdefault(module, OrderedDict())
        entry = bucket.setdefault(sub, [0, 0, 0])
        entry[0] += 1
        entry[1] += eff
        entry[2] += raw
        if module == 'other' or sub.startswith('other'):
            others.append((rel, eff))

    if args.json:
        print(json.dumps(tree, indent=2))
        return

    def totals(node):
        f = e = r = 0
        for v in node.values():
            if isinstance(v, list):
                f, e, r = f + v[0], e + v[1], r + v[2]
            else:
                sf, se, sr = totals(v)
                f, e, r = f + sf, e + se, r + sr
        return f, e, r

    w = 42
    hdr = f"{'':{w}} {'files':>7} {'LOC':>10}"
    if args.raw:
        hdr += f" {'raw':>10}"
    print()
    print('C/C++ lines of code — comments, blanks and log lines excluded')
    print('=' * len(hdr))
    print(hdr)
    print('-' * len(hdr))

    def row(label, indent, f, e, r):
        line = f"{' ' * indent + label:{w}} {f:>7,} {e:>10,}"
        if args.raw:
            line += f" {r:>10,}"
        print(line)

    grand = [0, 0, 0]
    vend = [0, 0, 0]
    for top, modules in tree.items():
        tf, te, tr = totals(modules)
        print()
        row(f'./{top}', 0, tf, te, tr)
        grand = [grand[0] + tf, grand[1] + te, grand[2] + tr]
        for module, subs in modules.items():
            mf, me, mr = totals(subs)
            single = len(subs) == 1 and module in subs
            row(module + (' *' if module in VENDORED else ''), 2, mf, me, mr)
            if not single:
                for sub, (f, e, r) in subs.items():
                    row(sub + (' *' if sub in VENDORED else ''), 4, f, e, r)
            for sub, (f, e, r) in subs.items():
                if sub in VENDORED:
                    vend = [vend[0] + f, vend[1] + e, vend[2] + r]

    own = [grand[i] - vend[i] for i in range(3)]
    print()
    print('=' * len(hdr))
    row('TOTAL', 0, *grand)
    row('  vendored / generated (*)', 0, *vend)
    row('  hand-written', 0, *own)
    print()

    if args.unclassified and others:
        print('Files in "other" buckets:')
        for rel, eff in sorted(others, key=lambda x: -x[1]):
            print(f'  {eff:>7,}  {rel}')
        print()


if __name__ == '__main__':
    sys.exit(main())
