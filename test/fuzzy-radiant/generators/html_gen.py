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
    '0', '0px', '0.001px', '1px', '10px', '50px', '100px', '200px', '500px',
    '-1px', '-10px', '-50px', '-9999px',
    '99999px', '1e5px',
    'auto',
    '0%', '1%', '50%', '100%', '200%', '-50%',
    'min-content', 'max-content', 'fit-content',
    'calc(100% - 1px)', 'calc(100% + 100px)', 'calc(100% - 99999px)',
    'calc(50% + 50px)', 'calc(0 / 1)',
    '1em', '2em', '10em', '100em',
    '1vw', '50vw', '100vw',
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
    '1fr 2fr', '100px 1fr 100px', 'repeat(5, 1fr)',
    '50px repeat(3, 1fr) 50px',
]

TAGS_BLOCK = ['div', 'section', 'article', 'main', 'aside', 'header', 'footer', 'nav', 'p', 'h1', 'h2', 'h3']
TAGS_INLINE = ['span', 'a', 'em', 'strong', 'b', 'i', 'code', 'small', 'sub', 'sup']
TAGS_ALL = TAGS_BLOCK + TAGS_INLINE

TEXT_SAMPLES = [
    'Hello World',
    'A',
    '',
    'Thisisaverylongwordwithnobreakopportunities',
    'Short',
    '你好世界混合Latin文字',
    'مرحبا',
    'Lorem ipsum dolor sit amet, consectetur adipiscing elit.',
    'a b c d e f g h i j k l m n o p q r s t u v w x y z',
    '🎉🎊🎈🎁',
    ' ',
    '\t\n',
    '&amp; &lt; &gt; &quot;',
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
