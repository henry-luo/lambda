#!/usr/bin/env python3
"""
HTML/CSS mutator for Radiant layout engine fuzzy testing.

Takes existing HTML test files and applies random mutations to CSS properties,
DOM structure, and attribute values to create adversarial variants.

Usage:
    python3 html_mutator.py <input.html> [--count N] [--output-dir DIR] [--seed SEED]
    python3 html_mutator.py --input-dir test/layout/data/baseline --count 5 --output-dir DIR
"""

import argparse
import os
import random
import re
import sys

# ---------------------------------------------------------------------------
# Mutation value pools
# ---------------------------------------------------------------------------

EXTREME_LENGTHS = [
    '0', '0px', '0.001px', '-1px', '-9999px', '99999px',
    'auto', '100%', '200%', '-50%',
    'min-content', 'max-content', 'fit-content',
    'calc(100% - 99999px)', 'calc(0 / 1)',
]

DISPLAY_VALUES = [
    'block', 'inline', 'inline-block', 'flex', 'inline-flex',
    'grid', 'inline-grid', 'table', 'table-row', 'table-cell',
    'none', 'list-item', 'flow-root', 'contents',
]

POSITION_VALUES = ['static', 'relative', 'absolute', 'fixed', 'sticky']
OVERFLOW_VALUES = ['visible', 'hidden', 'scroll', 'auto']
FLOAT_VALUES = ['none', 'left', 'right']
BOX_SIZING = ['content-box', 'border-box']

FLEX_VALUES = ['0 1 auto', '1 0 0', '1 1 0%', '0 0 auto', '1', '0', 'none',
               '9999 0 0', '0 9999 auto', '1 1 100%', '0.001 0.001 auto']

GRID_TEMPLATES = [
    'none', '1fr', 'auto', 'repeat(2, 1fr)', 'repeat(99, 1fr)',
    'repeat(auto-fill, 100px)', 'repeat(auto-fit, minmax(0, 1fr))',
    'minmax(0, 1fr)', 'minmax(auto, auto)',
]

# CSS properties and their mutation pools
CSS_MUTATIONS = {
    'width': EXTREME_LENGTHS,
    'height': EXTREME_LENGTHS,
    'min-width': EXTREME_LENGTHS,
    'min-height': EXTREME_LENGTHS,
    'max-width': EXTREME_LENGTHS,
    'max-height': EXTREME_LENGTHS,
    'margin': [f'{v}' for v in ['0', 'auto', '-10px', '50px', '-9999px', '99999px', '50%', '-50%']],
    'margin-top': ['0', '-10px', '50px', '-9999px', 'auto'],
    'margin-bottom': ['0', '-10px', '50px', '-9999px', 'auto'],
    'margin-left': ['0', '-10px', '50px', '-9999px', 'auto'],
    'margin-right': ['0', '-10px', '50px', '-9999px', 'auto'],
    'padding': ['0', '1px', '10px', '50px', '100px', '50%'],
    'display': DISPLAY_VALUES,
    'position': POSITION_VALUES,
    'overflow': OVERFLOW_VALUES,
    'overflow-x': OVERFLOW_VALUES,
    'overflow-y': OVERFLOW_VALUES,
    'float': FLOAT_VALUES,
    'clear': ['none', 'left', 'right', 'both'],
    'box-sizing': BOX_SIZING,
    'top': EXTREME_LENGTHS,
    'left': EXTREME_LENGTHS,
    'right': EXTREME_LENGTHS,
    'bottom': EXTREME_LENGTHS,
    'flex': FLEX_VALUES,
    'flex-grow': ['0', '1', '0.001', '9999'],
    'flex-shrink': ['0', '1', '0.001', '9999'],
    'flex-basis': ['auto', '0', '0%', '100%', '200%', 'min-content', 'max-content'],
    'flex-direction': ['row', 'row-reverse', 'column', 'column-reverse'],
    'flex-wrap': ['nowrap', 'wrap', 'wrap-reverse'],
    'align-items': ['flex-start', 'flex-end', 'center', 'stretch', 'baseline'],
    'justify-content': ['flex-start', 'flex-end', 'center', 'space-between', 'space-around'],
    'align-self': ['auto', 'flex-start', 'flex-end', 'center', 'stretch', 'baseline'],
    'grid-template-columns': GRID_TEMPLATES,
    'grid-template-rows': GRID_TEMPLATES,
    'gap': ['0', '1px', '10px', '50px', '100px'],
    'font-size': ['0', '0.1px', '1px', '100px', '9999px'],
    'line-height': ['0', '0.5', '1', 'normal', '9999px'],
    'text-indent': ['0', '-9999px', '9999px', '50%', '-50%'],
    'white-space': ['normal', 'nowrap', 'pre', 'pre-wrap', 'pre-line'],
    'word-break': ['normal', 'break-all', 'keep-all'],
    'vertical-align': ['baseline', 'top', 'middle', 'bottom', '50px', '-50px'],
    'border-collapse': ['collapse', 'separate'],
    'table-layout': ['auto', 'fixed'],
    'z-index': ['-999999', '0', '999999', 'auto'],
    'opacity': ['0', '0.5', '1', '-1', '2'],
    'direction': ['ltr', 'rtl'],
}

# ---------------------------------------------------------------------------
# Mutation operations
# ---------------------------------------------------------------------------

def mutate_css_value(html, num_mutations=None):
    """Find CSS property declarations and randomly change their values."""
    if num_mutations is None:
        num_mutations = random.randint(1, 5)

    # Match property: value in style attributes and <style> blocks
    prop_pattern = re.compile(
        r'((?:' + '|'.join(re.escape(p) for p in CSS_MUTATIONS.keys()) + r'))\s*:\s*([^;}"]+)',
        re.IGNORECASE
    )

    matches = list(prop_pattern.finditer(html))
    if not matches:
        return html

    # Pick random matches to mutate
    to_mutate = random.sample(matches, min(num_mutations, len(matches)))
    # Process in reverse order to preserve positions
    to_mutate.sort(key=lambda m: m.start(), reverse=True)

    for match in to_mutate:
        prop = match.group(1).lower()
        pool = CSS_MUTATIONS.get(prop, EXTREME_LENGTHS)
        new_val = random.choice(pool)
        html = html[:match.start(2)] + new_val + html[match.end(2):]

    return html


def inject_css_property(html, num_injections=None):
    """Inject new CSS properties into existing style attributes."""
    if num_injections is None:
        num_injections = random.randint(1, 3)

    # Find style="..." attributes
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html

    to_inject = random.sample(matches, min(num_injections, len(matches)))
    to_inject.sort(key=lambda m: m.start(), reverse=True)

    for match in to_inject:
        existing = match.group(1)
        # Pick a random property to inject
        prop = random.choice(list(CSS_MUTATIONS.keys()))
        val = random.choice(CSS_MUTATIONS[prop])
        new_style = f'{existing}; {prop}: {val}'
        html = html[:match.start(1)] + new_style + html[match.end(1):]

    return html


def mutate_display_mode(html):
    """Change display properties to create layout mode collisions."""
    prop_pattern = re.compile(r'display\s*:\s*([^;}"]+)', re.IGNORECASE)
    matches = list(prop_pattern.finditer(html))
    if not matches:
        # Inject display into first style attribute
        style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
        m = style_pattern.search(html)
        if m:
            new_display = random.choice(DISPLAY_VALUES)
            html = html[:m.start(1)] + f'display: {new_display}; ' + m.group(1) + html[m.end(1):]
        return html

    match = random.choice(matches)
    new_display = random.choice(DISPLAY_VALUES)
    html = html[:match.start(1)] + new_display + html[match.end(1):]
    return html


def swap_elements(html):
    """Swap two sibling elements in the DOM."""
    # Find matching tags at the same level - simplified heuristic
    tag_pattern = re.compile(r'(<(?:div|span|p|section|article)[^>]*>.*?</(?:div|span|p|section|article)>)',
                             re.DOTALL | re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if len(matches) < 2:
        return html

    i, j = random.sample(range(len(matches)), 2)
    if i > j:
        i, j = j, i

    m1 = matches[i]
    m2 = matches[j]

    # Only swap if they don't overlap
    if m1.end() <= m2.start():
        html = (html[:m1.start()] + m2.group() +
                html[m1.end():m2.start()] + m1.group() +
                html[m2.end():])
    return html


def duplicate_element(html):
    """Duplicate a random element."""
    tag_pattern = re.compile(r'(<(?:div|span|p|td|tr|li)[^>]*>.*?</(?:div|span|p|td|tr|li)>)',
                             re.DOTALL | re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html

    match = random.choice(matches)
    count = random.choice([2, 3, 5, 10, 50])
    duplicates = match.group() * count
    html = html[:match.start()] + duplicates + html[match.end():]
    return html


def remove_element(html):
    """Remove a random element from the DOM."""
    tag_pattern = re.compile(r'<(div|span|p|section|td|tr)[^>]*>.*?</\1>',
                             re.DOTALL | re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html

    match = random.choice(matches)
    html = html[:match.start()] + html[match.end():]
    return html


def inject_deep_nesting(html):
    """Inject deeply nested elements into the document."""
    depth = random.choice([20, 50, 100, 200])
    nested = 'deep content'
    for _ in range(depth):
        nested = f'<div>{nested}</div>'

    # Insert after <body>
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + nested + html[end_tag + 1:]
    return html


def mutate_colspan_rowspan(html):
    """Mutate colspan/rowspan attributes to extreme values."""
    attr_pattern = re.compile(r'(colspan|rowspan)="(\d+)"', re.IGNORECASE)
    matches = list(attr_pattern.finditer(html))
    if not matches:
        return html

    match = random.choice(matches)
    new_val = random.choice([0, 1, 2, 5, 100, 9999])
    html = html[:match.start(2)] + str(new_val) + html[match.end(2):]
    return html


def inject_orphan_table_parts(html):
    """Inject table parts (tr, td) without proper table parent."""
    orphans = [
        '<tr><td>orphan cell</td></tr>',
        '<td>bare cell</td>',
        '<th>bare header</th>',
        '<tbody><tr><td>orphan tbody</td></tr></tbody>',
        '<caption>orphan caption</caption>',
    ]
    orphan = random.choice(orphans)

    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + orphan + html[end_tag + 1:]
    return html


def inject_large_text(html):
    """Inject a very large text node."""
    word = random.choice(['word', 'Unbreakablelongword', 'a', '你好'])
    count = random.choice([1000, 5000, 10000])
    sep = random.choice([' ', '', '\n'])
    text = sep.join([word] * count)

    tag_pattern = re.compile(r'>([\s\S]*?)</', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        match = random.choice(matches)
        html = html[:match.start(1)] + text + html[match.end(1):]
    return html


def strip_all_styles(html):
    """Remove all style attributes to test default layout."""
    return re.sub(r'\s*style="[^"]*"', '', html, flags=re.IGNORECASE)


def randomize_all_widths(html):
    """Replace all width values with extreme values."""
    def replace_width(m):
        return f'width: {random.choice(EXTREME_LENGTHS)}'
    return re.sub(r'width\s*:\s*[^;}"]+', replace_width, html, flags=re.IGNORECASE)


# ---------------------------------------------------------------------------
# Mutation pipeline
# ---------------------------------------------------------------------------

MUTATIONS = [
    ('css_value', mutate_css_value, 3),
    ('inject_css', inject_css_property, 2),
    ('display_mode', mutate_display_mode, 2),
    ('swap_elements', swap_elements, 1),
    ('duplicate_element', duplicate_element, 1),
    ('remove_element', remove_element, 1),
    ('deep_nesting', inject_deep_nesting, 1),
    ('colspan_rowspan', mutate_colspan_rowspan, 1),
    ('orphan_table', inject_orphan_table_parts, 1),
    ('large_text', inject_large_text, 1),
    ('strip_styles', strip_all_styles, 1),
    ('randomize_widths', randomize_all_widths, 1),
]


def mutate(html, num_mutations=None):
    """Apply a random combination of mutations to an HTML string."""
    if num_mutations is None:
        num_mutations = random.randint(1, 4)

    # Build weighted list
    weighted = []
    for name, fn, weight in MUTATIONS:
        weighted.extend([(name, fn)] * weight)

    applied = []
    for _ in range(num_mutations):
        name, fn = random.choice(weighted)
        try:
            html = fn(html)
            applied.append(name)
        except Exception:
            pass  # skip failed mutations

    return html, applied


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='Mutate HTML files for Radiant layout fuzzing')
    parser.add_argument('input', nargs='?', help='Input HTML file')
    parser.add_argument('--input-dir', '-d', help='Directory of HTML files to mutate')
    parser.add_argument('--count', '-n', type=int, default=5,
                        help='Mutations per input file (default: 5)')
    parser.add_argument('--output-dir', '-o', required=True,
                        help='Output directory for mutated files')
    parser.add_argument('--seed', '-s', type=int, default=None,
                        help='Random seed for reproducibility')
    parser.add_argument('--max-input-size', type=int, default=100000,
                        help='Skip input files larger than this (bytes, default: 100000)')
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    os.makedirs(args.output_dir, exist_ok=True)

    # Collect input files
    input_files = []
    if args.input:
        input_files.append(args.input)
    elif args.input_dir:
        for root, _, files in os.walk(args.input_dir):
            for f in files:
                if f.endswith('.html') or f.endswith('.htm'):
                    input_files.append(os.path.join(root, f))
    else:
        print('Error: provide an input file or --input-dir', file=sys.stderr)
        sys.exit(1)

    if not input_files:
        print('No HTML files found', file=sys.stderr)
        sys.exit(1)

    total = 0
    for fpath in input_files:
        try:
            size = os.path.getsize(fpath)
            if size > args.max_input_size:
                continue

            with open(fpath, 'r', errors='replace') as f:
                original = f.read()
        except (OSError, UnicodeDecodeError):
            continue

        base = os.path.splitext(os.path.basename(fpath))[0]
        for i in range(args.count):
            mutated, applied = mutate(original)
            out_name = f'mut_{base}_{i:03d}.html'
            out_path = os.path.join(args.output_dir, out_name)
            with open(out_path, 'w') as f:
                f.write(f'<!-- mutations: {", ".join(applied)} -->\n')
                f.write(mutated)
            total += 1

    print(f'Generated {total} mutated files from {len(input_files)} inputs in {args.output_dir}')


if __name__ == '__main__':
    main()
