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
