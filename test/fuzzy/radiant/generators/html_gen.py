#!/usr/bin/env python3
"""
Structured HTML/CSS generator for Radiant layout engine fuzzy testing.

Generates adversarial but syntactically valid HTML documents with randomized
CSS properties targeting specific layout algorithm stress points.

Usage:
    python3 html_gen.py [--mode MODE] [--count N] [--output-dir DIR] [--seed SEED]

Modes: flex, grid, table, block, float, text, inline, position, margin, mixed, all
"""

import argparse
import os
import random
import sys

# ---------------------------------------------------------------------------
# Value pools
# ---------------------------------------------------------------------------

EXTREME_LENGTHS = [
    '0', '0px', '0.001px', '0.0001px', '1px', '10px', '50px', '100px', '200px', '500px',
    '-1px', '-10px', '-50px', '-100px', '-9999px', '-99999px',
    '99999px', '999999px', '1e5px', '1e7px', '1e10px',
    'auto', 'inherit', 'initial', 'unset',
    '0%', '0.001%', '1%', '50%', '100%', '150%', '200%', '999%', '-50%', '-100%', '-999%',
    'min-content', 'max-content', 'fit-content', 'fit-content(50%)',
    'calc(100% - 1px)', 'calc(100% + 100px)', 'calc(100% - 99999px)',
    'calc(50% + 50px)', 'calc(0 / 1)', 'calc(100% * 2)',
    'calc(100vw - 100%)', 'calc(1px + 1em + 1%)',
    'calc(100% - 100% + 0.001px)', 'calc(999999px - 999998px)',
    'calc(100% / 3)', 'calc(100% / 7)', 'calc(100% / 0.001)',
    'min(100px, 50%)', 'max(0px, 100%)', 'clamp(0px, 50%, 100px)',
    '1em', '2em', '10em', '100em', '0.01em',
    '1rem', '100rem',
    '1vw', '50vw', '100vw', '200vw',
    '1vh', '50vh', '100vh',
    '1vmin', '1vmax',
    '1ch', '1ex',
]

DISPLAY_VALUES = [
    'block', 'inline', 'inline-block', 'flex', 'inline-flex',
    'grid', 'inline-grid', 'table', 'table-row', 'table-cell',
    'table-row-group', 'table-header-group', 'table-footer-group',
    'table-column', 'table-column-group', 'table-caption',
    'none', 'list-item', 'flow-root', 'contents',
]

POSITION_VALUES = ['static', 'relative', 'absolute', 'fixed', 'sticky']

OVERFLOW_VALUES = ['visible', 'hidden', 'scroll', 'auto', 'clip']

FLOAT_VALUES = ['none', 'left', 'right']

CLEAR_VALUES = ['none', 'left', 'right', 'both']

FLEX_DIRECTION = ['row', 'row-reverse', 'column', 'column-reverse']

FLEX_WRAP = ['nowrap', 'wrap', 'wrap-reverse']

ALIGN_VALUES = ['flex-start', 'flex-end', 'center', 'stretch', 'baseline',
                'space-between', 'space-around', 'space-evenly']

JUSTIFY_VALUES = ['flex-start', 'flex-end', 'center', 'stretch',
                  'space-between', 'space-around', 'space-evenly']

VERTICAL_ALIGN = ['baseline', 'top', 'middle', 'bottom', 'text-top',
                  'text-bottom', 'sub', 'super', '0', '10px', '-5px', '50%']

FONT_SIZES = ['0', '0.1px', '1px', '8px', '12px', '16px', '24px', '48px',
              '100px', '9999px', '0.5em', '2em', '10em', 'inherit']

LINE_HEIGHTS = ['0', '0.5', '1', '1.2', '1.5', '2', '5', '100',
                'normal', '0px', '10px', '9999px']

TEXT_INDENTS = ['0', '10px', '50px', '-10px', '-9999px', '50%', '-50%']

BOX_SIZING = ['content-box', 'border-box']

BORDER_STYLES = ['none', 'solid', 'dashed', 'dotted', 'double',
                 'groove', 'ridge', 'inset', 'outset', 'hidden']

WHITE_SPACE = ['normal', 'nowrap', 'pre', 'pre-wrap', 'pre-line', 'break-spaces']

WORD_BREAK = ['normal', 'break-all', 'keep-all', 'break-word']

GRID_TEMPLATE_VALUES = [
    'none', 'auto', '1fr', '100px', 'min-content', 'max-content',
    'repeat(2, 1fr)', 'repeat(3, 100px)', 'repeat(auto-fill, 100px)',
    'repeat(auto-fit, minmax(100px, 1fr))',
    'minmax(0, 1fr)', 'minmax(auto, auto)', 'minmax(100px, 200px)',
    'minmax(0, 0)', 'minmax(min-content, max-content)',
    'minmax(0, 99999px)', 'minmax(99999px, 0)',
    '1fr 2fr', '100px 1fr 100px', 'repeat(5, 1fr)',
    '50px repeat(3, 1fr) 50px',
    'repeat(20, 1fr)', 'repeat(100, 50px)', 'repeat(999, 1fr)',
    'repeat(auto-fill, minmax(0, 1fr))', 'repeat(auto-fill, minmax(1px, 1fr))',
    'repeat(auto-fit, minmax(0, 1fr))',
    '0fr 1fr 0fr', '0.001fr 999fr',
    'fit-content(100px) 1fr fit-content(200px)',
    'subgrid',
]

TAGS_BLOCK = ['div', 'section', 'article', 'main', 'aside', 'header', 'footer', 'nav', 'p', 'h1', 'h2', 'h3']
TAGS_INLINE = ['span', 'a', 'em', 'strong', 'b', 'i', 'code', 'small', 'sub', 'sup']
TAGS_ALL = TAGS_BLOCK + TAGS_INLINE

TEXT_SAMPLES = [
    'Hello World',
    'A',
    '',
    'Thisisaverylongwordwithnobreakopportunities',
    'Thisisaverylongwordwithnobreakopportunitiesthisisaverylongwordwithnobreakopportunitiesthisisaverylongwordwithnobreakopportunities',
    'Short',
    '你好世界混合Latin文字',
    '你好' * 200,
    'مرحبا بالعالم',
    'שלום עולם',
    'مرحبا Hello مرحبا World مرحبا',  # bidi mixed
    'Lorem ipsum dolor sit amet, consectetur adipiscing elit. ' * 20,
    'a b c d e f g h i j k l m n o p q r s t u v w x y z',
    '🎉🎊🎈🎁' * 50,
    ' ',
    '   \t\t\n\n   ',
    '\n' * 50,
    '&amp; &lt; &gt; &quot;',
    '<script>alert(1)</script>',  # should be escaped by parser
    'a\u200Bb\u200Bc\u200Bd',  # zero-width spaces
    'a\u00ADb\u00ADc',  # soft hyphens
    'a' * 10000,  # 10K single char
    ' '.join(['word'] * 5000),  # 5K words
]

# ---------------------------------------------------------------------------
# Helpers
# ---------------------------------------------------------------------------

def rv(pool):
    """Pick a random value from a pool."""
    return random.choice(pool)

def maybe(prob=0.5):
    """Return True with given probability."""
    return random.random() < prob

def rand_color():
    """Random CSS color."""
    r, g, b = random.randint(0, 255), random.randint(0, 255), random.randint(0, 255)
    if maybe(0.3):
        return f'rgba({r},{g},{b},{random.random():.2f})'
    return f'#{r:02x}{g:02x}{b:02x}'

def rand_length():
    return rv(EXTREME_LENGTHS)

def rand_margin():
    """Random margin value including negative."""
    return rv(['0', 'auto', '10px', '-10px', '20px', '-20px', '50px',
               '-50px', '5%', '-5%', '50%', '-9999px', '99999px'])

def rand_border():
    w = rv(['0', '1px', '2px', '5px', '10px', '50px'])
    s = rv(BORDER_STYLES)
    c = rand_color()
    return f'{w} {s} {c}'

def rand_padding():
    return rv(['0', '1px', '5px', '10px', '20px', '50px', '100px', '5%', '50%'])

def css_props_block():
    """Generate random block-model CSS properties."""
    props = {}
    if maybe(0.6): props['width'] = rand_length()
    if maybe(0.6): props['height'] = rand_length()
    if maybe(0.3): props['min-width'] = rand_length()
    if maybe(0.3): props['min-height'] = rand_length()
    if maybe(0.3): props['max-width'] = rand_length()
    if maybe(0.3): props['max-height'] = rand_length()
    if maybe(0.5): props['margin'] = f'{rand_margin()} {rand_margin()} {rand_margin()} {rand_margin()}'
    if maybe(0.4): props['padding'] = f'{rand_padding()} {rand_padding()} {rand_padding()} {rand_padding()}'
    if maybe(0.3): props['border'] = rand_border()
    if maybe(0.3): props['box-sizing'] = rv(BOX_SIZING)
    if maybe(0.2): props['overflow'] = rv(OVERFLOW_VALUES)
    if maybe(0.15): props['background'] = rand_color()
    return props

def css_to_style(props):
    """Convert a dict of CSS properties to a style attribute string."""
    if not props:
        return ''
    parts = [f'{k}: {v}' for k, v in props.items()]
    return f' style="{"; ".join(parts)}"'

def wrap_html(body, extra_css=''):
    """Wrap body content in a full HTML document."""
    return f'''<!DOCTYPE html>
<html>
<head><meta charset="utf-8"><style>
* {{ margin: 0; padding: 0; }}
{extra_css}
</style></head>
<body>
{body}
</body>
</html>'''

def make_element(tag, style_props, children='', attrs=''):
    """Create an HTML element with style."""
    style = css_to_style(style_props)
    return f'<{tag}{attrs}{style}>{children}</{tag}>'

# ---------------------------------------------------------------------------
# Targeted generators
# ---------------------------------------------------------------------------

def gen_flex_stress():
    """Generate adversarial flex layout HTML."""
    outer_props = {
        'display': rv(['flex', 'inline-flex']),
        'flex-direction': rv(FLEX_DIRECTION),
        'flex-wrap': rv(FLEX_WRAP),
        'align-items': rv(ALIGN_VALUES),
        'justify-content': rv(JUSTIFY_VALUES),
    }
    if maybe(0.4): outer_props['align-content'] = rv(ALIGN_VALUES)
    if maybe(0.5): outer_props['width'] = rand_length()
    if maybe(0.5): outer_props['height'] = rand_length()
    if maybe(0.3): outer_props['gap'] = rv(['0', '5px', '10px', '20px', '50px'])

    num_children = random.randint(1, 15)
    children = []
    for _ in range(num_children):
        child_props = css_props_block()
        # flex-specific properties
        child_props['flex-grow'] = rv(['0', '1', '2', '0.5', '0.001', '9999'])
        child_props['flex-shrink'] = rv(['0', '1', '2', '0.5', '9999'])
        child_props['flex-basis'] = rv(['auto', '0', '0px', '0%', '50%', '100%',
                                         '200%', '100px', 'min-content', 'max-content',
                                         'fit-content', 'content'])
        if maybe(0.3): child_props['align-self'] = rv(ALIGN_VALUES)
        if maybe(0.2): child_props['order'] = str(random.randint(-10, 10))
        if maybe(0.2): child_props['margin'] = 'auto'
        if maybe(0.15): child_props['position'] = rv(POSITION_VALUES)

        text = rv(TEXT_SAMPLES) if maybe(0.6) else ''

        # Nested flex
        if maybe(0.2):
            child_props['display'] = rv(['flex', 'inline-flex'])
            inner = []
            for _ in range(random.randint(1, 5)):
                ip = css_props_block()
                ip['flex'] = rv(['0 1 auto', '1 0 0', '1 1 0%', '0 0 auto', '1', '0'])
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))
            text = '\n'.join(inner)

        children.append(make_element('div', child_props, text))

    body = make_element('div', outer_props, '\n'.join(children))
    return wrap_html(body)


def gen_grid_stress():
    """Generate adversarial grid layout HTML."""
    outer_props = {
        'display': rv(['grid', 'inline-grid']),
        'grid-template-columns': rv(GRID_TEMPLATE_VALUES),
        'grid-template-rows': rv(GRID_TEMPLATE_VALUES),
    }
    if maybe(0.5): outer_props['width'] = rand_length()
    if maybe(0.5): outer_props['height'] = rand_length()
    if maybe(0.5): outer_props['gap'] = rv(['0', '5px', '10px', '20px', '1px'])
    if maybe(0.3): outer_props['align-items'] = rv(ALIGN_VALUES)
    if maybe(0.3): outer_props['justify-items'] = rv(JUSTIFY_VALUES)

    num_children = random.randint(1, 20)
    children = []
    for i in range(num_children):
        child_props = css_props_block()
        # grid placement
        if maybe(0.3):
            cs = random.randint(1, 5)
            child_props['grid-column'] = f'span {cs}'
        if maybe(0.3):
            rs = random.randint(1, 5)
            child_props['grid-row'] = f'span {rs}'
        if maybe(0.2):
            s = random.randint(1, 6)
            e = s + random.randint(1, 4)
            child_props['grid-column'] = f'{s} / {e}'
        if maybe(0.15):
            child_props['align-self'] = rv(ALIGN_VALUES)
        if maybe(0.15):
            child_props['justify-self'] = rv(JUSTIFY_VALUES)
        if maybe(0.1):
            child_props['position'] = rv(POSITION_VALUES)

        children.append(make_element('div', child_props, rv(TEXT_SAMPLES)))

    body = make_element('div', outer_props, '\n'.join(children))
    return wrap_html(body)


def gen_table_stress():
    """Generate adversarial table layout HTML."""
    table_props = {}
    if maybe(0.5): table_props['width'] = rand_length()
    if maybe(0.3): table_props['height'] = rand_length()
    if maybe(0.5): table_props['border-collapse'] = rv(['collapse', 'separate'])
    if maybe(0.3): table_props['border-spacing'] = rv(['0', '2px', '5px', '10px 20px'])
    if maybe(0.3): table_props['table-layout'] = rv(['auto', 'fixed'])
    if maybe(0.3): table_props['border'] = rand_border()

    num_rows = random.randint(1, 10)
    num_cols = random.randint(1, 8)

    # Option: orphan rows/cells without proper parents
    if maybe(0.15):
        rows = []
        for _ in range(num_rows):
            cells = []
            for _ in range(num_cols):
                cp = css_props_block()
                cells.append(make_element('td', cp, rv(TEXT_SAMPLES)))
            rows.append(make_element('tr', {}, '\n'.join(cells)))
        # <tr> without <table>
        body = '\n'.join(rows)
        return wrap_html(body)

    rows = []
    has_thead = maybe(0.3)
    has_tfoot = maybe(0.2)

    for r in range(num_rows):
        cells = []
        for c in range(num_cols):
            cp = css_props_block()
            tag = 'th' if r == 0 and has_thead else 'td'
            attrs = ''
            if maybe(0.2):
                colspan = random.choice([0, 1, 2, 3, 5, 100, 9999])
                attrs += f' colspan="{colspan}"'
            if maybe(0.2):
                rowspan = random.choice([0, 1, 2, 3, 5, 100, 9999])
                attrs += f' rowspan="{rowspan}"'
            cells.append(make_element(tag, cp, rv(TEXT_SAMPLES), attrs))
        rows.append(make_element('tr', {}, '\n'.join(cells)))

    body_parts = []
    if has_thead:
        body_parts.append(f'<thead>{rows[0]}</thead>')
        rows = rows[1:]
    if has_tfoot and len(rows) > 1:
        body_parts.append(f'<tfoot>{rows[-1]}</tfoot>')
        rows = rows[:-1]
    body_parts.insert(1 if has_thead else 0, f'<tbody>{"".join(rows)}</tbody>')

    table = make_element('table', table_props, '\n'.join(body_parts))

    # Nested tables
    if maybe(0.25):
        inner_table = gen_table_stress_inner()
        table = make_element('table', table_props,
                             '\n'.join(body_parts) + f'\n<tr><td>{inner_table}</td></tr>')

    return wrap_html(table)


def gen_table_stress_inner():
    """Small inner table for nesting."""
    rows = []
    for _ in range(random.randint(1, 3)):
        cells = [make_element('td', css_props_block(), rv(TEXT_SAMPLES))
                 for _ in range(random.randint(1, 3))]
        rows.append(make_element('tr', {}, ''.join(cells)))
    return make_element('table', {'border-collapse': rv(['collapse', 'separate']),
                                   'width': rand_length()}, ''.join(rows))


def gen_margin_collapse():
    """Generate HTML that stresses margin collapsing logic."""
    children = []
    depth = random.randint(2, 8)

    # Self-collapsing blocks (empty with only margins)
    if maybe(0.3):
        for _ in range(random.randint(1, 5)):
            children.append(make_element('div', {
                'margin-top': rand_margin(),
                'margin-bottom': rand_margin(),
            }))

    # Nested margins
    inner = rv(TEXT_SAMPLES) if maybe(0.5) else ''
    for i in range(depth):
        props = {
            'margin-top': rand_margin(),
            'margin-bottom': rand_margin(),
        }
        if maybe(0.3): props['padding-top'] = rand_padding()
        if maybe(0.3): props['padding-bottom'] = rand_padding()
        if maybe(0.2): props['border-top'] = rand_border()
        if maybe(0.2): props['border-bottom'] = rand_border()
        if maybe(0.15): props['overflow'] = rv(OVERFLOW_VALUES)
        if maybe(0.1): props['display'] = rv(['block', 'flow-root', 'flex', 'inline-block'])
        if maybe(0.1): props['float'] = rv(FLOAT_VALUES)
        if maybe(0.1): props['position'] = rv(POSITION_VALUES)
        if maybe(0.1): props['clear'] = rv(CLEAR_VALUES)
        inner = make_element('div', props, inner)

    children.append(inner)

    # Adjacent siblings with margins
    for _ in range(random.randint(0, 5)):
        children.append(make_element('div', {
            'margin-top': rand_margin(),
            'margin-bottom': rand_margin(),
            'height': rv(['0', 'auto', '10px', '50px']),
        }, rv(TEXT_SAMPLES) if maybe(0.5) else ''))

    body = '\n'.join(children)
    return wrap_html(body)


def gen_float_stress():
    """Generate HTML that stresses float layout."""
    children = []
    num_floats = random.randint(2, 12)

    for _ in range(num_floats):
        props = css_props_block()
        props['float'] = rv(['left', 'right'])
        if maybe(0.3): props['clear'] = rv(CLEAR_VALUES)
        if maybe(0.5): props['width'] = rand_length()
        if maybe(0.5): props['height'] = rand_length()
        children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    # Intersperse block elements
    for _ in range(random.randint(1, 5)):
        pos = random.randint(0, len(children))
        bp = css_props_block()
        if maybe(0.3): bp['clear'] = rv(CLEAR_VALUES)
        if maybe(0.2): bp['overflow'] = rv(OVERFLOW_VALUES)
        children.insert(pos, make_element('p', bp, rv(TEXT_SAMPLES)))

    container_props = css_props_block()
    if maybe(0.3): container_props['overflow'] = rv(OVERFLOW_VALUES)
    body = make_element('div', container_props, '\n'.join(children))
    return wrap_html(body)


def gen_text_stress():
    """Generate HTML that stresses text/inline layout."""
    props = css_props_block()
    props['font-size'] = rv(FONT_SIZES)
    props['line-height'] = rv(LINE_HEIGHTS)
    if maybe(0.4): props['text-indent'] = rv(TEXT_INDENTS)
    if maybe(0.3): props['white-space'] = rv(WHITE_SPACE)
    if maybe(0.3): props['word-break'] = rv(WORD_BREAK)
    if maybe(0.2): props['text-align'] = rv(['left', 'right', 'center', 'justify'])
    if maybe(0.2): props['direction'] = rv(['ltr', 'rtl'])
    if maybe(0.3): props['width'] = rand_length()

    # Build text content with inline elements
    parts = []
    for _ in range(random.randint(1, 10)):
        if maybe(0.4):
            # Inline element with style
            ip = {}
            if maybe(0.4): ip['font-size'] = rv(FONT_SIZES)
            if maybe(0.3): ip['vertical-align'] = rv(VERTICAL_ALIGN)
            if maybe(0.3): ip['line-height'] = rv(LINE_HEIGHTS)
            if maybe(0.2): ip['display'] = rv(['inline', 'inline-block'])
            if maybe(0.2): ip['border'] = rand_border()
            if maybe(0.2): ip['padding'] = rand_padding()
            if maybe(0.2): ip['margin'] = rand_margin()
            if maybe(0.15): ip['width'] = rand_length()
            if maybe(0.15): ip['height'] = rand_length()
            parts.append(make_element(rv(TAGS_INLINE), ip, rv(TEXT_SAMPLES)))
        else:
            parts.append(rv(TEXT_SAMPLES))

    # Float interaction with text
    if maybe(0.3):
        fp = {'float': rv(['left', 'right']), 'width': rv(['50px', '100px', '200px'])}
        if maybe(0.5): fp['height'] = rv(['50px', '100px', '200px'])
        parts.insert(0, make_element('div', fp, rv(TEXT_SAMPLES)))

    body = make_element('div', props, '\n'.join(parts))
    return wrap_html(body)


def gen_inline_stress():
    """Generate HTML that stresses inline layout edge cases."""
    parts = []
    for _ in range(random.randint(3, 20)):
        ip = {}
        tag = rv(TAGS_INLINE)
        # empty inline with decorations
        if maybe(0.3):
            ip['border'] = rand_border()
            ip['padding'] = rand_padding()
            ip['margin'] = f'{rand_margin()} {rand_margin()}'
            text = '' if maybe(0.5) else rv(TEXT_SAMPLES)
        else:
            text = rv(TEXT_SAMPLES)

        if maybe(0.3): ip['vertical-align'] = rv(VERTICAL_ALIGN)
        if maybe(0.2): ip['display'] = rv(['inline', 'inline-block', 'none'])
        if maybe(0.2): ip['line-height'] = rv(LINE_HEIGHTS)

        # Nested inline
        if maybe(0.2):
            inner_ip = {}
            if maybe(0.5): inner_ip['vertical-align'] = rv(VERTICAL_ALIGN)
            if maybe(0.3): inner_ip['font-size'] = rv(FONT_SIZES)
            text = make_element(rv(TAGS_INLINE), inner_ip, text)

        parts.append(make_element(tag, ip, text))

    container = {'width': rand_length(), 'line-height': rv(LINE_HEIGHTS)}
    if maybe(0.3): container['text-align'] = rv(['left', 'right', 'center', 'justify'])
    body = make_element('div', container, '\n'.join(parts))
    return wrap_html(body)


def gen_position_stress():
    """Generate HTML that stresses absolute/fixed/sticky positioning."""
    container_props = css_props_block()
    container_props['position'] = 'relative'
    if maybe(0.5): container_props['width'] = rand_length()
    if maybe(0.5): container_props['height'] = rand_length()

    children = []
    for _ in range(random.randint(2, 10)):
        cp = css_props_block()
        cp['position'] = rv(POSITION_VALUES)
        if cp['position'] in ('absolute', 'fixed', 'sticky'):
            if maybe(0.7): cp['top'] = rand_length()
            if maybe(0.5): cp['left'] = rand_length()
            if maybe(0.3): cp['right'] = rand_length()
            if maybe(0.3): cp['bottom'] = rand_length()
            if maybe(0.2): cp['z-index'] = str(random.choice([-999999, -1, 0, 1, 10, 999999]))
        if maybe(0.3): cp['display'] = rv(DISPLAY_VALUES)

        # Absolutely positioned child inside non-positioned parent
        if maybe(0.2):
            inner = make_element('div', {
                'position': 'absolute',
                'top': rand_length(),
                'left': rand_length(),
                'width': rand_length(),
                'height': rand_length(),
            }, rv(TEXT_SAMPLES))
            children.append(make_element('div', cp, inner + rv(TEXT_SAMPLES)))
        else:
            children.append(make_element('div', cp, rv(TEXT_SAMPLES)))

    body = make_element('div', container_props, '\n'.join(children))
    return wrap_html(body)


def gen_deep_nesting(depth=None):
    """Generate deeply nested DOM."""
    if depth is None:
        depth = random.choice([50, 100, 200, 500, 1000])
    html = rv(TEXT_SAMPLES)
    for i in range(depth):
        props = {}
        if maybe(0.3): props['margin'] = rand_margin()
        if maybe(0.2): props['padding'] = rand_padding()
        if maybe(0.1): props['display'] = rv(DISPLAY_VALUES)
        if maybe(0.1): props['position'] = rv(POSITION_VALUES)
        if maybe(0.05): props['float'] = rv(FLOAT_VALUES)
        tag = rv(TAGS_BLOCK) if maybe(0.8) else rv(TAGS_INLINE)
        html = make_element(tag, props, html)
    return wrap_html(html)


def gen_wide_siblings(count=None):
    """Generate element with many siblings."""
    if count is None:
        count = random.choice([100, 500, 1000, 5000])
    children = []
    for i in range(count):
        props = {}
        if maybe(0.2): props['display'] = rv(['inline', 'inline-block', 'block'])
        if maybe(0.1): props['float'] = rv(FLOAT_VALUES)
        if maybe(0.1): props['width'] = rand_length()
        children.append(make_element(rv(TAGS_ALL), props, str(i)))
    container = {'width': '1200px'}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_mixed_context():
    """Generate interleaved block/inline/flex/grid/table elements."""
    children = []
    for _ in range(random.randint(5, 20)):
        mode = random.choice(['block', 'inline', 'flex', 'grid', 'table', 'float', 'abs'])
        props = css_props_block()

        if mode == 'block':
            props['display'] = 'block'
            el = make_element('div', props, rv(TEXT_SAMPLES))
        elif mode == 'inline':
            props['display'] = rv(['inline', 'inline-block'])
            el = make_element('span', props, rv(TEXT_SAMPLES))
        elif mode == 'flex':
            props['display'] = 'flex'
            inner = ''.join(make_element('div', {'flex': '1'}, rv(TEXT_SAMPLES))
                           for _ in range(random.randint(1, 4)))
            el = make_element('div', props, inner)
        elif mode == 'grid':
            props['display'] = 'grid'
            props['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
            inner = ''.join(make_element('div', {}, rv(TEXT_SAMPLES))
                           for _ in range(random.randint(1, 6)))
            el = make_element('div', props, inner)
        elif mode == 'table':
            rows = ''.join(
                make_element('tr', {},
                    ''.join(make_element('td', {}, rv(TEXT_SAMPLES))
                            for _ in range(random.randint(1, 4))))
                for _ in range(random.randint(1, 3)))
            el = make_element('table', props, rows)
        elif mode == 'float':
            props['float'] = rv(['left', 'right'])
            props['width'] = rv(['100px', '200px', '50%'])
            el = make_element('div', props, rv(TEXT_SAMPLES))
        else:  # abs
            props['position'] = 'absolute'
            props['top'] = rand_length()
            props['left'] = rand_length()
            el = make_element('div', props, rv(TEXT_SAMPLES))

        children.append(el)

    container = {'position': 'relative', 'width': '1200px'}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_replaced_elements():
    """Generate replaced elements (img) with extreme dimensions."""
    children = []
    for _ in range(random.randint(2, 8)):
        w = rv(['0', '1', '100', '9999', '-1', ''])
        h = rv(['0', '1', '100', '9999', '-1', ''])
        attrs = ''
        if w: attrs += f' width="{w}"'
        if h: attrs += f' height="{h}"'
        style_props = css_props_block()
        style = css_to_style(style_props)
        children.append(f'<img src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"{attrs}{style}>')

    body = make_element('div', {'width': '800px'}, '\n'.join(children))
    return wrap_html(body)


def gen_list_stress():
    """Generate list elements with various display modes."""
    items = []
    for _ in range(random.randint(2, 15)):
        ip = css_props_block()
        if maybe(0.3): ip['display'] = rv(DISPLAY_VALUES)
        if maybe(0.2): ip['list-style-type'] = rv(['disc', 'circle', 'square', 'decimal',
                                                     'none', 'lower-alpha', 'upper-roman'])
        if maybe(0.2): ip['float'] = rv(FLOAT_VALUES)
        items.append(make_element('li', ip, rv(TEXT_SAMPLES)))

    list_tag = rv(['ul', 'ol'])
    lp = css_props_block()
    if maybe(0.3): lp['list-style-position'] = rv(['inside', 'outside'])
    body = make_element(list_tag, lp, '\n'.join(items))

    # Orphan list items
    if maybe(0.3):
        body += '\n' + make_element('li', {}, 'Orphan item')

    return wrap_html(body)


def gen_flex_in_grid():
    """Generate flex containers inside grid cells and vice versa."""
    grid_props = {
        'display': 'grid',
        'grid-template-columns': rv(GRID_TEMPLATE_VALUES),
        'grid-template-rows': rv(GRID_TEMPLATE_VALUES),
        'width': rand_length(),
        'height': rand_length(),
    }
    if maybe(0.5): grid_props['gap'] = rv(['0', '5px', '20px'])

    children = []
    for _ in range(random.randint(3, 12)):
        cp = css_props_block()
        cp['display'] = rv(['flex', 'inline-flex'])
        cp['flex-direction'] = rv(FLEX_DIRECTION)
        cp['flex-wrap'] = rv(FLEX_WRAP)
        if maybe(0.5): cp['align-items'] = rv(ALIGN_VALUES)
        if maybe(0.5): cp['justify-content'] = rv(JUSTIFY_VALUES)
        if maybe(0.3): cp['grid-column'] = f'span {random.randint(1, 4)}'
        if maybe(0.3): cp['grid-row'] = f'span {random.randint(1, 3)}'

        inner = []
        for _ in range(random.randint(1, 8)):
            ip = css_props_block()
            ip['flex'] = rv(['0 1 auto', '1 0 0', '1 1 0%', '0 0 auto', '9999 0 0',
                              '0.001 9999 auto', '1 1 100%', '0 0 0'])
            if maybe(0.2): ip['position'] = rv(POSITION_VALUES)
            if maybe(0.2): ip['min-width'] = rand_length()
            if maybe(0.2): ip['max-width'] = rand_length()
            # nested grid inside flex inside grid
            if maybe(0.1):
                ip['display'] = 'grid'
                ip['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
                inner_inner = ''.join(make_element('div', css_props_block(), rv(TEXT_SAMPLES))
                                      for _ in range(random.randint(1, 4)))
                inner.append(make_element('div', ip, inner_inner))
            else:
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))

        children.append(make_element('div', cp, '\n'.join(inner)))

    body = make_element('div', grid_props, '\n'.join(children))
    return wrap_html(body)


def gen_table_in_flex():
    """Generate tables inside flex containers with conflicting sizing."""
    flex_props = {
        'display': 'flex',
        'flex-wrap': rv(FLEX_WRAP),
        'width': rand_length(),
        'align-items': rv(ALIGN_VALUES),
    }

    children = []
    for _ in range(random.randint(2, 6)):
        cp = css_props_block()
        cp['flex'] = rv(['0 1 auto', '1 0 0', '1 1 auto', '0 0 auto', '9999 1 0'])

        # table as flex child
        table_props = {}
        table_props['border-collapse'] = rv(['collapse', 'separate'])
        table_props['table-layout'] = rv(['auto', 'fixed'])
        if maybe(0.5): table_props['width'] = rand_length()

        rows = []
        for _ in range(random.randint(1, 5)):
            cells = []
            for _ in range(random.randint(1, 6)):
                cell_p = css_props_block()
                attrs = ''
                if maybe(0.3):
                    attrs += f' colspan="{random.choice([0, 2, 3, 5, 99])}"'
                if maybe(0.3):
                    attrs += f' rowspan="{random.choice([0, 2, 3, 5, 99])}"'
                cells.append(make_element('td', cell_p, rv(TEXT_SAMPLES), attrs))
            rows.append(make_element('tr', {}, ''.join(cells)))

        table = make_element('table', table_props, ''.join(rows))
        children.append(make_element('div', cp, table))

    body = make_element('div', flex_props, '\n'.join(children))
    return wrap_html(body)


def gen_contradiction():
    """Generate CSS with contradictory constraints that stress resolution."""
    children = []

    for _ in range(random.randint(3, 10)):
        props = {}
        contradiction = random.choice([
            'size', 'flex_basis', 'position', 'display',
            'overflow', 'float_flex', 'margins', 'grid_sizing',
        ])

        if contradiction == 'size':
            props['width'] = rv(['100px', '200px', '500px'])
            props['min-width'] = rv(['300px', '500px', '999px', '99999px'])
            props['max-width'] = rv(['0', '10px', '50px', '1px'])
            props['height'] = rv(['100px', '200px'])
            props['min-height'] = rv(['500px', '999px'])
            props['max-height'] = rv(['0', '10px', '1px'])

        elif contradiction == 'flex_basis':
            props['display'] = 'flex'
            inner = []
            for _ in range(random.randint(2, 6)):
                ip = {}
                ip['flex-basis'] = rv(['200%', '99999px', '-100px'])
                ip['flex-grow'] = rv(['0', '9999'])
                ip['flex-shrink'] = rv(['0', '0.001'])
                ip['min-width'] = rv(['99999px', '500px'])
                ip['max-width'] = rv(['0', '1px'])
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))
            children.append(make_element('div', props, '\n'.join(inner)))
            continue

        elif contradiction == 'position':
            props['display'] = rv(['inline', 'inline-block'])
            inner_p = {'position': 'absolute', 'top': '0', 'left': '0',
                      'width': '99999px', 'height': '99999px'}
            children.append(make_element('span', props,
                make_element('div', inner_p, rv(TEXT_SAMPLES))))
            continue

        elif contradiction == 'display':
            props['display'] = 'flex'
            inner = []
            for _ in range(random.randint(2, 5)):
                ip = {'display': rv(['table-cell', 'table-row', 'table-column',
                                     'table-caption', 'list-item', 'inline'])}
                ip.update(css_props_block())
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))
            children.append(make_element('div', props, '\n'.join(inner)))
            continue

        elif contradiction == 'overflow':
            props['display'] = 'inline'
            props['overflow'] = rv(['scroll', 'auto', 'hidden'])
            props['width'] = '100px'
            props['height'] = '100px'

        elif contradiction == 'float_flex':
            props['display'] = 'flex'
            inner = []
            for _ in range(random.randint(2, 5)):
                ip = {'float': rv(['left', 'right']), 'clear': rv(CLEAR_VALUES)}
                ip.update(css_props_block())
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))
            children.append(make_element('div', props, '\n'.join(inner)))
            continue

        elif contradiction == 'margins':
            props['display'] = 'flex'
            props['justify-content'] = rv(JUSTIFY_VALUES)
            props['align-items'] = rv(ALIGN_VALUES)
            inner = []
            for _ in range(random.randint(2, 5)):
                ip = {'margin': 'auto', 'margin-left': 'auto', 'margin-right': 'auto',
                      'margin-top': 'auto', 'margin-bottom': 'auto',
                      'align-self': rv(ALIGN_VALUES)}
                ip['width'] = rand_length()
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))
            children.append(make_element('div', props, '\n'.join(inner)))
            continue

        elif contradiction == 'grid_sizing':
            props['display'] = 'grid'
            props['grid-template-columns'] = rv(['50px 50px', '1fr', 'repeat(3, 30px)'])
            props['width'] = '100px'
            inner = []
            for _ in range(random.randint(2, 8)):
                ip = css_props_block()
                ip['min-width'] = rv(['99999px', '500px', '200%'])
                ip['grid-column'] = f'span {random.randint(1, 5)}'
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))
            children.append(make_element('div', props, '\n'.join(inner)))
            continue

        children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    body = '\n'.join(children)
    return wrap_html(body)


def gen_recursive_layout():
    """Generate deeply recursive layout mode nesting: flex->grid->table->flex->..."""
    depth = random.randint(4, 12)
    modes = ['flex', 'grid', 'table', 'block', 'inline-block']
    content = rv(TEXT_SAMPLES)

    for i in range(depth):
        mode = rv(modes)
        props = css_props_block()

        if mode == 'flex':
            props['display'] = 'flex'
            props['flex-direction'] = rv(FLEX_DIRECTION)
            props['flex-wrap'] = rv(FLEX_WRAP)
            if maybe(0.5): props['align-items'] = rv(ALIGN_VALUES)
            siblings = []
            for _ in range(random.randint(1, 4)):
                sp = {'flex': rv(['1', '0 1 auto', '1 0 0', '0 0 auto', '9999 0 0'])}
                sp.update(css_props_block())
                if maybe(0.3): sp['position'] = rv(POSITION_VALUES)
                siblings.append(make_element('div', sp, rv(TEXT_SAMPLES)))
            idx = random.randint(0, len(siblings) - 1)
            siblings[idx] = make_element('div',
                {'flex': rv(['1', '0 1 auto', '1 0 0'])}, content)
            content = '\n'.join(siblings)

        elif mode == 'grid':
            props['display'] = 'grid'
            props['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
            if maybe(0.5): props['gap'] = rv(['0', '5px', '10px'])
            siblings = [make_element('div', css_props_block(), rv(TEXT_SAMPLES))
                       for _ in range(random.randint(1, 4))]
            siblings.append(make_element('div', {}, content))
            random.shuffle(siblings)
            content = '\n'.join(siblings)

        elif mode == 'table':
            cell = make_element('td', css_props_block(), content)
            extra_cells = ''.join(make_element('td', css_props_block(), rv(TEXT_SAMPLES))
                                  for _ in range(random.randint(0, 3)))
            row = make_element('tr', {}, cell + extra_cells)
            extra_rows = ''.join(
                make_element('tr', {},
                    ''.join(make_element('td', {}, rv(TEXT_SAMPLES))
                            for _ in range(random.randint(1, 4))))
                for _ in range(random.randint(0, 3)))
            props['border-collapse'] = rv(['collapse', 'separate'])
            if maybe(0.3): props['table-layout'] = rv(['auto', 'fixed'])
            content = make_element('table', props, row + extra_rows)
            continue

        elif mode == 'inline-block':
            props['display'] = 'inline-block'

        else:
            props['display'] = 'block'

        content = make_element('div', props, content)

    container = {'width': rv(['400px', '800px', '1200px', '50%', 'auto']),
                 'position': 'relative'}
    body = make_element('div', container, content)
    return wrap_html(body)


def gen_float_clear_chain():
    """Generate complex float + clear interaction chains."""
    children = []
    for i in range(random.randint(10, 40)):
        props = css_props_block()
        kind = random.choice(['float-l', 'float-r', 'clear', 'bfc', 'normal',
                               'abs', 'float-zero', 'inline-float'])

        if kind == 'float-l':
            props['float'] = 'left'
            props['width'] = rv(['50px', '100px', '200px', '33%', '50%', '0', '99999px'])
            props['height'] = rv(['20px', '50px', '100px', '0', '99999px'])
        elif kind == 'float-r':
            props['float'] = 'right'
            props['width'] = rv(['50px', '100px', '200px', '33%', '50%'])
            props['height'] = rv(['20px', '50px', '100px'])
        elif kind == 'clear':
            props['clear'] = rv(['left', 'right', 'both'])
            if maybe(0.3): props['float'] = rv(['left', 'right'])
        elif kind == 'bfc':
            props['overflow'] = rv(['hidden', 'auto', 'scroll'])
            props['width'] = rand_length()
        elif kind == 'abs':
            props['position'] = 'absolute'
            props['top'] = rand_length()
            props['left'] = rand_length()
            props['width'] = rand_length()
        elif kind == 'float-zero':
            props['float'] = rv(['left', 'right'])
            props['width'] = rv(['0', '0px', '0.001px'])
            props['height'] = rv(['0', '0px', '100px'])
        elif kind == 'inline-float':
            props['float'] = rv(['left', 'right'])
            props['display'] = rv(['inline', 'inline-block'])

        children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    container = {'width': rv(['400px', '800px', '1200px']), 'position': 'relative'}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_aspect_ratio_stress():
    """Generate elements with aspect-ratio and conflicting size constraints."""
    children = []
    for _ in range(random.randint(3, 12)):
        props = css_props_block()
        props['aspect-ratio'] = rv(['1', '1/1', '16/9', '4/3', '0.5', '2',
                                     '1/0', '0/1', '99999/1', '1/99999',
                                     'auto', 'auto 16/9', '16/9 auto'])
        if maybe(0.7): props['width'] = rand_length()
        if maybe(0.5): props['height'] = rand_length()
        if maybe(0.3): props['min-width'] = rand_length()
        if maybe(0.3): props['min-height'] = rand_length()
        if maybe(0.3): props['max-width'] = rand_length()
        if maybe(0.3): props['max-height'] = rand_length()
        if maybe(0.2): props['position'] = rv(POSITION_VALUES)
        if maybe(0.2): props['display'] = rv(DISPLAY_VALUES)
        children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    flex_props = {'display': 'flex', 'flex-wrap': rv(FLEX_WRAP), 'width': '800px'}
    body = make_element('div', flex_props, '\n'.join(children))
    return wrap_html(body)


def gen_writing_mode_stress():
    """Generate content with mixed writing modes and directions."""
    children = []
    wm_values = ['horizontal-tb', 'vertical-rl', 'vertical-lr']
    dir_values = ['ltr', 'rtl']

    for _ in range(random.randint(3, 10)):
        props = css_props_block()
        props['writing-mode'] = rv(wm_values)
        props['direction'] = rv(dir_values)
        if maybe(0.3): props['text-orientation'] = rv(['mixed', 'upright', 'sideways'])
        if maybe(0.3): props['display'] = rv(DISPLAY_VALUES)

        inner_props = css_props_block()
        inner_props['writing-mode'] = rv(wm_values)
        inner_props['direction'] = rv(dir_values)

        inner = make_element('div', inner_props,
            rv(TEXT_SAMPLES) + make_element('span', {}, rv(TEXT_SAMPLES)))
        children.append(make_element('div', props, inner))

    body = '\n'.join(children)
    return wrap_html(body)


def gen_transform_stress():
    """Generate transformed elements that interact with layout."""
    transforms = [
        'none', 'translate(0, 0)', 'translate(99999px, 99999px)',
        'translate(-99999px, -99999px)', 'translate(50%, 50%)',
        'scale(0)', 'scale(-1)', 'scale(0.001)', 'scale(9999)',
        'rotate(0deg)', 'rotate(90deg)', 'rotate(360deg)',
        'skew(45deg)', 'matrix(1,0,0,1,0,0)',
        'translate3d(0,0,0)', 'perspective(100px)',
    ]

    children = []
    for _ in range(random.randint(3, 10)):
        props = css_props_block()
        props['transform'] = rv(transforms)
        if maybe(0.3): props['transform-origin'] = rv(['center', '0 0', '100% 100%',
                                                        'top left', '-50px -50px'])
        if maybe(0.3): props['position'] = rv(POSITION_VALUES)
        if maybe(0.3): props['display'] = rv(DISPLAY_VALUES)
        if maybe(0.2): props['overflow'] = rv(OVERFLOW_VALUES)
        if maybe(0.2): props['will-change'] = 'transform'

        if maybe(0.3):
            inner_p = {'position': 'absolute', 'top': rand_length(),
                      'left': rand_length(), 'width': rand_length()}
            inner = make_element('div', inner_p, rv(TEXT_SAMPLES))
            children.append(make_element('div', props, inner + rv(TEXT_SAMPLES)))
        else:
            children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    container = {'position': 'relative', 'width': '800px', 'height': '600px'}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_multi_column():
    """Generate multi-column layout stress tests."""
    props = css_props_block()
    props['column-count'] = str(random.choice([1, 2, 3, 5, 10, 100, 9999]))
    if maybe(0.5): props['column-width'] = rv(['0', '1px', '50px', '100px', '99999px', 'auto'])
    if maybe(0.5): props['column-gap'] = rv(['0', '1px', '10px', '50px', '99999px', 'normal'])
    if maybe(0.3): props['column-rule'] = rand_border()
    if maybe(0.3): props['column-fill'] = rv(['auto', 'balance'])

    parts = []
    for _ in range(random.randint(3, 15)):
        cp = css_props_block()
        if maybe(0.2): cp['break-inside'] = rv(['auto', 'avoid'])
        if maybe(0.2): cp['break-before'] = rv(['auto', 'column', 'avoid'])
        if maybe(0.2): cp['break-after'] = rv(['auto', 'column', 'avoid'])
        if maybe(0.2): cp['column-span'] = rv(['none', 'all'])
        if maybe(0.1): cp['float'] = rv(FLOAT_VALUES)
        if maybe(0.1): cp['position'] = rv(POSITION_VALUES)
        parts.append(make_element('div', cp, rv(TEXT_SAMPLES)))

    body = make_element('div', props, '\n'.join(parts))
    return wrap_html(body)


def gen_css_var_stress():
    """Generate HTML using CSS custom properties in stress patterns."""
    css = '''\n:root {\n    --w: ''' + rand_length() + ''';\n    --h: ''' + rand_length() + ''';\n    --m: ''' + rand_margin() + ''';\n    --d: ''' + rv(DISPLAY_VALUES) + ''';\n    --flex: ''' + rv(['1', '0 1 auto', '0 0 0', '9999 0 0']) + ''';\n    --cols: ''' + rv(GRID_TEMPLATE_VALUES) + ''';\n}\n.a { display: var(--d); width: var(--w); height: var(--h); margin: var(--m); }\n.b { display: flex; }\n.b > * { flex: var(--flex); width: var(--w); }\n.c { display: grid; grid-template-columns: var(--cols); }\n'''

    children = []
    for cls in ['a', 'b', 'c']:
        inner = ''.join(make_element('div', {'class': cls},
            rv(TEXT_SAMPLES)) for _ in range(random.randint(2, 6)))
        children.append(f'<div class="{cls}">{inner}</div>')

    body = '\n'.join(children)
    return wrap_html(body, extra_css=css)


def gen_form_elements():
    """Generate form/replaced elements with extreme dimensions and layout interactions."""
    form_tags = [
        ('<input type="text">', ''),
        ('<input type="checkbox">', ''),
        ('<input type="radio">', ''),
        ('<input type="submit" value="Submit">', ''),
        ('<input type="range">', ''),
        ('<input type="number">', ''),
        ('<textarea>', '</textarea>'),
        ('<select><option>A</option><option>B</option></select>', ''),
        ('<button>Click</button>', ''),
        ('<progress value="50" max="100">', '</progress>'),
        ('<meter value="0.7">', '</meter>'),
    ]
    children = []
    for _ in range(random.randint(5, 20)):
        tag_open, tag_close = rv(form_tags)
        props = css_props_block()
        if maybe(0.4): props['display'] = rv(DISPLAY_VALUES)
        if maybe(0.3): props['position'] = rv(POSITION_VALUES)
        if maybe(0.3): props['float'] = rv(FLOAT_VALUES)
        if maybe(0.3): props['vertical-align'] = rv(VERTICAL_ALIGN)
        style = css_to_style(props)
        # inject style into the tag
        el = tag_open.replace('>', f'{style}>', 1)
        if tag_close:
            el += rv(TEXT_SAMPLES) + tag_close
        # sometimes wrap in a container with conflicting layout
        if maybe(0.3):
            wp = {}
            wp['display'] = rv(['flex', 'grid', 'inline-flex', 'inline-grid', 'table-cell'])
            wp['width'] = rand_length()
            wp['height'] = rand_length()
            el = make_element('div', wp, el)
        children.append(el)

    container = {'width': rv(['400px', '800px', '0', 'auto', '100%'])}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_percentage_chain():
    """Generate deep chains of percentage-based sizing (classic CSS edge case)."""
    # percentage heights only resolve against fixed-height parents
    depth = random.randint(5, 15)
    content = make_element('div', {
        'width': '100%', 'height': '100%',
        'background': rand_color(),
    }, rv(TEXT_SAMPLES))

    for i in range(depth):
        props = {}
        # mix of fixed and percentage heights to create resolution edge cases
        if maybe(0.6):
            props['height'] = rv(['auto', '50%', '100%', '200%', '0%', 'min-content', 'max-content'])
        else:
            props['height'] = rv(['100px', '200px', '0', 'auto'])
        props['width'] = rv(['50%', '100%', '200%', 'auto', '0%', 'min-content'])
        if maybe(0.3): props['min-height'] = rv(['0', '50%', '100%', '200%', '99999px'])
        if maybe(0.3): props['max-height'] = rv(['0', '50%', '100%', '200%', 'none'])
        if maybe(0.2): props['position'] = rv(POSITION_VALUES)
        if maybe(0.2): props['display'] = rv(['block', 'flex', 'grid', 'inline-block'])
        if maybe(0.1): props['overflow'] = rv(OVERFLOW_VALUES)
        content = make_element('div', props, content)

    # sometimes the outermost has no explicit height (so all %'s collapse)
    outer = {}
    if maybe(0.5):
        outer['height'] = rv(['500px', '100vh', '0'])
    outer['width'] = '800px'
    body = make_element('div', outer, content)
    return wrap_html(body)


def gen_display_contents():
    """Generate display:contents trees that stress skip-over logic."""
    children = []
    for _ in range(random.randint(3, 12)):
        props = css_props_block()
        props['display'] = 'contents'
        # display:contents elements should pass through children
        inner = []
        for _ in range(random.randint(1, 5)):
            ip = css_props_block()
            if maybe(0.3): ip['display'] = rv(['contents', 'block', 'flex', 'grid', 'inline'])
            if maybe(0.3): ip['position'] = rv(POSITION_VALUES)
            if maybe(0.2): ip['float'] = rv(FLOAT_VALUES)
            inner.append(make_element(rv(TAGS_ALL), ip, rv(TEXT_SAMPLES)))
        children.append(make_element('div', props, '\n'.join(inner)))

    # nested contents chains
    for _ in range(random.randint(1, 5)):
        depth = random.randint(3, 10)
        inner = rv(TEXT_SAMPLES)
        for d in range(depth):
            inner = make_element('div', {'display': 'contents'}, inner)
        children.append(inner)

    # contents inside flex/grid
    container_props = {}
    container_props['display'] = rv(['flex', 'grid', 'block'])
    if container_props['display'] == 'grid':
        container_props['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
    if maybe(0.5): container_props['width'] = rand_length()
    body = make_element('div', container_props, '\n'.join(children))
    return wrap_html(body)


def gen_empty_elements():
    """Generate empty elements with various display modes, borders, padding."""
    children = []
    for _ in range(random.randint(10, 40)):
        props = css_props_block()
        props['display'] = rv(DISPLAY_VALUES)
        if maybe(0.5): props['border'] = rand_border()
        if maybe(0.5): props['padding'] = f'{rand_padding()} {rand_padding()} {rand_padding()} {rand_padding()}'
        if maybe(0.3): props['margin'] = f'{rand_margin()} {rand_margin()} {rand_margin()} {rand_margin()}'
        if maybe(0.3): props['min-width'] = rand_length()
        if maybe(0.3): props['min-height'] = rand_length()
        if maybe(0.2): props['position'] = rv(POSITION_VALUES)
        if maybe(0.2): props['float'] = rv(FLOAT_VALUES)
        # strictly empty - no text content
        children.append(make_element(rv(TAGS_ALL), props, ''))

    container = {'width': rv(['400px', '800px', '0', 'auto'])}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_bfc_creation():
    """Generate scenarios that trigger Block Formatting Context creation."""
    bfc_triggers = [
        {'overflow': 'hidden'}, {'overflow': 'auto'}, {'overflow': 'scroll'},
        {'display': 'flow-root'}, {'display': 'inline-block'},
        {'float': 'left'}, {'float': 'right'},
        {'position': 'absolute'}, {'position': 'fixed'},
        {'display': 'flex'}, {'display': 'grid'},
        {'display': 'table-cell'}, {'display': 'table-caption'},
        {'contain': 'layout'}, {'contain': 'paint'}, {'contain': 'strict'},
        {'column-count': '2'},
    ]
    children = []
    for _ in range(random.randint(5, 15)):
        trigger = rv(bfc_triggers).copy()
        trigger.update(css_props_block())
        # put floats and margin-collapsing inside to test BFC isolation
        inner = []
        for _ in range(random.randint(1, 5)):
            ip = {}
            kind = rv(['float', 'margin', 'abs', 'normal'])
            if kind == 'float':
                ip['float'] = rv(['left', 'right'])
                ip['width'] = rv(['50px', '100px', '50%'])
            elif kind == 'margin':
                ip['margin-top'] = rv(['-50px', '50px', '-9999px', 'auto'])
                ip['margin-bottom'] = rv(['-50px', '50px', '-9999px', 'auto'])
            elif kind == 'abs':
                ip['position'] = 'absolute'
                ip['top'] = rand_length()
                ip['left'] = rand_length()
            inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))
        children.append(make_element('div', trigger, '\n'.join(inner)))

    container = {'width': '800px', 'position': 'relative'}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_inline_block_baseline():
    """Stress inline-block baseline alignment edge cases."""
    # baseline alignment with different content types is a classic crash area
    parts = []
    for _ in range(random.randint(5, 20)):
        props = {'display': 'inline-block'}
        props['vertical-align'] = rv(VERTICAL_ALIGN)
        if maybe(0.5): props['width'] = rand_length()
        if maybe(0.5): props['height'] = rand_length()
        if maybe(0.3): props['overflow'] = rv(OVERFLOW_VALUES)
        if maybe(0.3): props['border'] = rand_border()
        if maybe(0.3): props['padding'] = rand_padding()
        if maybe(0.3): props['line-height'] = rv(LINE_HEIGHTS)
        if maybe(0.3): props['font-size'] = rv(FONT_SIZES)

        content_type = rv(['text', 'empty', 'nested', 'img', 'overflow'])
        if content_type == 'text':
            text = rv(TEXT_SAMPLES)
        elif content_type == 'empty':
            text = ''
        elif content_type == 'nested':
            ip = {'display': 'inline-block', 'vertical-align': rv(VERTICAL_ALIGN)}
            ip['font-size'] = rv(FONT_SIZES)
            text = make_element('span', ip, rv(TEXT_SAMPLES))
        elif content_type == 'img':
            text = '<img src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7" style="width:20px;height:20px;">'
        else:
            props['overflow'] = 'hidden'
            props['height'] = rv(['20px', '50px', '0'])
            text = rv(TEXT_SAMPLES)[:200] if maybe(0.5) else ''

        parts.append(make_element('span', props, text))

    container = {'width': rv(['400px', '800px', '1200px', '0', 'auto']),
                 'line-height': rv(LINE_HEIGHTS),
                 'font-size': rv(FONT_SIZES)}
    body = make_element('div', container, '\n'.join(parts))
    return wrap_html(body)


def gen_table_caption():
    """Stress table caption layout in various containers."""
    children = []
    for _ in range(random.randint(2, 6)):
        tp = css_props_block()
        tp['border-collapse'] = rv(['collapse', 'separate'])
        if maybe(0.5): tp['table-layout'] = rv(['auto', 'fixed'])
        if maybe(0.5): tp['width'] = rand_length()

        caption_props = css_props_block()
        caption_props['caption-side'] = rv(['top', 'bottom'])
        if maybe(0.3): caption_props['text-align'] = rv(['left', 'center', 'right'])

        rows = []
        for _ in range(random.randint(1, 4)):
            cells = [make_element('td', css_props_block(), rv(TEXT_SAMPLES))
                     for _ in range(random.randint(1, 5))]
            rows.append(make_element('tr', {}, ''.join(cells)))

        caption = make_element('caption', caption_props, rv(TEXT_SAMPLES))
        table = make_element('table', tp, caption + ''.join(rows))

        # put table in various containers
        wrapper_display = rv(['block', 'flex', 'grid', 'inline-block', 'inline-flex'])
        wrapper = {'display': wrapper_display, 'width': rand_length()}
        if wrapper_display in ('grid', 'inline-grid'):
            wrapper['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
        children.append(make_element('div', wrapper, table))

    body = '\n'.join(children)
    return wrap_html(body)


def gen_visibility_collapse():
    """Stress visibility:collapse in table rows/columns and other elements."""
    # table with collapsed rows
    tp = css_props_block()
    tp['border-collapse'] = rv(['collapse', 'separate'])
    if maybe(0.5): tp['table-layout'] = rv(['auto', 'fixed'])
    tp['width'] = rv(['400px', '800px', 'auto', '100%'])

    rows = []
    num_rows = random.randint(3, 10)
    num_cols = random.randint(2, 6)
    for r in range(num_rows):
        rp = {}
        if maybe(0.3): rp['visibility'] = rv(['visible', 'hidden', 'collapse'])
        cells = []
        for c in range(num_cols):
            cp = css_props_block()
            if maybe(0.2): cp['visibility'] = rv(['visible', 'hidden', 'collapse'])
            attrs = ''
            if maybe(0.15):
                attrs += f' colspan="{random.choice([2, 3, 5])}"'
            if maybe(0.15):
                attrs += f' rowspan="{random.choice([2, 3, 5])}"'
            cells.append(make_element('td', cp, rv(TEXT_SAMPLES), attrs))
        rows.append(make_element('tr', rp, ''.join(cells)))

    # colgroup with visibility:collapse
    colgroup = ''
    if maybe(0.5):
        cols = []
        for c in range(num_cols):
            vis = rv(['visible', 'collapse']) if maybe(0.5) else 'visible'
            cols.append(f'<col style="visibility:{vis};">')
        colgroup = f'<colgroup>{"".join(cols)}</colgroup>'

    table = make_element('table', tp, colgroup + ''.join(rows))

    # also test collapse outside tables (should act like hidden)
    extra = []
    for _ in range(random.randint(1, 5)):
        ep = css_props_block()
        ep['visibility'] = 'collapse'
        ep['display'] = rv(DISPLAY_VALUES)
        extra.append(make_element('div', ep, rv(TEXT_SAMPLES)))

    body = table + '\n' + '\n'.join(extra)
    return wrap_html(body)


def gen_zero_dimension_stress():
    """Generate elements with zero/near-zero dimensions combined with content/padding/border."""
    children = []
    for _ in range(random.randint(5, 20)):
        props = {}
        props['width'] = rv(['0', '0px', '0.001px', '-1px'])
        props['height'] = rv(['0', '0px', '0.001px', '-1px'])
        # but add content-generating properties
        if maybe(0.5): props['padding'] = rv(['10px', '50px', '100px', '50%'])
        if maybe(0.5): props['border'] = rand_border()
        if maybe(0.3): props['min-width'] = rv(['0', '100px', '50%'])
        if maybe(0.3): props['min-height'] = rv(['0', '100px', '50%'])
        if maybe(0.3): props['overflow'] = rv(OVERFLOW_VALUES)
        props['display'] = rv(DISPLAY_VALUES)
        if maybe(0.2): props['position'] = rv(POSITION_VALUES)
        if maybe(0.2): props['float'] = rv(FLOAT_VALUES)

        inner = rv(TEXT_SAMPLES) if maybe(0.7) else ''
        # sometimes nest another zero-size in zero-size
        if maybe(0.3):
            inner += make_element('div', {
                'width': '0', 'height': '0',
                'position': rv(['absolute', 'relative']),
                'overflow': rv(OVERFLOW_VALUES),
            }, rv(TEXT_SAMPLES))

        children.append(make_element('div', props, inner))

    container = {'width': '800px', 'display': rv(['block', 'flex', 'grid'])}
    if container['display'] == 'grid':
        container['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_sticky_position():
    """Generate sticky positioning edge cases."""
    children = []
    # scrollable container with multiple sticky elements
    container_props = {
        'width': rv(['400px', '800px', '100%']),
        'height': rv(['200px', '500px', '100vh']),
        'overflow': rv(['auto', 'scroll', 'hidden']),
        'position': 'relative',
    }

    for _ in range(random.randint(5, 20)):
        props = css_props_block()
        if maybe(0.4):
            props['position'] = 'sticky'
            props['top'] = rv(['0', '10px', '-10px', '50px', '100%', 'auto'])
            if maybe(0.3): props['bottom'] = rv(['0', '10px', 'auto'])
            if maybe(0.2): props['left'] = rv(['0', '10px', 'auto'])
            if maybe(0.2): props['right'] = rv(['0', '10px', 'auto'])
        elif maybe(0.2):
            props['position'] = rv(POSITION_VALUES)
        if maybe(0.3): props['height'] = rv(['50px', '100px', '200px', '0'])
        if maybe(0.3): props['display'] = rv(['block', 'flex', 'grid', 'inline-block'])

        # nested sticky inside sticky
        inner = rv(TEXT_SAMPLES)
        if maybe(0.2):
            ip = {'position': 'sticky', 'top': rv(['0', '20px', '-10px'])}
            ip['height'] = rv(['30px', '50px', '100px'])
            inner = make_element('div', ip, rv(TEXT_SAMPLES))

        children.append(make_element('div', props, inner))

    body = make_element('div', container_props, '\n'.join(children))
    return wrap_html(body)


def gen_pseudo_element_stress():
    """Generate pseudo-element ::before/::after stress tests via <style> blocks."""
    css_rules = []
    children = []
    for i in range(random.randint(5, 15)):
        cls = f'p{i}'
        props = css_props_block()
        if maybe(0.3): props['display'] = rv(DISPLAY_VALUES)
        if maybe(0.3): props['position'] = rv(POSITION_VALUES)

        # before pseudo
        before_display = rv(['block', 'inline', 'inline-block', 'flex', 'grid', 'none', 'table'])
        before_content = rv(['""', '"Hello"', '" "', '"\\a"', 'counter(c)',
                             '"' + 'x' * random.choice([1, 100, 1000]) + '"'])
        before_props = [f'content: {before_content}', f'display: {before_display}']
        if maybe(0.5): before_props.append(f'width: {rand_length()}')
        if maybe(0.5): before_props.append(f'height: {rand_length()}')
        if maybe(0.3): before_props.append(f'position: {rv(POSITION_VALUES)}')
        if maybe(0.3): before_props.append(f'float: {rv(FLOAT_VALUES)}')

        # after pseudo
        after_display = rv(['block', 'inline', 'inline-block', 'none', 'table-cell'])
        after_content = rv(['""', '"World"', 'counter(c)', '"' + 'y' * random.choice([1, 50]) + '"'])
        after_props = [f'content: {after_content}', f'display: {after_display}']
        if maybe(0.5): after_props.append(f'width: {rand_length()}')
        if maybe(0.3): after_props.append(f'float: {rv(FLOAT_VALUES)}')
        if maybe(0.3): after_props.append(f'clear: {rv(CLEAR_VALUES)}')

        css_rules.append(f'.{cls}::before {{ {"; ".join(before_props)} }}')
        css_rules.append(f'.{cls}::after {{ {"; ".join(after_props)} }}')

        children.append(f'<div class="{cls}"{css_to_style(props)}>{rv(TEXT_SAMPLES)}</div>')

    extra_css = '\n'.join(css_rules)
    body = '\n'.join(children)
    return wrap_html(body, extra_css=extra_css)


def gen_overflow_interaction():
    """Generate overflow clipping edge cases with positioned children."""
    children = []
    for _ in range(random.randint(3, 10)):
        outer = css_props_block()
        outer['overflow'] = rv(OVERFLOW_VALUES)
        if maybe(0.5): outer['overflow-x'] = rv(OVERFLOW_VALUES)
        if maybe(0.5): outer['overflow-y'] = rv(OVERFLOW_VALUES)
        outer['width'] = rv(['100px', '200px', '0', 'auto'])
        outer['height'] = rv(['100px', '200px', '0', 'auto'])
        outer['position'] = 'relative'

        inner = []
        for _ in range(random.randint(1, 5)):
            ip = css_props_block()
            ip['position'] = rv(['absolute', 'fixed', 'relative', 'static'])
            ip['width'] = rv(['200%', '99999px', '0', 'auto'])
            ip['height'] = rv(['200%', '99999px', '0', 'auto'])
            if ip['position'] in ('absolute', 'fixed'):
                ip['top'] = rv(['-50px', '0', '50px', '-99999px'])
                ip['left'] = rv(['-50px', '0', '50px', '-99999px'])

            # nested overflow inside overflow
            if maybe(0.3):
                nip = {'overflow': rv(OVERFLOW_VALUES), 'width': rand_length(),
                       'height': rand_length()}
                inner.append(make_element('div', ip,
                    make_element('div', nip, rv(TEXT_SAMPLES))))
            else:
                inner.append(make_element('div', ip, rv(TEXT_SAMPLES)))

        children.append(make_element('div', outer, '\n'.join(inner)))

    body = '\n'.join(children)
    return wrap_html(body)


def gen_table_in_everything():
    """Generate tables nested in every possible layout context."""
    contexts = ['flex', 'grid', 'inline-flex', 'inline-grid', 'inline-block',
                'block', 'table-cell', 'contents', 'flow-root']
    children = []

    for ctx in contexts:
        wp = css_props_block()
        wp['display'] = ctx
        if ctx in ('grid', 'inline-grid'):
            wp['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
        if ctx in ('flex', 'inline-flex'):
            wp['flex-wrap'] = rv(FLEX_WRAP)
        wp['width'] = rand_length()

        # table with various stressors
        tp = {}
        tp['border-collapse'] = rv(['collapse', 'separate'])
        tp['table-layout'] = rv(['auto', 'fixed'])
        tp['width'] = rv(['100%', 'auto', '200%', '0', '99999px'])

        rows = []
        for _ in range(random.randint(1, 4)):
            cells = []
            for _ in range(random.randint(1, 5)):
                cp = css_props_block()
                attrs = ''
                if maybe(0.3): attrs += f' colspan="{random.choice([0, 2, 5, 99])}"'
                if maybe(0.3): attrs += f' rowspan="{random.choice([0, 2, 5, 99])}"'
                cells.append(make_element('td', cp, rv(TEXT_SAMPLES), attrs))
            rows.append(make_element('tr', {}, ''.join(cells)))

        table = make_element('table', tp, ''.join(rows))
        children.append(make_element('div', wp, table))

    body = '\n'.join(children)
    return wrap_html(body)


def gen_flex_overflow_minmax():
    """Stress flex layout with min/max constraints and overflow."""
    outer = {
        'display': 'flex',
        'flex-direction': rv(FLEX_DIRECTION),
        'flex-wrap': rv(FLEX_WRAP),
        'width': rv(['0', '100px', '400px', '800px', '99999px']),
        'height': rv(['0', '100px', '400px', 'auto']),
        'overflow': rv(OVERFLOW_VALUES),
    }
    if maybe(0.5): outer['align-items'] = rv(ALIGN_VALUES)

    children = []
    for _ in range(random.randint(3, 15)):
        cp = {}
        # conflicting flex + min/max
        cp['flex-grow'] = rv(['0', '0.001', '1', '9999'])
        cp['flex-shrink'] = rv(['0', '0.001', '1', '9999'])
        cp['flex-basis'] = rv(['0', '0%', 'auto', '100%', '200%', '99999px',
                                'min-content', 'max-content'])
        cp['min-width'] = rv(['0', '99999px', 'min-content', 'max-content', '200%'])
        cp['max-width'] = rv(['0', '1px', 'none', '50%', 'min-content'])
        cp['min-height'] = rv(['0', '99999px', '200%', 'auto'])
        cp['max-height'] = rv(['0', '1px', 'none', '50%'])
        if maybe(0.3): cp['overflow'] = rv(OVERFLOW_VALUES)
        if maybe(0.3): cp['position'] = rv(POSITION_VALUES)

        children.append(make_element('div', cp, rv(TEXT_SAMPLES)))

    body = make_element('div', outer, '\n'.join(children))
    return wrap_html(body)


def gen_grid_overlap_placement():
    """Stress grid with overlapping explicit placements and negative line numbers."""
    outer = {
        'display': 'grid',
        'grid-template-columns': rv(['repeat(4, 100px)', 'repeat(6, 1fr)',
                                      '100px 1fr 100px', 'repeat(3, minmax(0, 1fr))']),
        'grid-template-rows': rv(['repeat(4, 100px)', 'auto auto auto',
                                   'repeat(3, minmax(50px, auto))']),
        'width': rv(['400px', '800px', '0', 'auto']),
        'height': rv(['400px', 'auto', '0']),
    }
    if maybe(0.5): outer['gap'] = rv(['0', '5px', '20px', '99999px'])

    children = []
    for _ in range(random.randint(5, 20)):
        cp = css_props_block()
        # explicit placement with overlaps
        if maybe(0.6):
            cs = random.randint(-5, 8)
            ce = cs + random.randint(-3, 6)
            cp['grid-column'] = f'{cs} / {ce}'
        if maybe(0.6):
            rs = random.randint(-5, 8)
            re = rs + random.randint(-3, 6)
            cp['grid-row'] = f'{rs} / {re}'
        if maybe(0.2): cp['grid-column'] = f'span {random.choice([0, 1, 5, 99, 9999])}'
        if maybe(0.2): cp['grid-row'] = f'span {random.choice([0, 1, 5, 99, 9999])}'
        if maybe(0.2): cp['position'] = rv(POSITION_VALUES)
        if maybe(0.2): cp['z-index'] = str(random.randint(-99, 99))

        children.append(make_element('div', cp, rv(TEXT_SAMPLES)))

    body = make_element('div', outer, '\n'.join(children))
    return wrap_html(body)


def gen_whitespace_stress():
    """Stress white-space handling edge cases with various break scenarios."""
    ws_modes = ['normal', 'nowrap', 'pre', 'pre-wrap', 'pre-line', 'break-spaces']
    children = []

    for _ in range(random.randint(3, 10)):
        props = css_props_block()
        props['white-space'] = rv(ws_modes)
        props['width'] = rv(['0', '50px', '100px', '400px', 'auto', 'min-content', 'max-content'])
        if maybe(0.3): props['word-break'] = rv(WORD_BREAK)
        if maybe(0.3): props['overflow-wrap'] = rv(['normal', 'break-word', 'anywhere'])
        if maybe(0.3): props['text-overflow'] = rv(['clip', 'ellipsis'])
        if maybe(0.3): props['overflow'] = rv(OVERFLOW_VALUES)
        if maybe(0.2): props['hyphens'] = rv(['none', 'manual', 'auto'])

        # tricky whitespace content
        text_choices = [
            '\t\t\t\n\n\n   ',
            'word\tword\tword',
            '\n\n\nline1\nline2\nline3\n\n\n',
            ' '.join(['w'] * 500),
            'a\u00A0b\u00A0c',  # non-breaking spaces
            'a\u200Bb\u200Bc',  # zero-width spaces
            'word ' * 200 + '\n' * 50 + 'word ' * 200,
            '\r\n' * 100,
            '  leading  trailing  ',
            '' ,  # empty
        ]
        text = rv(text_choices)
        children.append(make_element('div', props, text))

    body = '\n'.join(children)
    return wrap_html(body)


def gen_svg_inline_stress():
    """Generate inline SVG elements with adversarial shapes, transforms, and viewBox edge cases."""
    svg_shapes = []
    for _ in range(random.randint(3, 15)):
        shape = rv(['rect', 'circle', 'ellipse', 'line', 'polyline', 'polygon', 'path', 'text'])
        if shape == 'rect':
            x = rv(['0', '-100', '50%', '99999'])
            y = rv(['0', '-100', '50%', '99999'])
            w = rv(['0', '1', '100', '99999', '-1', '100%'])
            h = rv(['0', '1', '100', '99999', '-1', '100%'])
            rx = rv(['0', '10', '50%', '99999'])
            svg_shapes.append(f'<rect x="{x}" y="{y}" width="{w}" height="{h}" rx="{rx}" fill="{rand_color()}"/>')
        elif shape == 'circle':
            cx = rv(['0', '50', '-50', '99999', '50%'])
            cy = rv(['0', '50', '-50', '99999', '50%'])
            r = rv(['0', '1', '50', '99999', '-1', '0.001'])
            svg_shapes.append(f'<circle cx="{cx}" cy="{cy}" r="{r}" fill="{rand_color()}"/>')
        elif shape == 'ellipse':
            cx = rv(['0', '50', '-50', '99999'])
            cy = rv(['0', '50', '-50', '99999'])
            erx = rv(['0', '1', '50', '99999', '-1'])
            ery = rv(['0', '1', '50', '99999', '-1'])
            svg_shapes.append(f'<ellipse cx="{cx}" cy="{cy}" rx="{erx}" ry="{ery}" fill="{rand_color()}"/>')
        elif shape == 'line':
            x1 = rv(['0', '-99999', '99999'])
            y1 = rv(['0', '-99999', '99999'])
            x2 = rv(['0', '-99999', '99999', '100'])
            y2 = rv(['0', '-99999', '99999', '100'])
            svg_shapes.append(f'<line x1="{x1}" y1="{y1}" x2="{x2}" y2="{y2}" stroke="{rand_color()}" stroke-width="{rv(["0", "1", "99999"])}"/>')
        elif shape == 'path':
            d = rv([
                'M0,0', 'M0,0 L100,100', 'M0,0 Q50,50 100,0',
                'M0,0 C0,0 50,50 100,0', 'M0,0 Z', '',
                'M0,0 L99999,99999 L-99999,0 Z',
                'M0,0 A50,50 0 1,1 100,0', 'M0 0',
            ])
            svg_shapes.append(f'<path d="{d}" fill="{rand_color()}" stroke="{rand_color()}"/>')
        elif shape == 'text':
            x = rv(['0', '50', '-100', '99999'])
            y = rv(['0', '50', '-100', '99999'])
            fs = rv(['0', '1', '12', '99999', 'inherit'])
            svg_shapes.append(f'<text x="{x}" y="{y}" font-size="{fs}" fill="{rand_color()}">{rv(TEXT_SAMPLES[:8])}</text>')
        elif shape == 'polyline':
            pts = ' '.join(f'{random.randint(-999, 9999)},{random.randint(-999, 9999)}'
                          for _ in range(random.randint(2, 20)))
            svg_shapes.append(f'<polyline points="{pts}" fill="none" stroke="{rand_color()}"/>')
        elif shape == 'polygon':
            pts = ' '.join(f'{random.randint(-999, 9999)},{random.randint(-999, 9999)}'
                          for _ in range(random.randint(3, 15)))
            svg_shapes.append(f'<polygon points="{pts}" fill="{rand_color()}"/>')

    vb_x = rv(['0', '-100', '-99999'])
    vb_y = rv(['0', '-100', '-99999'])
    vb_w = rv(['100', '0', '1', '99999', '-1', '0.001'])
    vb_h = rv(['100', '0', '1', '99999', '-1', '0.001'])
    viewBox = f'{vb_x} {vb_y} {vb_w} {vb_h}'

    svg_style = css_props_block()
    if maybe(0.3): svg_style['overflow'] = rv(OVERFLOW_VALUES)
    style_str = css_to_style(svg_style)

    preserve = rv(['none', 'xMidYMid meet', 'xMinYMin slice', 'xMaxYMax meet', 'xMidYMid slice'])

    content = '\n'.join(svg_shapes)
    if maybe(0.5):
        transforms = rv(['translate(50,50)', 'rotate(45)', 'scale(2)', 'scale(0)',
                         'skewX(30)', 'matrix(1,0,0,1,0,0)', 'translate(99999,99999)',
                         'rotate(360,50,50)', 'scale(-1,-1)'])
        content = f'<g transform="{transforms}">{content}</g>'
    if maybe(0.3):
        content = f'<g opacity="{rv(["0", "0.5", "1", "-1", "2"])}">{content}</g>'

    defs = '<defs>'
    if maybe(0.5):
        defs += f'<linearGradient id="g1" x1="0%" y1="0%" x2="100%" y2="100%"><stop offset="0%" style="stop-color:{rand_color()}"/><stop offset="100%" style="stop-color:{rand_color()}"/></linearGradient>'
    if maybe(0.3):
        defs += f'<clipPath id="c1"><rect x="10" y="10" width="{rv(["50", "0", "99999"])}" height="{rv(["50", "0", "99999"])}"/></clipPath>'
    if maybe(0.3):
        defs += '<filter id="f1"><feGaussianBlur stdDeviation="5"/></filter>'
    defs += '</defs>'

    svg = f'<svg viewBox="{viewBox}" preserveAspectRatio="{preserve}" xmlns="http://www.w3.org/2000/svg"{style_str}>{defs}{content}</svg>'

    container_display = rv(['block', 'flex', 'grid', 'inline-block', 'inline'])
    container = {'display': container_display, 'width': rand_length(), 'height': rand_length()}
    if container_display in ('flex', 'inline-flex'):
        container['align-items'] = rv(ALIGN_VALUES)
    if container_display in ('grid', 'inline-grid'):
        container['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)

    body = make_element('div', container, svg)
    return wrap_html(body)


def gen_image_data_uri_stress():
    """Generate images with data URI sources and extreme layout properties."""
    DATA_URIS = [
        'data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7',
        'data:image/png;base64,iVBORw0KGgoAAAANSUhEUgAAAAEAAAABCAYAAAAfFcSJAAAADUlEQVR42mP8/5+hHgAHggJ/PchI7wAAAABJRU5ErkJggg==',
        'data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 width=%22100%22 height=%22100%22%3E%3Crect fill=%22red%22 width=%22100%22 height=%22100%22/%3E%3C/svg%3E',
        'data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 viewBox=%220 0 1 1%22%3E%3Ccircle cx=%220.5%22 cy=%220.5%22 r=%220.5%22/%3E%3C/svg%3E',
        'data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 width=%220%22 height=%220%22/%3E',
        'data:image/svg+xml,%3Csvg xmlns=%22http://www.w3.org/2000/svg%22 width=%2299999%22 height=%2299999%22%3E%3Crect width=%2299999%22 height=%2299999%22/%3E%3C/svg%3E',
        'data:image/gif;base64,',
        'data:,',
        'data:image/png;base64,invalid',
    ]

    children = []
    for _ in range(random.randint(5, 20)):
        src = rv(DATA_URIS)
        props = css_props_block()
        attrs = f' src="{src}"'
        if maybe(0.5): attrs += f' width="{rv(["0", "1", "100", "9999", "-1", ""])}"'
        if maybe(0.5): attrs += f' height="{rv(["0", "1", "100", "9999", "-1", ""])}"'
        if maybe(0.3): attrs += f' alt="{rv(TEXT_SAMPLES[:5])}"'
        if maybe(0.2): attrs += ' loading="lazy"'
        if maybe(0.4): props['aspect-ratio'] = rv(['1', '16/9', '4/3', '0.001', 'auto'])
        if maybe(0.3): props['object-fit'] = rv(['fill', 'contain', 'cover', 'none', 'scale-down'])
        if maybe(0.3): props['object-position'] = rv(['center', '0 0', '100% 100%', '-50px -50px'])
        if maybe(0.3): props['vertical-align'] = rv(VERTICAL_ALIGN)
        if maybe(0.2): props['float'] = rv(FLOAT_VALUES)
        if maybe(0.2): props['position'] = rv(POSITION_VALUES)
        style_str = css_to_style(props)
        img = f'<img{attrs}{style_str}>'
        if maybe(0.4):
            wp = {}
            wp['display'] = rv(['block', 'flex', 'grid', 'inline-block', 'table-cell'])
            wp['width'] = rand_length()
            wp['height'] = rand_length()
            if maybe(0.3): wp['overflow'] = rv(OVERFLOW_VALUES)
            img = make_element('div', wp, img)
        children.append(img)

    # background-image with data URI (use only base64 URIs, safe in unquoted url())
    for _ in range(random.randint(1, 5)):
        uri = rv(DATA_URIS[:2])
        bg_props = css_props_block()
        bg_props['background-image'] = f'url({uri})'
        bg_props['background-size'] = rv(['cover', 'contain', 'auto', '100% 100%', '0 0', '99999px 99999px'])
        bg_props['background-repeat'] = rv(['repeat', 'no-repeat', 'repeat-x', 'space', 'round'])
        if maybe(0.5): bg_props['background-position'] = rv(['center', '0 0', '100% 100%', '-99999px 0'])
        children.append(make_element('div', bg_props, rv(TEXT_SAMPLES)))

    container = {'width': rv(['400px', '800px', '0', 'auto', '100%'])}
    body = make_element('div', container, '\n'.join(children))
    return wrap_html(body)


def gen_online_resources():
    """Generate HTML with external CSS, image, and font resources from online URLs."""
    EXTERNAL_CSS = [
        'https://cdnjs.cloudflare.com/ajax/libs/normalize/8.0.1/normalize.min.css',
        'https://cdn.jsdelivr.net/npm/reset-css@5.0.2/reset.min.css',
        'https://unpkg.com/sanitize.css',
    ]
    EXTERNAL_FONTS = [
        'https://fonts.googleapis.com/css2?family=Roboto:wght@100;400;900&display=swap',
        'https://fonts.googleapis.com/css2?family=Noto+Sans+SC&display=swap',
        'https://fonts.googleapis.com/css2?family=Fira+Code&display=swap',
    ]
    EXTERNAL_IMAGES = [
        'https://via.placeholder.com/1x1',
        'https://via.placeholder.com/1000x1000',
        'https://via.placeholder.com/1x1000',
        'https://via.placeholder.com/1000x1',
        'https://picsum.photos/200/300',
    ]

    head_parts = ['<meta charset="utf-8">']
    for _ in range(random.randint(0, 2)):
        head_parts.append(f'<link rel="stylesheet" href="{rv(EXTERNAL_CSS)}">')
    font_urls = random.sample(EXTERNAL_FONTS, random.randint(1, len(EXTERNAL_FONTS)))
    for url in font_urls:
        head_parts.append(f'<link rel="stylesheet" href="{url}">')
    if maybe(0.3):
        head_parts.append(f'<link rel="preconnect" href="https://fonts.gstatic.com" crossorigin>')
    head = '\n'.join(head_parts)

    children = []
    # images from external URLs
    for _ in range(random.randint(2, 8)):
        src = rv(EXTERNAL_IMAGES)
        props = css_props_block()
        if maybe(0.4): props['aspect-ratio'] = rv(['1', '16/9', 'auto'])
        if maybe(0.3): props['object-fit'] = rv(['fill', 'contain', 'cover', 'none', 'scale-down'])
        attrs = f' src="{src}"'
        if maybe(0.5): attrs += f' width="{rv(["0", "100", "9999"])}"'
        if maybe(0.5): attrs += f' height="{rv(["0", "100", "9999"])}"'
        style_str = css_to_style(props)
        children.append(f'<img{attrs}{style_str}>')

    # text using web fonts
    font_families = ['Roboto', 'Noto Sans SC', 'Fira Code']
    for _ in range(random.randint(3, 10)):
        props = css_props_block()
        props['font-family'] = f"'{rv(font_families)}', sans-serif"
        props['font-weight'] = rv(['100', '400', '700', '900', 'bold', 'normal'])
        props['font-size'] = rv(FONT_SIZES)
        if maybe(0.3): props['font-style'] = rv(['normal', 'italic', 'oblique'])
        if maybe(0.3): props['font-variant'] = rv(['normal', 'small-caps'])
        if maybe(0.3): props['letter-spacing'] = rv(['0', '-2px', '5px', '1em'])
        children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    # background images from URLs
    for _ in range(random.randint(1, 3)):
        props = css_props_block()
        props['background-image'] = f"url({rv(EXTERNAL_IMAGES)})"
        props['background-size'] = rv(['cover', 'contain', 'auto', '100% 100%'])
        props['min-height'] = rv(['100px', '200px', '0'])
        children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    body = '\n'.join(children)
    return f'<!DOCTYPE html>\n<html>\n<head>{head}\n<style>* {{ margin: 0; padding: 0; }}</style></head>\n<body>\n{body}\n</body>\n</html>'


def gen_font_stress():
    """Generate HTML with @font-face declarations, woff/woff2, and font property stress tests."""
    FONT_FACE_SRCS = [
        'url("https://fonts.gstatic.com/s/roboto/v30/KFOmCnqEu92Fr1Mu4mxK.woff2") format("woff2")',
        'url("data:font/woff2;base64,d09GMgABAAAAAA") format("woff2")',
        'url("data:font/woff;base64,d09GRgABAAAAAA") format("woff")',
        'url("nonexistent.woff2") format("woff2")',
        'url("") format("woff2")',
        'url("data:,") format("truetype")',
        'local("Arial")',
        'local("NonExistentFont12345")',
        'url("https://example.com/font.ttf") format("truetype"), url("https://example.com/font.woff") format("woff")',
    ]

    SYSTEM_FONTS = ['serif', 'sans-serif', 'monospace', 'cursive', 'fantasy',
                    'system-ui', 'ui-serif', 'ui-sans-serif', 'ui-monospace', 'ui-rounded',
                    'emoji', 'math', 'fangsong']

    css_rules = []
    font_names = []
    for i in range(random.randint(2, 8)):
        name = f'FuzzFont{i}'
        font_names.append(name)
        src = rv(FONT_FACE_SRCS)
        weight = rv(['100', '400', '700', '900', '100 900', 'normal', 'bold'])
        style = rv(['normal', 'italic', 'oblique', 'oblique 10deg 20deg'])
        display = rv(['auto', 'block', 'swap', 'fallback', 'optional'])
        urange = rv(['U+0-FF', 'U+4E00-9FFF', 'U+0-FFFF', 'U+0', 'U+0-0'])
        rule = f'@font-face {{ font-family: "{name}"; src: {src}; font-weight: {weight}; font-style: {style}; font-display: {display}; unicode-range: {urange}; }}'
        css_rules.append(rule)

    children = []
    for _ in range(random.randint(5, 20)):
        props = css_props_block()
        families = []
        if maybe(0.7) and font_names:
            families.append(f"'{rv(font_names)}'")
        if maybe(0.5):
            families.append(f"'{rv(font_names)}'" if font_names else "'Arial'")
        families.append(rv(SYSTEM_FONTS))
        props['font-family'] = ', '.join(families)
        props['font-size'] = rv(FONT_SIZES + ['xx-small', 'xx-large', 'larger', 'smaller', '0.001em', '99999px'])
        props['font-weight'] = rv(['1', '100', '400', '700', '900', '999', '1000', 'normal', 'bold', 'lighter', 'bolder'])
        if maybe(0.4): props['font-style'] = rv(['normal', 'italic', 'oblique', 'oblique 20deg'])
        if maybe(0.3): props['font-stretch'] = rv(['ultra-condensed', 'condensed', 'normal', 'expanded', 'ultra-expanded', '50%', '200%'])
        if maybe(0.3): props['font-variant'] = rv(['normal', 'small-caps', 'all-small-caps', 'petite-caps', 'unicase', 'titling-caps'])
        if maybe(0.3): props['font-feature-settings'] = rv(['"liga" 1', '"kern" 0', '"smcp" 1', 'normal'])
        if maybe(0.2): props['line-height'] = rv(LINE_HEIGHTS)
        if maybe(0.2): props['letter-spacing'] = rv(['0', '-5px', '10px', '1em', 'normal'])
        if maybe(0.2): props['word-spacing'] = rv(['0', '-5px', '10px', '1em', 'normal'])
        children.append(make_element('div', props, rv(TEXT_SAMPLES)))

    extra_css = '\n'.join(css_rules)
    body = '\n'.join(children)
    return wrap_html(body, extra_css=extra_css)


def gen_form_comprehensive():
    """Generate comprehensive HTML forms with all input types, fieldset, legend, and layout."""
    children = []

    # fieldset + legend combinations
    for _ in range(random.randint(1, 3)):
        fp = css_props_block()
        if maybe(0.3): fp['display'] = rv(DISPLAY_VALUES)
        lp = css_props_block()
        if maybe(0.3): lp['display'] = rv(DISPLAY_VALUES)
        if maybe(0.3): lp['float'] = rv(FLOAT_VALUES)

        fields = []
        for _ in range(random.randint(2, 8)):
            ip = css_props_block()
            if maybe(0.3): ip['display'] = rv(DISPLAY_VALUES)
            if maybe(0.3): ip['width'] = rand_length()
            if maybe(0.3): ip['height'] = rand_length()
            if maybe(0.2): ip['position'] = rv(POSITION_VALUES)
            input_type = rv(['text', 'password', 'email', 'tel', 'url', 'search',
                            'number', 'range', 'date', 'time', 'datetime-local',
                            'month', 'week', 'color', 'file', 'hidden',
                            'checkbox', 'radio', 'submit', 'reset', 'button'])
            style_str = css_to_style(ip)
            extra = ''
            if input_type in ('text', 'password', 'email'):
                if maybe(0.3): extra += f' placeholder="{rv(TEXT_SAMPLES[:5])}"'
                if maybe(0.2): extra += f' size="{rv(["0", "1", "20", "9999"])}"'
            if input_type == 'image':
                extra += ' src="data:image/gif;base64,R0lGODlhAQABAIAAAAAAAP///yH5BAEAAAAALAAAAAABAAEAAAIBRAA7"'
            fields.append(f'<input type="{input_type}"{extra}{style_str}>')
            if maybe(0.5):
                fields.append(make_element('label', {'display': rv(['inline', 'block', 'inline-block', 'flex'])}, rv(TEXT_SAMPLES[:5])))

        legend = make_element('legend', lp, rv(TEXT_SAMPLES[:5]))
        fieldset = make_element('fieldset', fp, legend + '\n'.join(fields))
        children.append(fieldset)

    # textarea with extreme sizes
    for _ in range(random.randint(1, 3)):
        tp = css_props_block()
        tp['resize'] = rv(['none', 'both', 'horizontal', 'vertical'])
        rows = rv(['0', '1', '5', '9999'])
        cols = rv(['0', '1', '20', '9999'])
        children.append(f'<textarea rows="{rows}" cols="{cols}"{css_to_style(tp)}>{rv(TEXT_SAMPLES)}</textarea>')

    # select with options
    for _ in range(random.randint(1, 3)):
        sp = css_props_block()
        if maybe(0.3): sp['display'] = rv(DISPLAY_VALUES)
        opt_count = random.choice([0, 1, 5, 100])
        options = ''.join(f'<option value="{i}">{rv(TEXT_SAMPLES[:5])}</option>' for i in range(opt_count))
        multiple = ' multiple' if maybe(0.3) else ''
        size = f' size="{rv(["0", "1", "5", "99"])}"' if maybe(0.3) else ''
        children.append(f'<select{multiple}{size}{css_to_style(sp)}>{options}</select>')

    # progress and meter
    children.append(f'<progress value="{rv(["0", "50", "100", "-1", "999"])}" max="{rv(["0", "100", "1", "999"])}" style="width:{rand_length()};height:{rand_length()};display:{rv(DISPLAY_VALUES)};"></progress>')
    children.append(f'<meter value="{rv(["0", "0.5", "1", "-1", "2"])}" min="{rv(["0", "-1"])}" max="{rv(["1", "100", "0"])}" style="width:{rand_length()};display:{rv(DISPLAY_VALUES)};"></meter>')
    children.append(f'<output style="display:{rv(DISPLAY_VALUES)};width:{rand_length()};">{rv(TEXT_SAMPLES[:5])}</output>')

    form_props = css_props_block()
    form_props['display'] = rv(['block', 'flex', 'grid', 'inline-block'])
    if form_props['display'] == 'grid':
        form_props['grid-template-columns'] = rv(GRID_TEMPLATE_VALUES)
    if form_props['display'] in ('flex', 'inline-flex'):
        form_props['flex-wrap'] = rv(FLEX_WRAP)
        form_props['align-items'] = rv(ALIGN_VALUES)
    form_props['width'] = rv(['400px', '800px', '0', 'auto', '100%'])

    body = make_element('form', form_props, '\n'.join(children))
    return wrap_html(body)


def gen_form_in_flex_grid():
    """Generate form elements inside flex/grid/table containers with conflicting sizing."""
    containers = []

    # forms in flex
    flex_items = []
    for _ in range(random.randint(3, 8)):
        elem = rv([
            f'<input type="text" style="flex:{rv(["1", "0", "999", "0 1 auto"])};width:{rand_length()};min-width:{rand_length()};max-width:{rand_length()};">',
            f'<textarea style="flex:{rv(["1", "0 1 auto"])};resize:none;width:{rand_length()};height:{rand_length()};">text</textarea>',
            f'<select style="flex:{rv(["1", "0"])};width:{rand_length()};"><option>opt</option></select>',
            f'<button style="flex:{rv(["1", "0 1 auto"])};width:{rand_length()};">btn</button>',
        ])
        flex_items.append(elem)
    containers.append(make_element('div', {
        'display': 'flex', 'flex-wrap': rv(FLEX_WRAP),
        'width': rv(['400px', '800px', '0', 'auto']),
        'gap': rv(['0', '10px', '99999px']),
    }, '\n'.join(flex_items)))

    # forms in grid
    grid_items = []
    for _ in range(random.randint(3, 8)):
        elem = rv([
            f'<input type="text" style="width:100%;min-width:{rand_length()};grid-column:span {random.randint(1, 3)};">',
            f'<textarea style="grid-row:span {random.randint(1, 3)};width:100%;height:100%;">text</textarea>',
            f'<select style="width:100%;"><option>opt</option></select>',
        ])
        grid_items.append(elem)
    containers.append(make_element('div', {
        'display': 'grid',
        'grid-template-columns': rv(GRID_TEMPLATE_VALUES),
        'gap': rv(['0', '10px', '50px']),
        'width': rv(['400px', '800px', '0', 'auto']),
    }, '\n'.join(grid_items)))

    # forms in table
    form_cells = []
    for _ in range(random.randint(2, 6)):
        elem = rv([
            '<input type="text" style="width:100%;">',
            '<textarea style="width:100%;height:50px;">text</textarea>',
            '<select style="width:100%;"><option>A</option><option>B</option></select>',
            f'<input type="checkbox"><label>{rv(TEXT_SAMPLES[:5])}</label>',
        ])
        form_cells.append(make_element('td', css_props_block(), elem))
    row = make_element('tr', {}, ''.join(form_cells))
    containers.append(make_element('table', {
        'width': rv(['100%', '400px', '0', 'auto']),
        'table-layout': rv(['auto', 'fixed']),
        'border-collapse': rv(['collapse', 'separate']),
    }, row))

    body = '\n'.join(containers)
    return wrap_html(body)


# ---------------------------------------------------------------------------
# Generator registry
# ---------------------------------------------------------------------------

GENERATORS = {
    'flex': gen_flex_stress,
    'grid': gen_grid_stress,
    'table': gen_table_stress,
    'block': gen_margin_collapse,
    'float': gen_float_stress,
    'text': gen_text_stress,
    'inline': gen_inline_stress,
    'position': gen_position_stress,
    'margin': gen_margin_collapse,
    'deep': gen_deep_nesting,
    'wide': gen_wide_siblings,
    'mixed': gen_mixed_context,
    'replaced': gen_replaced_elements,
    'list': gen_list_stress,
    'flex_grid': gen_flex_in_grid,
    'table_flex': gen_table_in_flex,
    'contradict': gen_contradiction,
    'recursive': gen_recursive_layout,
    'float_chain': gen_float_clear_chain,
    'aspect': gen_aspect_ratio_stress,
    'writing': gen_writing_mode_stress,
    'transform': gen_transform_stress,
    'multicol': gen_multi_column,
    'cssvar': gen_css_var_stress,
    'form': gen_form_elements,
    'pct_chain': gen_percentage_chain,
    'contents': gen_display_contents,
    'empty': gen_empty_elements,
    'bfc': gen_bfc_creation,
    'ib_baseline': gen_inline_block_baseline,
    'caption': gen_table_caption,
    'vis_collapse': gen_visibility_collapse,
    'zero_dim': gen_zero_dimension_stress,
    'sticky': gen_sticky_position,
    'pseudo': gen_pseudo_element_stress,
    'overflow': gen_overflow_interaction,
    'table_ctx': gen_table_in_everything,
    'flex_minmax': gen_flex_overflow_minmax,
    'grid_overlap': gen_grid_overlap_placement,
    'ws_stress': gen_whitespace_stress,
    'svg_inline': gen_svg_inline_stress,
    'img_data_uri': gen_image_data_uri_stress,
    'online_res': gen_online_resources,
    'font_stress': gen_font_stress,
    'form_full': gen_form_comprehensive,
    'form_layout': gen_form_in_flex_grid,
}

ALL_MODES = list(GENERATORS.keys())

def generate_one(mode=None):
    """Generate a single fuzzy HTML document."""
    if mode is None or mode == 'all':
        mode = random.choice(ALL_MODES)
    gen = GENERATORS.get(mode)
    if gen is None:
        raise ValueError(f'Unknown mode: {mode}. Available: {", ".join(ALL_MODES)}')
    return gen()


# ---------------------------------------------------------------------------
# Main
# ---------------------------------------------------------------------------

def main():
    parser = argparse.ArgumentParser(description='Generate fuzzy HTML for Radiant layout testing')
    parser.add_argument('--mode', '-m', default='all',
                        help=f'Generation mode: {", ".join(ALL_MODES)}, all (default: all)')
    parser.add_argument('--count', '-n', type=int, default=100,
                        help='Number of files to generate (default: 100)')
    parser.add_argument('--output-dir', '-o', default=None,
                        help='Output directory (default: stdout for count=1)')
    parser.add_argument('--seed', '-s', type=int, default=None,
                        help='Random seed for reproducibility')
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    if args.output_dir:
        os.makedirs(args.output_dir, exist_ok=True)
        for i in range(args.count):
            mode = args.mode if args.mode != 'all' else random.choice(ALL_MODES)
            html = generate_one(mode)
            path = os.path.join(args.output_dir, f'fuzz_{mode}_{i:04d}.html')
            with open(path, 'w') as f:
                f.write(html)
        print(f'Generated {args.count} files in {args.output_dir}')
    else:
        for i in range(args.count):
            html = generate_one(args.mode)
            if args.count == 1:
                print(html)
            else:
                print(f'--- fuzz #{i} ({args.mode}) ---')
                print(html)
                print()


if __name__ == '__main__':
    main()
