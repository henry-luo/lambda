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
    '0', '0px', '0.001px', '0.0001px', '-1px', '-9999px', '-99999px',
    '99999px', '999999px', '1e7px', '1e10px',
    'auto', 'inherit', 'initial', 'unset',
    '100%', '200%', '999%', '-50%', '-100%', '-999%',
    'min-content', 'max-content', 'fit-content', 'fit-content(0)',
    'calc(100% - 99999px)', 'calc(0 / 1)', 'calc(100% * 999)',
    'calc(1px + 1em + 1%)', 'calc(100% / 0.001)',
    'min(0px, 100%)', 'max(99999px, 100%)', 'clamp(0px, 50%, 100px)',
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
    'repeat(999, 1fr)', 'repeat(auto-fill, minmax(0, 1fr))',
    '0fr 1fr 0fr', '0.001fr 999fr',
    'minmax(0, 0)', 'minmax(99999px, 0)',
    'fit-content(100px) 1fr fit-content(200px)',
    'subgrid',
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
    'writing-mode': ['horizontal-tb', 'vertical-rl', 'vertical-lr'],
    'aspect-ratio': ['1', '16/9', '0.001', '9999', '1/0', '0/1', 'auto'],
    'transform': ['none', 'scale(0)', 'scale(-1)', 'scale(9999)', 'translate(99999px)',
                  'rotate(90deg)', 'skew(89deg)'],
    'column-count': ['1', '2', '5', '100', '9999'],
    'column-width': ['0', '1px', '99999px', 'auto'],
    'word-wrap': ['normal', 'break-word', 'anywhere'],
    'overflow-wrap': ['normal', 'break-word', 'anywhere'],
    'text-overflow': ['clip', 'ellipsis'],
    'letter-spacing': ['0', '-5px', '50px', 'normal'],
    'word-spacing': ['0', '-5px', '50px', 'normal'],
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


def wrap_in_layout_container(html):
    """Wrap random elements in a new flex/grid container."""
    containers = [
        '<div style="display:flex;flex-wrap:wrap;">',
        '<div style="display:grid;grid-template-columns:repeat(auto-fill,minmax(0,1fr));">',
        '<div style="display:inline-flex;">',
        '<div style="display:inline-grid;">',
        '<div style="display:flex;flex-direction:column;height:0;">',
        '<div style="display:grid;grid-template-rows:repeat(99,1fr);">',
        '<div style="display:flex;overflow:hidden;width:0;height:0;">',
    ]
    tag_pattern = re.compile(r'(<(div|section|article|span|p)[^>]*>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    match = random.choice(matches)
    wrapper = random.choice(containers)
    html = html[:match.start()] + wrapper + html[match.start():]
    # find a closing tag to insert </div>
    close_pos = html.find('</', match.end() + len(wrapper))
    if close_pos >= 0:
        next_close = html.find('>', close_pos)
        if next_close >= 0:
            html = html[:next_close + 1] + '</div>' + html[next_close + 1:]
    return html


def inject_contradictory_styles(html):
    """Inject contradictory CSS properties on the same element."""
    contradictions = [
        'display:none;display:flex;display:block;',
        'position:absolute;position:relative;position:fixed;',
        'width:0;width:100%;width:auto;',
        'height:99999px;height:0;height:auto;min-height:99999px;max-height:0;',
        'float:left;float:right;clear:both;',
        'overflow:hidden;overflow:visible;overflow:scroll;',
        'visibility:hidden;visibility:visible;visibility:collapse;',
        'flex-grow:999;flex-shrink:999;flex-basis:0;width:99999px;',
        'min-width:99999px;max-width:0;width:50%;',
        'min-height:99999px;max-height:0;height:50%;',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    match = random.choice(matches)
    extra = random.choice(contradictions)
    html = html[:match.end(1)] + ';' + extra + html[match.end(1):]
    return html


def inject_multi_css_bomb(html):
    """Inject many CSS properties at once on a random element."""
    props = [
        'margin:-9999px', 'padding:9999px', 'border:999px solid red',
        'box-sizing:border-box', 'outline:999px solid blue',
        'position:absolute', 'top:-9999px', 'left:-9999px',
        'width:calc(100% + 99999px)', 'height:calc(100vh * 999)',
        'transform:scale(0.001) rotate(720deg)',
        'font-size:0.001px', 'line-height:99999',
        'flex:999 999 0', 'order:-99999',
        'z-index:2147483647', 'opacity:0',
        'column-count:999', 'column-gap:99999px',
        'text-indent:-99999px', 'letter-spacing:999px',
        'word-spacing:999px', 'white-space:pre',
    ]
    num_props = random.randint(5, 15)
    chosen = random.sample(props, min(num_props, len(props)))
    style_str = ';'.join(chosen)

    tag_pattern = re.compile(r'<(div|span|p|section|article|table|ul|ol)([^>]*?)(/?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    match = random.choice(matches)
    attrs = match.group(2)
    if 'style="' in attrs.lower():
        # Append to existing style
        old_style_match = re.search(r'style="([^"]*)"', attrs, re.IGNORECASE)
        if old_style_match:
            new_attrs = attrs[:old_style_match.end(1)] + ';' + style_str + attrs[old_style_match.end(1):]
            html = html[:match.start(2)] + new_attrs + html[match.end(2):]
    else:
        html = html[:match.end(2)] + f' style="{style_str}"' + html[match.end(2):]
    return html


def change_all_display_modes(html):
    """Cascade display mode changes across multiple elements."""
    mode = random.choice(['flex', 'grid', 'inline', 'inline-flex', 'inline-grid',
                          'table', 'table-row', 'table-cell', 'none', 'contents',
                          'inline-block', 'block', 'flow-root'])
    def replace_display(m):
        return f'display:{mode}'
    return re.sub(r'display\s*:\s*[^;}"]+', replace_display, html, flags=re.IGNORECASE)


def fragment_dom(html):
    """Randomly split an element's content and move part of it elsewhere."""
    tag_pattern = re.compile(r'(<(div|span|p)[^>]*>)([\s\S]*?)(</\2>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if len(matches) < 2:
        return html
    src = random.choice(matches)
    dst = random.choice(matches)
    if src == dst:
        return html
    content = src.group(3)
    if len(content) < 2:
        return html
    mid = len(content) // 2
    fragment = content[mid:]
    # Remove fragment from source
    html = html[:src.start(3) + mid] + html[src.end(3):]
    # Re-find destination (offset may have shifted)
    tag_pattern2 = re.compile(r'(<(div|span|p)[^>]*>)', re.IGNORECASE)
    matches2 = list(tag_pattern2.finditer(html))
    if matches2:
        insert_match = random.choice(matches2)
        html = html[:insert_match.end()] + fragment + html[insert_match.end():]
    return html


def inject_extreme_inline_styles(html):
    """Add style attributes with extreme values to elements without styles."""
    extreme_styles = [
        'width:0;height:0;overflow:visible;',
        'position:fixed;top:0;left:0;width:100%;height:100%;',
        'font-size:999px;line-height:0;',
        'margin:-99999px;padding:99999px;',
        'transform:translateX(-99999px);',
        'display:flex;flex-wrap:wrap;gap:99999px;',
        'writing-mode:vertical-rl;text-orientation:mixed;direction:rtl;',
        'columns:999;column-gap:0;',
        'aspect-ratio:0.001;width:100%;',
        'float:left;clear:right;width:200%;margin-left:-100%;',
    ]
    tag_pattern = re.compile(r'<(div|span|p|section|article|h[1-6]|li|td|th)(\s[^>]*)?\s*>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    # Pick several elements to modify
    count = random.randint(1, min(5, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        if 'style=' not in match.group().lower():
            style = random.choice(extreme_styles)
            insert_pos = match.end() - 1 + offset  # before '>'
            insertion = f' style="{style}"'
            html = html[:insert_pos] + insertion + html[insert_pos:]
            offset += len(insertion)
    return html


def inject_zero_size_containers(html):
    """Insert zero-sized containers with overflowing content."""
    containers = [
        '<div style="width:0;height:0;overflow:visible;"><span>overflow text that should not be clipped</span></div>',
        '<div style="width:0;height:0;display:flex;"><div style="width:999px;height:999px;">large child</div></div>',
        '<div style="max-width:0;max-height:0;"><table><tr><td>table in zero box</td></tr></table></div>',
        '<div style="width:0;height:0;position:relative;"><div style="position:absolute;width:100%;height:100%;">abs in zero</div></div>',
    ]
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + random.choice(containers) + html[end_tag + 1:]
    return html


def inject_replaced_element_stress(html):
    """Inject images and replaced elements with extreme dimensions."""
    elements = [
        '<img style="width:99999px;height:0;">',
        '<img style="width:0;height:99999px;aspect-ratio:1;">',
        '<iframe style="width:100%;height:99999px;"></iframe>',
        '<svg style="width:0;height:0;"><rect width="99999" height="99999"/></svg>',
        '<canvas width="99999" height="99999" style="display:block;"></canvas>',
        '<img style="width:calc(100% + 99999px);max-width:0;">',
    ]
    tag_pattern = re.compile(r'(</div>|</span>|</p>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        match = random.choice(matches)
        html = html[:match.start()] + random.choice(elements) + html[match.start():]
    return html


def inject_float_bomb(html):
    """Inject many floated elements to stress float clearing."""
    count = random.choice([20, 50, 100])
    floats = []
    for i in range(count):
        side = random.choice(['left', 'right'])
        w = random.choice(['10px', '50%', '33.33%', '25%', 'auto', '0'])
        floats.append(f'<div style="float:{side};width:{w};">F{i}</div>')
    bomb = ''.join(floats) + '<div style="clear:both;"></div>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + bomb + html[end_tag + 1:]
    return html


def randomize_all_positions(html):
    """Randomize position and offset values throughout the document."""
    positions = ['static', 'relative', 'absolute', 'fixed', 'sticky']
    def replace_position(m):
        return f'position:{random.choice(positions)}'
    html = re.sub(r'position\s*:\s*[^;}"]+', replace_position, html, flags=re.IGNORECASE)
    offsets = ['top', 'left', 'right', 'bottom']
    for off in offsets:
        def replace_offset(m, o=off):
            return f'{o}:{random.choice(EXTREME_LENGTHS)}'
        html = re.sub(rf'{off}\s*:\s*[^;}}"]+', replace_offset, html, flags=re.IGNORECASE)
    return html


def remove_closing_tags(html):
    """Remove random closing tags to create malformed HTML."""
    close_pattern = re.compile(r'</(?:div|span|p|section|article|table|tr|td|th|ul|ol|li)>',
                                re.IGNORECASE)
    matches = list(close_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(5, len(matches)))
    to_remove = random.sample(matches, count)
    to_remove.sort(key=lambda m: m.start(), reverse=True)
    for match in to_remove:
        html = html[:match.start()] + html[match.end():]
    return html


def inject_form_elements(html):
    """Inject form/replaced elements into the document."""
    forms = [
        '<input type="text" style="width:99999px;height:0;">',
        '<textarea style="display:flex;width:0;height:99999px;">text</textarea>',
        '<select style="display:grid;width:100%;"><option>A</option></select>',
        '<button style="display:inline-flex;position:absolute;">btn</button>',
        '<input type="checkbox" style="width:99999px;margin:-9999px;">',
        '<img src="data:," style="width:99999px;height:0;aspect-ratio:1;">',
        '<iframe style="width:100%;height:99999px;display:block;"></iframe>',
        '<svg style="width:0;height:0;display:block;overflow:visible;"><rect width="9999" height="9999"/></svg>',
    ]
    tag_pattern = re.compile(r'(</div>|</span>|</p>|</td>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(3, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        elem = random.choice(forms)
        pos = match.start() + offset
        html = html[:pos] + elem + html[pos:]
        offset += len(elem)
    return html


def randomize_to_percentage_heights(html):
    """Replace height values with percentages to stress %-resolution chains."""
    pct_values = ['0%', '50%', '100%', '200%', '999%', '-50%']
    def replace_height(m):
        return f'height:{random.choice(pct_values)}'
    return re.sub(r'height\s*:\s*[^;}"]+', replace_height, html, flags=re.IGNORECASE)


def inject_display_contents(html):
    """Inject display:contents on random elements."""
    tag_pattern = re.compile(r'<(div|span|section|article|p)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(5, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            old_style_match = re.search(r'style="([^"]*)"', attrs, re.IGNORECASE)
            if old_style_match:
                new_attrs = attrs[:old_style_match.end(1)] + ';display:contents' + attrs[old_style_match.end(1):]
                html = html[:match.start(2) + offset] + new_attrs + html[match.end(2) + offset:]
                offset += len(new_attrs) - len(attrs)
        else:
            insertion = ' style="display:contents"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def empty_all_text_nodes(html):
    """Remove all text content, leaving only empty elements."""
    return re.sub(r'>([^<]+)<', '><', html)


def duplicate_css_properties(html):
    """Duplicate CSS properties with different values to test cascade."""
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(3, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        existing = match.group(1)
        # extract first property and add it again with different value
        prop_match = re.search(r'([a-z-]+)\s*:\s*([^;]+)', existing, re.IGNORECASE)
        if prop_match:
            prop = prop_match.group(1).lower()
            pool = CSS_MUTATIONS.get(prop, EXTREME_LENGTHS)
            new_val = random.choice(pool)
            addition = f'; {prop}: {new_val}; {prop}: {random.choice(pool)}'
            pos = match.end(1) + offset
            html = html[:pos] + addition + html[pos:]
            offset += len(addition)
    return html


def inject_pseudo_elements(html):
    """Inject a <style> block with pseudo-element rules targeting existing elements."""
    pseudo_rules = []
    targets = ['div', 'span', 'p', 'td', 'th', 'li', 'section', 'article']
    count = random.randint(2, 6)
    for _ in range(count):
        tag = random.choice(targets)
        pseudo = random.choice(['::before', '::after'])
        content = random.choice(['""', '"X"', '" "', '"' + 'A' * random.choice([1, 100, 500]) + '"'])
        display = random.choice(['block', 'inline', 'inline-block', 'flex', 'none', 'table'])
        props = [f'content:{content}', f'display:{display}']
        if random.random() < 0.5:
            props.append(f'width:{random.choice(EXTREME_LENGTHS)}')
        if random.random() < 0.5:
            props.append(f'height:{random.choice(EXTREME_LENGTHS)}')
        if random.random() < 0.3:
            props.append(f'position:{random.choice(POSITION_VALUES)}')
        if random.random() < 0.3:
            props.append(f'float:{random.choice(["left", "right", "none"])}')
        pseudo_rules.append(f'{tag}{pseudo} {{ {"; ".join(props)} }}')

    style_block = f'<style>{chr(10).join(pseudo_rules)}</style>'

    # inject before </head> or after <body>
    head_pos = html.lower().find('</head>')
    if head_pos >= 0:
        html = html[:head_pos] + style_block + html[head_pos:]
    else:
        body_pos = html.lower().find('<body')
        if body_pos >= 0:
            end_tag = html.find('>', body_pos)
            if end_tag >= 0:
                html = html[:end_tag + 1] + style_block + html[end_tag + 1:]
    return html


def cascade_overflow_hidden(html):
    """Set overflow:hidden on many elements to stress clipping logic."""
    tag_pattern = re.compile(r'<(div|section|article|main|aside)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(8, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    overflow_val = random.choice(['hidden', 'clip', 'scroll', 'auto'])
    for match in sorted(chosen, key=lambda m: m.start()):
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            old_style_match = re.search(r'style="([^"]*)"', attrs, re.IGNORECASE)
            if old_style_match:
                addition = f';overflow:{overflow_val}'
                pos = match.start(2) + offset + old_style_match.end(1) - len(attrs) + len(match.group(2))
                # simpler: just append before closing quote
                insert_pos = match.start() + offset + match.group(0).rindex('"')
                html = html[:insert_pos] + addition + html[insert_pos:]
                offset += len(addition)
        else:
            insertion = f' style="overflow:{overflow_val}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_sticky_bomb(html):
    """Inject many sticky-positioned elements."""
    count = random.choice([10, 20, 50])
    stickies = []
    for i in range(count):
        top = random.choice(['0', '10px', '50px', '-10px', '100%'])
        stickies.append(f'<div style="position:sticky;top:{top};height:20px;">S{i}</div>')
    bomb = ''.join(stickies)
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + bomb + html[end_tag + 1:]
    return html


def inject_visibility_collapse(html):
    """Inject visibility:collapse on random table rows/elements."""
    # target table rows
    tr_pattern = re.compile(r'<tr([^>]*?)>', re.IGNORECASE)
    matches = list(tr_pattern.finditer(html))
    if matches:
        count = random.randint(1, min(3, len(matches)))
        chosen = random.sample(matches, count)
        offset = 0
        for match in sorted(chosen, key=lambda m: m.start()):
            attrs = match.group(1)
            if 'style="' in attrs.lower():
                old_style_match = re.search(r'style="([^"]*)"', attrs, re.IGNORECASE)
                if old_style_match:
                    addition = ';visibility:collapse'
                    pos = match.start() + offset + match.group(0).rindex('"')
                    html = html[:pos] + addition + html[pos:]
                    offset += len(addition)
            else:
                insertion = ' style="visibility:collapse"'
                pos = match.end(1) + offset
                html = html[:pos] + insertion + html[pos:]
                offset += len(insertion)
    return html


def inject_negative_margin_chain(html):
    """Create a chain of elements with collapsing negative margins."""
    count = random.choice([5, 10, 20, 50])
    elems = []
    for i in range(count):
        m = random.choice(['-10px', '-50px', '-200px', '-999px', '-9999px'])
        elems.append(f'<div style="margin-top:{m};height:20px;background:red;">NM{i}</div>')
    chain = ''.join(elems)
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + chain + html[end_tag + 1:]
    return html


def inject_absolute_in_inline(html):
    """Inject absolutely positioned elements inside inline contexts."""
    snippets = [
        '<span>text<div style="position:absolute;top:0;left:0;width:200px;height:200px;">abs in span</div>more text</span>',
        '<span style="display:inline;">inline<div style="position:absolute;width:100%;height:100%;">abs</div></span>',
        '<a href="#">link<div style="position:fixed;top:0;right:0;">fixed in anchor</div></a>',
        '<em>em<span style="position:absolute;left:-9999px;width:99999px;">abs in em</span></em>',
    ]
    tag_pattern = re.compile(r'(</div>|</p>|</span>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        match = random.choice(matches)
        html = html[:match.start()] + random.choice(snippets) + html[match.start():]
    return html


def randomize_box_sizing(html):
    """Randomly inject box-sizing changes throughout the document."""
    tag_pattern = re.compile(r'<(div|span|p|section|article|td|th|table)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(10, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        sizing = random.choice(['border-box', 'content-box'])
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';box-sizing:{sizing}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="box-sizing:{sizing}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_direction_bidi_mix(html):
    """Mix LTR/RTL directions and inject bidi override characters."""
    bidi_chars = ['\u200F', '\u200E', '\u202A', '\u202B', '\u202C', '\u202D', '\u202E',
                  '\u2066', '\u2067', '\u2068', '\u2069']
    # inject direction on random elements
    tag_pattern = re.compile(r'<(div|span|p|section|td)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        direction = random.choice(['rtl', 'ltr'])
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';direction:{direction}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="direction:{direction}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    # also inject bidi chars into text
    text_pattern = re.compile(r'>([^<]{2,})<', re.IGNORECASE)
    text_matches = list(text_pattern.finditer(html))
    if text_matches:
        tm = random.choice(text_matches)
        bidi = ''.join(random.choices(bidi_chars, k=random.randint(2, 8)))
        mid = (tm.start(1) + tm.end(1)) // 2
        html = html[:mid] + bidi + html[mid:]
    return html


def inject_writing_mode_stress(html):
    """Apply conflicting writing modes across nested elements."""
    modes = ['horizontal-tb', 'vertical-rl', 'vertical-lr']
    tag_pattern = re.compile(r'<(div|span|p|section|table)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        wm = random.choice(modes)
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';writing-mode:{wm}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="writing-mode:{wm}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def corrupt_style_syntax(html):
    """Introduce CSS syntax errors: missing semicolons, bad values, unclosed strings."""
    corruptions = [
        lambda s: s.replace(';', '', 1),              # remove a semicolon
        lambda s: s.replace(':', ' ', 1),              # remove colon
        lambda s: s + '{{{{',                          # stray braces
        lambda s: 'color:rgb(' + s,                    # unclosed function
        lambda s: s.replace('px', 'xp', 1),            # typo unit
        lambda s: s + '; --: ;',                        # empty custom property
        lambda s: s + '; width: ;',                     # empty value
        lambda s: s + '; display: table-footer-header;', # invalid value
        lambda s: s + '; @import "x";',                 # at-rule in style attr
        lambda s: s + '; !!!invalid!!!;',               # garbage
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        existing = match.group(1)
        corruption = random.choice(corruptions)
        corrupted = corruption(existing)
        start = match.start(1) + offset
        end = match.end(1) + offset
        html = html[:start] + corrupted + html[end:]
        offset += len(corrupted) - len(existing)
    return html


def inject_empty_table_stress(html):
    """Inject empty/minimal tables with extreme styling."""
    tables = [
        '<table style="width:0;height:0;table-layout:fixed;"><tr></tr></table>',
        '<table style="width:100%;border-collapse:collapse;"><tr><td></td><td></td></tr></table>',
        '<table style="width:99999px;table-layout:fixed;border-spacing:9999px;"><caption style="height:99999px;"></caption><tr><td style="width:0;"></td></tr></table>',
        '<table style="display:block;"><thead style="display:flex;"><tr><td>flex thead</td></tr></thead></table>',
        '<table style="width:0;"><colgroup><col span="999" style="width:100px;"></colgroup><tr><td>c</td></tr></table>',
        '<table><tr><td style="width:50%;padding:50%;">%pad</td><td style="width:50%;padding:50%;">%pad2</td></tr></table>',
        '<table style="table-layout:fixed;width:1px;"><tr><td style="width:99999px;">overflow</td></tr></table>',
    ]
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            count = random.randint(1, 3)
            injection = ''.join(random.choices(tables, k=count))
            html = html[:end_tag + 1] + injection + html[end_tag + 1:]
    return html


def inject_list_item_stress(html):
    """Inject list items with display overrides and nesting chaos."""
    items = []
    count = random.choice([5, 10, 20])
    for i in range(count):
        display = random.choice(['block', 'flex', 'grid', 'inline', 'table', 'none', 'list-item', 'contents'])
        items.append(f'<li style="display:{display};list-style:inside;">L{i}</li>')
    # some items with sub-lists
    items.append('<li><ul>' + ''.join(f'<li style="float:left;">SL{i}</li>' for i in range(5)) + '</ul></li>')
    items.append('<li style="display:flex;"><ol><li>nested-ol</li></ol></li>')
    random.shuffle(items)
    container = random.choice(['ul', 'ol'])
    list_html = f'<{container} style="list-style-position:outside;padding:0;margin:0;">{"".join(items)}</{container}>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + list_html + html[end_tag + 1:]
    return html


def randomize_flex_properties(html):
    """Randomly assign flex properties to all elements with style attrs."""
    flex_props = [
        'display:flex', 'display:inline-flex',
        'flex-wrap:wrap', 'flex-wrap:nowrap', 'flex-wrap:wrap-reverse',
        'flex-direction:column', 'flex-direction:row-reverse', 'flex-direction:column-reverse',
        'align-items:stretch', 'align-items:center', 'align-items:baseline',
        'justify-content:space-between', 'justify-content:space-around', 'justify-content:center',
        'align-content:flex-start', 'align-content:stretch', 'align-content:space-between',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(8, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        num = random.randint(1, 3)
        props = ';'.join(random.sample(flex_props, min(num, len(flex_props))))
        pos = match.end(1) + offset
        html = html[:pos] + ';' + props + html[pos:]
        offset += len(props) + 1
    return html


def inject_grid_item_placement(html):
    """Add conflicting grid-row/column placement on child elements."""
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        placements = [
            f'grid-row:{random.choice(["1", "2", "span 2", "-1", "1 / -1", "1 / span 999", "auto"])}',
            f'grid-column:{random.choice(["1", "2", "span 3", "-1", "1 / -1", "1 / span 999", "auto"])}',
        ]
        props = ';'.join(random.sample(placements, random.randint(1, 2)))
        pos = match.end(1) + offset
        html = html[:pos] + ';' + props + html[pos:]
        offset += len(props) + 1
    return html


def inject_min_max_conflict(html):
    """Create min-width > max-width and min-height > max-height conflicts."""
    conflicts = [
        'min-width:500px;max-width:100px;',
        'min-height:500px;max-height:100px;',
        'min-width:100%;max-width:0;',
        'min-height:999px;max-height:0;height:50%;',
        'min-width:99999px;max-width:1px;width:50%;',
        'min-width:auto;max-width:min-content;width:max-content;',
        'min-height:fit-content;max-height:0;',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        conflict = random.choice(conflicts)
        pos = match.end(1) + offset
        html = html[:pos] + ';' + conflict + html[pos:]
        offset += len(conflict) + 1
    return html


def inject_border_spacing_stress(html):
    """Inject extreme border-spacing values on tables."""
    table_pattern = re.compile(r'<table([^>]*?)>', re.IGNORECASE)
    matches = list(table_pattern.finditer(html))
    if not matches:
        # inject a table
        table = '<table style="border-collapse:separate;border-spacing:9999px;"><tr><td>a</td><td>b</td></tr><tr><td>c</td><td>d</td></tr></table>'
        body_pos = html.lower().find('<body')
        if body_pos >= 0:
            end_tag = html.find('>', body_pos)
            if end_tag >= 0:
                html = html[:end_tag + 1] + table + html[end_tag + 1:]
        return html
    match = random.choice(matches)
    spacing = random.choice(['0', '1px', '50px', '999px', '9999px', '-10px'])
    attrs = match.group(1)
    if 'style="' in attrs.lower():
        insert_pos = match.start() + match.group(0).rindex('"')
        html = html[:insert_pos] + f';border-collapse:separate;border-spacing:{spacing}' + html[insert_pos:]
    else:
        pos = match.end(1)
        html = html[:pos] + f' style="border-collapse:separate;border-spacing:{spacing}"' + html[pos:]
    return html


def inject_calc_expressions(html):
    """Inject complex and pathological calc() expressions into styles."""
    calcs = [
        'calc(100% - 100% + 0px)',
        'calc(100% / 3 * 3)',
        'calc(1px + 1em + 1rem + 1vh + 1vw)',
        'calc(100% * 0)',
        'calc(100% * -1)',
        'calc(99999px + 99999px)',
        'calc(100vw - 200%)',
        'calc(0px / 1)',
        'min(0px, max(100%, 50px))',
        'max(0px, min(100%, 99999px))',
        'clamp(0px, 50%, 99999px)',
        'clamp(99999px, 50%, 0px)',  # inverted clamp
        'calc(100% - calc(50% + calc(25% - 10px)))',
        'calc(1px * 0.000001)',
        'calc(100% + 0.001px)',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        prop = random.choice(['width', 'height', 'margin-left', 'padding-top', 'top', 'left', 'flex-basis'])
        val = random.choice(calcs)
        addition = f';{prop}:{val}'
        pos = match.end(1) + offset
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_nested_flexbox(html):
    """Create deeply nested flex containers, each overriding parent properties."""
    depth = random.choice([5, 10, 20, 50])
    inner = '<span style="flex:1;min-width:0;">leaf</span>'
    for i in range(depth):
        direction = random.choice(['row', 'column', 'row-reverse', 'column-reverse'])
        wrap = random.choice(['nowrap', 'wrap', 'wrap-reverse'])
        # only add sibling at first few levels to avoid exponential growth
        sibling = inner if i < 3 else ''
        inner = f'<div style="display:flex;flex-direction:{direction};flex-wrap:{wrap};flex:1;min-width:0;min-height:0;">{inner}{sibling}</div>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + inner + html[end_tag + 1:]
    return html


def inject_float_in_flex(html):
    """Inject floated elements inside flex/grid containers."""
    floated_children = ''.join(
        f'<div style="float:{random.choice(["left", "right"])};width:{random.choice(["50px", "50%", "auto"])};">F{i}</div>'
        for i in range(random.choice([3, 5, 10]))
    )
    # Find flex/grid containers
    flex_pattern = re.compile(r'display\s*:\s*(?:flex|grid|inline-flex|inline-grid)', re.IGNORECASE)
    tag_pattern = re.compile(r'<(div|section|article)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    for match in matches:
        if flex_pattern.search(match.group()):
            # inject floats after this opening tag
            pos = match.end()
            html = html[:pos] + floated_children + html[pos:]
            return html
    # fallback: inject flex container with floats
    container = f'<div style="display:flex;flex-wrap:wrap;">{floated_children}</div>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + container + html[end_tag + 1:]
    return html


def inject_clearfix_chaos(html):
    """Insert random clear properties on many elements."""
    tag_pattern = re.compile(r'<(div|p|section|article|span|li)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(3, min(10, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        clear_val = random.choice(['left', 'right', 'both', 'none'])
        float_val = random.choice(['left', 'right', 'none'])
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';clear:{clear_val};float:{float_val}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="clear:{clear_val};float:{float_val}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_auto_margin_stress(html):
    """Set margin:auto on many elements to stress centering logic."""
    tag_pattern = re.compile(r'<(div|section|article|table|p)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(3, min(10, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    auto_styles = [
        'margin:auto',
        'margin:0 auto',
        'margin:auto auto 0',
        'margin-left:auto;margin-right:auto',
        'margin-left:auto;margin-right:0',
        'margin:auto;width:50%',
        'margin:auto;position:absolute;top:0;left:0;right:0;bottom:0',
    ]
    for match in sorted(chosen, key=lambda m: m.start()):
        style = random.choice(auto_styles)
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';{style}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="{style}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_percentage_padding(html):
    """Inject percentage paddings which resolve against containing block width."""
    tag_pattern = re.compile(r'<(div|td|th|span|p)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    pct_values = ['0%', '10%', '50%', '100%', '200%', '999%']
    for match in sorted(chosen, key=lambda m: m.start()):
        p = random.choice(pct_values)
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';padding:{p}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="padding:{p}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_inline_block_whitespace(html):
    """Inject inline-block elements separated by various whitespace patterns."""
    ws_types = [' ', '\n', '\t', '  ', '\n  \n', '&#8203;', '<!-- comment -->']
    count = random.choice([5, 10, 20])
    elems = []
    for i in range(count):
        w = random.choice(['50px', '25%', '33.33%', 'auto', 'min-content', 'max-content'])
        elems.append(f'<span style="display:inline-block;width:{w};height:20px;vertical-align:{random.choice(["top", "bottom", "middle", "baseline"])};">IB{i}</span>')
    sep = random.choice(ws_types)
    ib_html = sep.join(elems)
    wrapper = f'<div style="font-size:{random.choice(["0", "12px", "16px", "99px"])};text-align:{random.choice(["left", "center", "right", "justify"])};">{ib_html}</div>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + wrapper + html[end_tag + 1:]
    return html


def scramble_tag_names(html):
    """Replace some common tag names with unusual HTML5 elements."""
    unusual = ['main', 'aside', 'nav', 'header', 'footer', 'figure', 'figcaption',
               'details', 'summary', 'dialog', 'data', 'time', 'mark', 'ruby',
               'rt', 'rp', 'bdi', 'bdo', 'wbr', 'meter', 'progress', 'output',
               'address', 'hgroup', 'search']
    tag_pattern = re.compile(r'<(div|span|p|section|article)(\s|>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(5, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        old_tag = match.group(1)
        new_tag = random.choice(unusual)
        # replace opening tag
        start = match.start(1) + offset
        end = match.end(1) + offset
        html = html[:start] + new_tag + html[end:]
        offset += len(new_tag) - len(old_tag)
        # try to find and replace corresponding closing tag (best effort)
        close_pattern = re.compile(rf'</{re.escape(old_tag)}>', re.IGNORECASE)
        close_match = close_pattern.search(html, start + len(new_tag))
        if close_match:
            html = html[:close_match.start() + 2] + new_tag + html[close_match.start() + 2 + len(old_tag):]
            offset += len(new_tag) - len(old_tag)
    return html


def inject_br_wbr_bomb(html):
    """Inject massive numbers of <br> and <wbr> tags into text content."""
    count = random.choice([50, 100, 500])
    tags = random.choice(['<br>', '<wbr>', '<br><wbr>'])
    bomb = tags * count
    text_pattern = re.compile(r'>([^<]{3,})<', re.IGNORECASE)
    matches = list(text_pattern.finditer(html))
    if matches:
        match = random.choice(matches)
        mid = (match.start(1) + match.end(1)) // 2
        html = html[:mid] + bomb + html[mid:]
    return html


def inject_anonymous_block_stress(html):
    """Mix inline and block content in the same parent to stress anonymous block creation."""
    mixed_content = [
        'text before<div style="display:block;">block child</div>text after',
        '<span>inline</span><div>block</div><span>inline again</span>',
        'raw text<p>paragraph</p>more raw text<div>block</div>final text',
        '<b>bold</b><table><tr><td>table</td></tr></table><i>italic</i>',
        'A<div style="float:left;">float</div>B<span>C</span>D<div>E</div>F',
        '<span>x</span>\n<div style="display:inline;">inline-div</div>\n<p>p</p>\nbare text',
    ]
    tag_pattern = re.compile(r'(<(div|section|article)[^>]*>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        match = random.choice(matches)
        content = random.choice(mixed_content)
        pos = match.end()
        html = html[:pos] + content + html[pos:]
    return html


def inject_table_width_algorithm(html):
    """Inject tables with competing width constraints to stress table layout."""
    table = '<table style="table-layout:fixed;width:100px;"><colgroup>'
    col_count = random.choice([3, 5, 10])
    for i in range(col_count):
        w = random.choice(['200px', '50%', '0', 'auto', '1px', '99999px'])
        table += f'<col style="width:{w};">'
    table += '</colgroup><tr>'
    for i in range(col_count):
        cw = random.choice(['300px', '0', 'auto', '100%', 'min-content'])
        table += f'<td style="width:{cw};white-space:nowrap;">cell{i} with some content</td>'
    table += '</tr></table>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + table + html[end_tag + 1:]
    return html


def inject_fixed_position_bomb(html):
    """Inject many fixed-position elements overlapping each other."""
    count = random.choice([10, 20, 50])
    elems = []
    for i in range(count):
        top = random.choice(['0', '50%', '100%', '-100px', '99999px'])
        left = random.choice(['0', '50%', '100%', '-100px', '99999px'])
        w = random.choice(['100px', '50%', '100%', '0', '99999px'])
        h = random.choice(['100px', '50%', '100%', '0', '99999px'])
        elems.append(f'<div style="position:fixed;top:{top};left:{left};width:{w};height:{h};overflow:hidden;">FP{i}</div>')
    bomb = ''.join(elems)
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + bomb + html[end_tag + 1:]
    return html


def inject_line_break_stress(html):
    """Insert word-break and line-break properties with conflicting values."""
    tag_pattern = re.compile(r'<(div|span|p|td)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        wb = random.choice(['normal', 'break-all', 'keep-all', 'break-word'])
        ws = random.choice(['normal', 'nowrap', 'pre', 'pre-wrap', 'pre-line', 'break-spaces'])
        ow = random.choice(['normal', 'break-word', 'anywhere'])
        hyphens = random.choice(['none', 'manual', 'auto'])
        props = f';word-break:{wb};white-space:{ws};overflow-wrap:{ow};hyphens:{hyphens}'
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            html = html[:insert_pos] + props + html[insert_pos:]
            offset += len(props)
        else:
            insertion = f' style="word-break:{wb};white-space:{ws};overflow-wrap:{ow};hyphens:{hyphens}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_order_stress(html):
    """Inject extreme order values on flex/grid items."""
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(8, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        order = random.choice(['-99999', '-1', '0', '1', '99999', '2147483647', '-2147483648'])
        pos = match.end(1) + offset
        addition = f';order:{order}'
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_aspect_ratio_conflict(html):
    """Create conflicting aspect-ratio with explicit width/height."""
    conflicts = [
        'aspect-ratio:1;width:100px;height:200px;',
        'aspect-ratio:16/9;width:100%;height:100%;',
        'aspect-ratio:0.001;width:99999px;',
        'aspect-ratio:9999;height:99999px;',
        'aspect-ratio:1/0;width:100px;',
        'aspect-ratio:0;width:100px;height:100px;',
        'aspect-ratio:auto;width:0;height:0;',
        'aspect-ratio:1;min-width:500px;max-width:100px;',
    ]
    tag_pattern = re.compile(r'<(div|img|span|section)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        conflict = random.choice(conflicts)
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            html = html[:insert_pos] + ';' + conflict + html[insert_pos:]
            offset += len(conflict) + 1
        else:
            insertion = f' style="{conflict}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_table_rowspan_overlap(html):
    """Create tables with overlapping rowspan/colspan that exceed grid bounds."""
    rows = random.choice([3, 5, 8])
    cols = random.choice([3, 5, 8])
    table = '<table style="width:100%;border-collapse:collapse;">'
    for r in range(rows):
        table += '<tr>'
        for c in range(cols):
            rs = random.choice([1, 1, 1, 2, 3, rows, rows * 2])
            cs = random.choice([1, 1, 1, 2, 3, cols, cols * 2])
            table += f'<td rowspan="{rs}" colspan="{cs}" style="border:1px solid black;">R{r}C{c}</td>'
        table += '</tr>'
    table += '</table>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + table + html[end_tag + 1:]
    return html


def inject_transform_stress(html):
    """Apply extreme transform chains on nested elements."""
    transforms = [
        'scale(0)', 'scale(-1)', 'scale(0.001)', 'scale(9999)',
        'translateX(99999px)', 'translateY(-99999px)', 'translate(50%, 50%)',
        'rotate(360deg)', 'rotate(99999deg)', 'skewX(89.9deg)', 'skewY(89.9deg)',
        'matrix(0,0,0,0,0,0)', 'matrix(1,0,0,1,99999,99999)',
        'perspective(0)', 'perspective(0.001px)',
    ]
    tag_pattern = re.compile(r'<(div|span|section|p)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        num_transforms = random.randint(1, 4)
        chain = ' '.join(random.choices(transforms, k=num_transforms))
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';transform:{chain}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="transform:{chain}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_multicol_stress(html):
    """Inject multi-column layout stress with breaks and spanning."""
    inner = ''
    for i in range(random.choice([5, 10, 20])):
        span = random.choice(['', 'column-span:all;', ''])
        break_val = random.choice(['', 'break-before:column;', 'break-after:column;', 'break-inside:avoid;', ''])
        inner += f'<div style="{span}{break_val}height:{random.choice(["50px", "200px", "0", "auto"])};">MC{i}</div>'
    container = f'<div style="column-count:{random.choice([2, 3, 5, 99])};column-gap:{random.choice(["0", "20px", "99999px"])};column-width:{random.choice(["0", "auto", "100px", "1px"])};">{inner}</div>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + container + html[end_tag + 1:]
    return html


def inject_counters_stress(html):
    """Inject CSS counters with extreme values."""
    counter_style = '''<style>
.ctr-parent { counter-reset: c 0; }
.ctr-child::before { counter-increment: c 999999; content: counter(c) " "; }
.ctr-nested { counter-reset: c -999999; }
</style>'''
    items = ''.join(f'<div class="ctr-child">item{i}</div>' for i in range(random.choice([10, 50, 100])))
    content = f'{counter_style}<div class="ctr-parent">{items}<div class="ctr-nested">{items}</div></div>'
    head_pos = html.lower().find('</head>')
    if head_pos >= 0:
        html = html[:head_pos] + counter_style + html[head_pos:]
        body_pos = html.lower().find('<body')
        if body_pos >= 0:
            end_tag = html.find('>', body_pos)
            if end_tag >= 0:
                container = f'<div class="ctr-parent">{items}<div class="ctr-nested">{items}</div></div>'
                html = html[:end_tag + 1] + container + html[end_tag + 1:]
    return html


def inject_shadow_dom_like_nesting(html):
    """Simulate shadow-DOM-like deep component nesting with style isolation."""
    depth = random.choice([3, 5, 10, 20])
    inner = '<span style="color:red;">leaf</span>'
    for i in range(depth):
        display = random.choice(['block', 'flex', 'grid', 'inline-block', 'contents'])
        overflow = random.choice(['visible', 'hidden', 'auto'])
        position = random.choice(['static', 'relative', 'absolute'])
        inner = f'<div style="display:{display};overflow:{overflow};position:{position};all:initial;"><style>div{{color:blue;}}</style>{inner}</div>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + inner + html[end_tag + 1:]
    return html


def inject_contain_property(html):
    """Inject CSS contain property to test containment."""
    contain_values = ['none', 'layout', 'paint', 'size', 'style', 'content', 'strict',
                      'layout paint', 'layout size', 'size paint', 'inline-size']
    tag_pattern = re.compile(r'<(div|section|article)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        contain = random.choice(contain_values)
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';contain:{contain}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="contain:{contain}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_gap_stress(html):
    """Inject extreme gap values on flex/grid containers and also on non-flex elements."""
    gap_values = ['0', '0.001px', '1px', '50px', '999px', '99999px', '50%', '100%', 'normal',
                  'calc(100% - 1px)', 'calc(100% + 100%)']
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        row_gap = random.choice(gap_values)
        col_gap = random.choice(gap_values)
        addition = f';row-gap:{row_gap};column-gap:{col_gap}'
        pos = match.end(1) + offset
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_all_inherit(html):
    """Set all properties to inherit/initial/unset on random elements."""
    tag_pattern = re.compile(r'<(div|span|p|section|td|th)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(2, min(6, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        val = random.choice(['inherit', 'initial', 'unset', 'revert'])
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';all:{val}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="all:{val}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_css_variable_bomb(html):
    """Inject CSS custom properties with circular-like references and extreme values."""
    var_rules = [
        ':root { --a: var(--b); --b: var(--c); --c: var(--a); }',
        ':root { --x: calc(var(--x) + 1px); }',
        ':root { --w: 99999px; }',
        ':root { --z: ; }',
        ':root { --long: ' + 'A' * 10000 + '; }',
        'div { --local: var(--nonexistent, var(--also-missing, 50px)); }',
    ]
    style_block = '<style>' + '\n'.join(random.sample(var_rules, random.randint(2, len(var_rules)))) + '</style>'
    head_pos = html.lower().find('</head>')
    if head_pos >= 0:
        html = html[:head_pos] + style_block + html[head_pos:]
    else:
        body_pos = html.lower().find('<body')
        if body_pos >= 0:
            end_tag = html.find('>', body_pos)
            if end_tag >= 0:
                html = html[:end_tag + 1] + style_block + html[end_tag + 1:]
    # also use vars in inline styles
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if matches:
        match = random.choice(matches)
        pos = match.end(1)
        html = html[:pos] + ';width:var(--w);height:var(--z, 100px)' + html[pos:]
    return html


def remove_all_whitespace_between_tags(html):
    """Remove all whitespace between tags to stress whitespace-sensitive layout."""
    return re.sub(r'>\s+<', '><', html)


def inject_massive_whitespace(html):
    """Inject large amounts of whitespace between elements."""
    ws = '\n' * random.choice([100, 500, 1000]) + ' ' * random.choice([100, 500])
    tag_pattern = re.compile(r'(</div>|</p>|</span>|</td>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        count = random.randint(1, min(3, len(matches)))
        chosen = random.sample(matches, count)
        offset = 0
        for match in sorted(chosen, key=lambda m: m.start()):
            pos = match.end() + offset
            html = html[:pos] + ws + html[pos:]
            offset += len(ws)
    return html


def inject_svg_element(html):
    """Inject inline SVG elements into the document."""
    svgs = [
        '<svg width="100" height="100" style="display:block;overflow:visible;"><rect width="200" height="200" fill="red"/></svg>',
        '<svg viewBox="0 0 0 0" style="width:100%;height:auto;"><circle r="99999"/></svg>',
        '<svg width="0" height="0" style="position:absolute;overflow:visible;"><text y="20" font-size="99999">SVG text</text></svg>',
        '<svg viewBox="-99999 -99999 199998 199998" preserveAspectRatio="none" style="width:100%;height:99999px;"><line x1="-99999" y1="0" x2="99999" y2="0" stroke="black"/></svg>',
        '<svg style="display:inline;width:50%;height:50%;vertical-align:bottom;"><g transform="scale(0)"><rect width="100" height="100"/></g></svg>',
        '<svg xmlns="http://www.w3.org/2000/svg" style="display:flex;"><path d="M0,0 L99999,99999 Z"/></svg>',
        '<svg width="1" height="1" style="min-width:99999px;"><ellipse cx="0" cy="0" rx="99999" ry="0"/></svg>',
        '<svg viewBox="0 0 100 100" style="width:0;height:0;overflow:visible;"><circle cx="50" cy="50" r="99999" fill="blue"/><text x="0" y="50" font-size="0">zero</text></svg>',
        '<svg style="display:inline-block;vertical-align:middle;width:100%;aspect-ratio:1;"><defs><linearGradient id="fg"><stop offset="0%" style="stop-color:red"/></linearGradient></defs><rect width="100%" height="100%" fill="url(#fg)"/></svg>',
    ]
    tag_pattern = re.compile(r'(</div>|</p>|</span>|</td>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        count = random.randint(1, min(3, len(matches)))
        chosen = random.sample(matches, count)
        offset = 0
        for match in sorted(chosen, key=lambda m: m.start()):
            svg = random.choice(svgs)
            pos = match.start() + offset
            html = html[:pos] + svg + html[pos:]
            offset += len(svg)
    return html


def inject_data_uri_image(html):
    """Inject images with data URI sources."""
    data_imgs = [
        '<img src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" style="width:99999px;height:0;">',
        '<img src="data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 width=%2299999%22 height=%221%22/%3E" style="display:block;max-width:100%;">',
        '<img src="data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/5+hHgAHggJ/PchI7wAAAABJRU5ErkJggg==" style="width:0;height:0;float:left;">',
        '<img src="data:," style="width:100%;aspect-ratio:16/9;">',
        '<img src="data:image/gif;base64," style="display:inline-block;vertical-align:middle;width:50%;min-width:99999px;">',
        '<img src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" style="position:absolute;top:0;left:0;width:100%;height:100%;object-fit:cover;">',
        '<img src="data:image/png;base64,invalid" style="display:flex;width:0;height:99999px;">',
    ]
    tag_pattern = re.compile(r'(</div>|</p>|</span>|</td>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        count = random.randint(1, min(3, len(matches)))
        chosen = random.sample(matches, count)
        offset = 0
        for match in sorted(chosen, key=lambda m: m.start()):
            img = random.choice(data_imgs)
            pos = match.start() + offset
            html = html[:pos] + img + html[pos:]
            offset += len(img)
    return html


def inject_external_stylesheet(html):
    """Inject external stylesheet links."""
    stylesheets = [
        '<link rel="stylesheet" href="https://cdnjs.cloudflare.com/ajax/libs/normalize/8.0.1/normalize.min.css">',
        '<link rel="stylesheet" href="https://cdn.jsdelivr.net/npm/reset-css@5.0.2/reset.min.css">',
        '<link rel="stylesheet" href="nonexistent.css">',
        '<link rel="stylesheet" href="">',
        '<link rel="stylesheet" href="data:text/css,*{margin:0;padding:0;box-sizing:border-box;}">',
        '<link rel="stylesheet" href="data:text/css,div{display:flex!important;}">',
    ]
    head_pos = html.lower().find('</head>')
    if head_pos >= 0:
        count = random.randint(1, 3)
        links = ''.join(random.choices(stylesheets, k=count))
        html = html[:head_pos] + links + html[head_pos:]
    return html


def inject_font_face(html):
    """Inject @font-face declarations with various font formats (woff, woff2, ttf)."""
    font_faces = [
        '@font-face { font-family: "FuzzWoff2"; src: url("data:font/woff2;base64,d09GMgABAAAAAA") format("woff2"); }',
        '@font-face { font-family: "FuzzWoff"; src: url("data:font/woff;base64,d09GRgABAAAAAA") format("woff"); }',
        '@font-face { font-family: "FuzzTTF"; src: local("NonExistentFont"), url("nonexistent.ttf") format("truetype"); font-display: swap; }',
        '@font-face { font-family: "FuzzEmpty"; src: url(""); }',
        '@font-face { font-family: "FuzzGoogle"; src: url("https://fonts.gstatic.com/s/roboto/v30/KFOmCnqEu92Fr1Mu4mxK.woff2") format("woff2"); }',
        '@font-face { font-family: ""; src: local("Arial"); }',
        '@font-face { font-family: "FuzzRange"; src: local("Arial"); unicode-range: U+0-0; }',
        '@font-face { font-family: "FuzzWeightRange"; src: local("Arial"); font-weight: 1 1000; font-style: oblique -90deg 90deg; }',
    ]
    style_block = '<style>' + '\n'.join(random.sample(font_faces, random.randint(2, len(font_faces)))) + '</style>'

    head_pos = html.lower().find('</head>')
    if head_pos >= 0:
        html = html[:head_pos] + style_block + html[head_pos:]

    # use the fonts in inline styles
    font_names = ['FuzzWoff2', 'FuzzWoff', 'FuzzTTF', 'FuzzEmpty', 'FuzzGoogle', 'FuzzRange']
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if matches:
        count = random.randint(1, min(4, len(matches)))
        chosen = random.sample(matches, count)
        offset = 0
        for match in sorted(chosen, key=lambda m: m.start()):
            font = random.choice(font_names)
            addition = f";font-family:'{font}',sans-serif;font-weight:{random.choice(['100', '400', '900'])}"
            pos = match.end(1) + offset
            html = html[:pos] + addition + html[pos:]
            offset += len(addition)
    return html


def inject_web_font_link(html):
    """Inject Google Fonts and other web font links."""
    font_links = [
        '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Roboto:wght@100;400;900&display=swap">',
        '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Open+Sans:ital,wght@0,400;1,700&display=swap">',
        '<link rel="stylesheet" href="https://fonts.googleapis.com/css2?family=Noto+Sans+SC&display=swap">',
        '<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>',
    ]
    head_pos = html.lower().find('</head>')
    if head_pos >= 0:
        links = ''.join(random.sample(font_links, random.randint(1, len(font_links))))
        html = html[:head_pos] + links + html[head_pos:]

    # apply web fonts to elements
    web_fonts = ["'Roboto'", "'Open Sans'", "'Noto Sans SC'"]
    tag_pattern = re.compile(r'<(div|span|p|td|th|li|h[1-6])([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        count = random.randint(1, min(4, len(matches)))
        chosen = random.sample(matches, count)
        offset = 0
        for match in sorted(chosen, key=lambda m: m.start()):
            font = random.choice(web_fonts)
            attrs = match.group(2)
            if 'style="' in attrs.lower():
                insert_pos = match.start() + offset + match.group(0).rindex('"')
                addition = f';font-family:{font},sans-serif'
                html = html[:insert_pos] + addition + html[insert_pos:]
                offset += len(addition)
            else:
                insertion = f' style="font-family:{font},sans-serif"'
                pos = match.end(2) + offset
                html = html[:pos] + insertion + html[pos:]
                offset += len(insertion)
    return html


def inject_form_chaos(html):
    """Inject comprehensive form elements with extreme styling."""
    form_elems = [
        '<fieldset style="display:flex;width:0;padding:99999px;"><legend style="float:left;width:200%;">Legend</legend><input type="text" style="flex:1;min-width:99999px;"></fieldset>',
        '<form style="display:grid;grid-template-columns:repeat(999,1fr);gap:0;"><input type="text"><input type="email"><textarea>T</textarea><select><option>O</option></select><button>B</button></form>',
        '<fieldset style="display:table;width:100%;"><legend style="display:table-caption;">Cap</legend><input type="range" style="width:200%;"><meter value="0.5" style="width:100%;display:block;"></fieldset>',
        '<input type="text" style="position:absolute;width:99999px;height:99999px;font-size:0;padding:50%;">',
        '<textarea style="display:inline-flex;width:0;height:0;resize:both;overflow:scroll;" rows="9999" cols="9999">X</textarea>',
        '<select style="display:grid;width:100%;height:0;" multiple size="9999">' + ''.join(f'<option>{i}</option>' for i in range(100)) + '</select>',
        '<output style="display:flex;flex:1;position:fixed;top:-99999px;">output</output>',
        '<progress value="50" max="100" style="display:block;width:200%;height:99999px;"></progress>',
        '<meter value="0.7" min="0" max="1" style="display:inline-block;width:100%;height:0;vertical-align:bottom;"></meter>',
        '<input type="image" src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" style="display:block;width:99999px;height:0;aspect-ratio:1;">',
    ]
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            count = random.randint(1, 4)
            injection = ''.join(random.choices(form_elems, k=count))
            html = html[:end_tag + 1] + injection + html[end_tag + 1:]
    return html


def inject_fieldset_legend(html):
    """Wrap random elements in fieldset/legend combinations."""
    tag_pattern = re.compile(r'(<(div|section|article)[^>]*>)(.*?)(</\2>)', re.DOTALL | re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    match = random.choice(matches)
    content = match.group(3)
    fs_display = random.choice(DISPLAY_VALUES)
    leg_display = random.choice(['block', 'inline', 'inline-block', 'flex', 'none', 'contents'])
    leg_extra = ''
    if random.random() < 0.3: leg_extra += f';float:{random.choice(["left", "right"])}'
    if random.random() < 0.3: leg_extra += f';position:{random.choice(POSITION_VALUES)}'
    fieldset = f'<fieldset style="display:{fs_display};width:{random.choice(EXTREME_LENGTHS)};padding:{random.choice(EXTREME_LENGTHS)};"><legend style="display:{leg_display}{leg_extra};">Legend</legend>{content}</fieldset>'
    html = html[:match.start()] + fieldset + html[match.end():]
    return html


def inject_background_image_data_uri(html):
    """Inject background-image with data URI on random elements."""
    bg_uris = [
        'url(data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7)',
        'url(data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/5+hHgAHggJ/PchI7wAAAABJRU5ErkJggg==)',
        'url(data:,)',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        bg = random.choice(bg_uris)
        size = random.choice(['cover', 'contain', 'auto', '100% 100%', '0 0', '99999px'])
        addition = f';background-image:{bg};background-size:{size};background-repeat:{random.choice(["repeat", "no-repeat", "space"])}'
        pos = match.end(1) + offset
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_font_property_stress(html):
    """Inject extreme font property values on random elements."""
    font_stresses = [
        'font-size:0;line-height:99999',
        'font-size:99999px;line-height:0',
        'font-size:0.001px;letter-spacing:99999px',
        'font-weight:1;font-stretch:ultra-condensed',
        'font-weight:1000;font-style:oblique 90deg',
        "font-family:'',sans-serif;font-size:inherit",
        'font-variant:small-caps;text-transform:uppercase',
        'font-size:calc(100vw);line-height:calc(100vh)',
        'font:italic bold 0/0 serif',
        'font:99999px/0 monospace',
        'font-size:xx-large;font-weight:bolder;font-stretch:200%',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(5, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        stress = random.choice(font_stresses)
        pos = match.end(1) + offset
        addition = f';{stress}'
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_text_decoration_stress(html):
    """Inject complex text decoration properties."""
    decorations = [
        'text-decoration:underline overline line-through',
        'text-decoration:underline wavy red',
        'text-decoration-thickness:99999px',
        'text-underline-offset:99999px',
        'text-decoration-skip-ink:none',
        'text-decoration:underline;text-underline-position:under',
        'text-shadow:0 0 99999px red,0 0 99999px blue,0 0 99999px green',
        'text-shadow:99999px 99999px 0 black',
        '-webkit-text-stroke:99999px red',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(5, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        deco = random.choice(decorations)
        pos = match.end(1) + offset
        addition = f';{deco}'
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_clip_path_mask(html):
    """Inject clip-path and mask CSS properties."""
    clip_paths = [
        'clip-path:circle(0)',
        'clip-path:circle(99999px at center)',
        'clip-path:ellipse(0 0)',
        'clip-path:inset(0)',
        'clip-path:inset(50%)',
        'clip-path:inset(99999px)',
        'clip-path:polygon(0 0, 100% 0, 50% 100%)',
        'clip-path:polygon(0 0, 0 0, 0 0)',
        'clip-path:none',
        'mask-image:linear-gradient(black, transparent)',
        'mask-image:none',
    ]
    tag_pattern = re.compile(r'<(div|span|section|p|img)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(5, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        clip = random.choice(clip_paths)
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';{clip}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="{clip}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_blend_mode(html):
    """Inject mix-blend-mode and background-blend-mode."""
    blend_modes = ['normal', 'multiply', 'screen', 'overlay', 'darken', 'lighten',
                   'color-dodge', 'color-burn', 'hard-light', 'soft-light',
                   'difference', 'exclusion', 'hue', 'saturation', 'color', 'luminosity']
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        mode = random.choice(blend_modes)
        prop = random.choice(['mix-blend-mode', 'background-blend-mode'])
        pos = match.end(1) + offset
        addition = f';{prop}:{mode};isolation:{random.choice(["auto", "isolate"])}'
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_ruby_annotation(html):
    """Inject ruby annotation elements for East Asian text layout stress."""
    ruby_snippets = [
        '<ruby>漢<rp>(</rp><rt>かん</rt><rp>)</rp>字<rp>(</rp><rt>じ</rt><rp>)</rp></ruby>',
        '<ruby style="display:flex;">文字<rt style="font-size:99999px;">annotation</rt></ruby>',
        '<ruby style="position:absolute;width:99999px;">text<rt>rt</rt></ruby>',
        '<ruby>A<rt>a</rt>B<rt>b</rt>C<rt>c</rt>D<rt>d</rt>E<rt>e</rt></ruby>',
        '<ruby style="writing-mode:vertical-rl;">縦書<rt>たてがき</rt></ruby>',
    ]
    tag_pattern = re.compile(r'(</div>|</p>|</span>|</td>)', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if matches:
        count = random.randint(1, min(3, len(matches)))
        chosen = random.sample(matches, count)
        offset = 0
        for match in sorted(chosen, key=lambda m: m.start()):
            ruby = random.choice(ruby_snippets)
            pos = match.start() + offset
            html = html[:pos] + ruby + html[pos:]
            offset += len(ruby)
    return html


def inject_input_type_bomb(html):
    """Inject many different input types with extreme styling."""
    input_types = ['text', 'password', 'email', 'tel', 'url', 'search',
                   'number', 'range', 'date', 'time', 'datetime-local',
                   'month', 'week', 'color', 'file', 'hidden',
                   'checkbox', 'radio', 'submit', 'reset', 'button', 'image']
    inputs = []
    for t in random.sample(input_types, random.randint(5, len(input_types))):
        style = random.choice([
            f'width:{random.choice(EXTREME_LENGTHS)};height:{random.choice(EXTREME_LENGTHS)}',
            f'display:{random.choice(DISPLAY_VALUES)};position:{random.choice(POSITION_VALUES)}',
            f'float:{random.choice(FLOAT_VALUES)};flex:1;min-width:0',
            f'font-size:{random.choice(["0", "99999px"])};padding:{random.choice(["50%", "99999px"])}',
            f'transform:{random.choice(["scale(0)", "scale(9999)", "rotate(90deg)"])}',
        ])
        extra = ''
        if t == 'image':
            extra = ' src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"'
        inputs.append(f'<input type="{t}"{extra} style="{style}">')
    bomb = '<div style="display:flex;flex-wrap:wrap;gap:0;width:0;">' + ''.join(inputs) + '</div>'
    body_pos = html.lower().find('<body')
    if body_pos >= 0:
        end_tag = html.find('>', body_pos)
        if end_tag >= 0:
            html = html[:end_tag + 1] + bomb + html[end_tag + 1:]
    return html


def inject_svg_in_table(html):
    """Inject SVG elements inside table cells to stress mixed layout."""
    svgs_in_cells = [
        '<td style="width:0;"><svg width="99999" height="1" style="display:block;"><rect width="99999" height="1"/></svg></td>',
        '<td><svg viewBox="0 0 100 100" style="width:100%;height:auto;"><circle cx="50" cy="50" r="50"/></svg></td>',
        '<td style="height:0;"><svg style="width:100%;height:99999px;overflow:visible;"><text y="50">SVG in TD</text></svg></td>',
    ]
    table_pattern = re.compile(r'<tr([^>]*?)>', re.IGNORECASE)
    matches = list(table_pattern.finditer(html))
    if matches:
        match = random.choice(matches)
        cell = random.choice(svgs_in_cells)
        pos = match.end()
        html = html[:pos] + cell + html[pos:]
    else:
        # inject a table with SVG cells
        table = '<table style="width:100%;table-layout:fixed;"><tr>' + ''.join(random.choices(svgs_in_cells, k=3)) + '</tr></table>'
        body_pos = html.lower().find('<body')
        if body_pos >= 0:
            end_tag = html.find('>', body_pos)
            if end_tag >= 0:
                html = html[:end_tag + 1] + table + html[end_tag + 1:]
    return html


def inject_object_fit_stress(html):
    """Inject object-fit and object-position on images and replaced elements."""
    fits = ['fill', 'contain', 'cover', 'none', 'scale-down']
    positions = ['center', '0 0', '100% 100%', '-99999px 0', '50% 200%', 'left top', 'right bottom']
    img_pattern = re.compile(r'<img([^>]*?)(/?)>', re.IGNORECASE)
    matches = list(img_pattern.finditer(html))
    if not matches:
        # inject images with object-fit
        imgs = []
        for _ in range(random.randint(2, 5)):
            fit = random.choice(fits)
            pos = random.choice(positions)
            imgs.append(f'<img src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" style="width:{random.choice(EXTREME_LENGTHS)};height:{random.choice(EXTREME_LENGTHS)};object-fit:{fit};object-position:{pos};">')
        wrapper = '<div style="display:flex;flex-wrap:wrap;">' + ''.join(imgs) + '</div>'
        body_pos = html.lower().find('<body')
        if body_pos >= 0:
            end_tag = html.find('>', body_pos)
            if end_tag >= 0:
                html = html[:end_tag + 1] + wrapper + html[end_tag + 1:]
        return html

    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        fit = random.choice(fits)
        pos = random.choice(positions)
        attrs = match.group(1)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';object-fit:{fit};object-position:{pos}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="object-fit:{fit};object-position:{pos}"'
            pos_idx = match.end(1) + offset
            html = html[:pos_idx] + insertion + html[pos_idx:]
            offset += len(insertion)
    return html


def inject_resize_overflow(html):
    """Inject resize property with various overflow combinations."""
    resize_combos = [
        'resize:both;overflow:auto',
        'resize:horizontal;overflow:hidden',
        'resize:vertical;overflow:scroll',
        'resize:both;overflow:visible',
        'resize:both;overflow:hidden;width:0;height:0',
        'resize:both;overflow:auto;min-width:99999px;max-width:0',
    ]
    tag_pattern = re.compile(r'<(div|textarea|section)([^>]*?)>', re.IGNORECASE)
    matches = list(tag_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        combo = random.choice(resize_combos)
        attrs = match.group(2)
        if 'style="' in attrs.lower():
            insert_pos = match.start() + offset + match.group(0).rindex('"')
            addition = f';{combo}'
            html = html[:insert_pos] + addition + html[insert_pos:]
            offset += len(addition)
        else:
            insertion = f' style="{combo}"'
            pos = match.end(2) + offset
            html = html[:pos] + insertion + html[pos:]
            offset += len(insertion)
    return html


def inject_backdrop_filter(html):
    """Inject backdrop-filter CSS property."""
    filters = [
        'backdrop-filter:blur(0)',
        'backdrop-filter:blur(99999px)',
        'backdrop-filter:brightness(0)',
        'backdrop-filter:contrast(9999)',
        'backdrop-filter:grayscale(1) blur(10px)',
        'backdrop-filter:invert(1) saturate(9999)',
        'backdrop-filter:none',
        'filter:blur(99999px)',
        'filter:brightness(0) contrast(0)',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(4, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        f = random.choice(filters)
        pos = match.end(1) + offset
        addition = f';{f}'
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


def inject_svg_background(html):
    """Inject SVG as background-image via data URI."""
    svg_bgs = [
        'background-image:url(data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22%3E%3Crect fill=%22red%22 width=%22100%25%22 height=%22100%25%22/%3E%3C/svg%3E);background-size:cover',
        'background-image:url(data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 width=%220%22 height=%220%22/%3E);background-size:99999px',
        'background-image:url(data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22%3E%3Ccircle r=%2299999%22/%3E%3C/svg%3E);background-repeat:repeat',
    ]
    style_pattern = re.compile(r'style="([^"]*)"', re.IGNORECASE)
    matches = list(style_pattern.finditer(html))
    if not matches:
        return html
    count = random.randint(1, min(3, len(matches)))
    chosen = random.sample(matches, count)
    offset = 0
    for match in sorted(chosen, key=lambda m: m.start()):
        bg = random.choice(svg_bgs)
        pos = match.end(1) + offset
        addition = f';{bg}'
        html = html[:pos] + addition + html[pos:]
        offset += len(addition)
    return html


# ---------------------------------------------------------------------------
# Mutation pipeline
# ---------------------------------------------------------------------------

MUTATIONS = [
    ('css_value', mutate_css_value, 3),
    ('inject_css', inject_css_property, 3),
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
    ('wrap_container', wrap_in_layout_container, 2),
    ('contradictory', inject_contradictory_styles, 2),
    ('css_bomb', inject_multi_css_bomb, 2),
    ('all_display', change_all_display_modes, 1),
    ('fragment_dom', fragment_dom, 1),
    ('extreme_inline', inject_extreme_inline_styles, 2),
    ('zero_size', inject_zero_size_containers, 1),
    ('replaced_stress', inject_replaced_element_stress, 1),
    ('float_bomb', inject_float_bomb, 1),
    ('randomize_positions', randomize_all_positions, 1),
    ('remove_closing', remove_closing_tags, 2),
    ('inject_form', inject_form_elements, 1),
    ('pct_heights', randomize_to_percentage_heights, 1),
    ('display_contents', inject_display_contents, 2),
    ('empty_all', empty_all_text_nodes, 1),
    ('duplicate_styles', duplicate_css_properties, 2),
    ('inject_pseudo', inject_pseudo_elements, 1),
    ('overflow_cascade', cascade_overflow_hidden, 1),
    ('sticky_bomb', inject_sticky_bomb, 1),
    ('visibility_collapse', inject_visibility_collapse, 1),
    ('negative_margin_chain', inject_negative_margin_chain, 1),
    ('absolute_in_inline', inject_absolute_in_inline, 2),
    ('randomize_box_sizing', randomize_box_sizing, 1),
    ('direction_bidi_mix', inject_direction_bidi_mix, 1),
    ('writing_mode_stress', inject_writing_mode_stress, 1),
    ('corrupt_style_syntax', corrupt_style_syntax, 2),
    ('empty_table_stress', inject_empty_table_stress, 1),
    ('list_item_stress', inject_list_item_stress, 1),
    ('randomize_flex', randomize_flex_properties, 2),
    ('grid_item_placement', inject_grid_item_placement, 1),
    ('min_max_conflict', inject_min_max_conflict, 2),
    ('border_spacing_stress', inject_border_spacing_stress, 1),
    ('calc_expressions', inject_calc_expressions, 2),
    ('nested_flexbox', inject_nested_flexbox, 1),
    ('float_in_flex', inject_float_in_flex, 1),
    ('clearfix_chaos', inject_clearfix_chaos, 1),
    ('auto_margin_stress', inject_auto_margin_stress, 1),
    ('percentage_padding', inject_percentage_padding, 1),
    ('inline_block_ws', inject_inline_block_whitespace, 1),
    ('scramble_tags', scramble_tag_names, 1),
    ('br_wbr_bomb', inject_br_wbr_bomb, 1),
    ('anonymous_block', inject_anonymous_block_stress, 2),
    ('table_width_algo', inject_table_width_algorithm, 1),
    ('fixed_position_bomb', inject_fixed_position_bomb, 1),
    ('line_break_stress', inject_line_break_stress, 1),
    ('order_stress', inject_order_stress, 1),
    ('aspect_ratio_conflict', inject_aspect_ratio_conflict, 1),
    ('table_rowspan_overlap', inject_table_rowspan_overlap, 1),
    ('transform_stress', inject_transform_stress, 1),
    ('multicol_stress', inject_multicol_stress, 1),
    ('counters_stress', inject_counters_stress, 1),
    ('shadow_dom_nesting', inject_shadow_dom_like_nesting, 1),
    ('contain_property', inject_contain_property, 1),
    ('gap_stress', inject_gap_stress, 1),
    ('all_inherit', inject_all_inherit, 1),
    ('css_variable_bomb', inject_css_variable_bomb, 1),
    ('remove_whitespace', remove_all_whitespace_between_tags, 1),
    ('massive_whitespace', inject_massive_whitespace, 1),
    ('inject_svg', inject_svg_element, 2),
    ('data_uri_img', inject_data_uri_image, 2),
    ('ext_stylesheet', inject_external_stylesheet, 1),
    ('font_face', inject_font_face, 2),
    ('web_font_link', inject_web_font_link, 1),
    ('form_chaos', inject_form_chaos, 2),
    ('fieldset_legend', inject_fieldset_legend, 1),
    ('bg_data_uri', inject_background_image_data_uri, 2),
    ('font_prop_stress', inject_font_property_stress, 2),
    ('text_deco_stress', inject_text_decoration_stress, 1),
    ('clip_path_mask', inject_clip_path_mask, 1),
    ('blend_mode', inject_blend_mode, 1),
    ('ruby_annotation', inject_ruby_annotation, 1),
    ('input_type_bomb', inject_input_type_bomb, 1),
    ('svg_in_table', inject_svg_in_table, 1),
    ('object_fit_stress', inject_object_fit_stress, 1),
    ('resize_overflow', inject_resize_overflow, 1),
    ('backdrop_filter', inject_backdrop_filter, 1),
    ('svg_background', inject_svg_background, 1),
]


def mutate(html, num_mutations=None):
    """Apply a random combination of mutations to an HTML string."""
    if num_mutations is None:
        num_mutations = random.randint(2, 8)

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
