#!/usr/bin/env python3
"""Extract the Script TeX profile's expl3 primitive aliases and rebinding metadata."""

import argparse
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'lmd/package/doc/latex/tex/expl3-code.tex'
PRIMITIVES = ROOT / 'lmd/package/doc/latex/tex_engine/primitives.ls'
DEST = ROOT / 'lmd/package/doc/latex/tex_engine/primitive_alias_catalog.ls'


def render(source_bytes, primitive_bytes):
    source = source_bytes.decode('utf-8')
    primitive_source = primitive_bytes.decode('utf-8')
    raw_names = primitive_source.split('let PRIMITIVES = [', 1)[1].split(']', 1)[0]
    primitives = json.loads('[' + raw_names + ']')
    if len(primitives) != len(set(primitives)):
        raise ValueError('duplicate Script primitive names')
    declarations = source.split('\\long \\def \\__kernel_primitive:NN #1#2', 1)[1]
    declarations, suffix = declarations.split('\n\\tex_endgroup:D', 1)
    pattern = r'\\__kernel_primitive:NN\s+\\([a-zA-Z]+|[^a-zA-Z])\s*\\([a-zA-Z_]+:D)'
    # The second definition copies only defined sources; later declarations win.
    marker = '\\tex_long:D \\tex_def:D \\__kernel_primitive:NN #1#2'
    unconditional, conditional_declarations = declarations.split(marker, 1)
    conditional_body = re.match(
        r'\s*\{\s*\\tex_ifdefined:D\s*#1\s*\\tex_expandafter:D\s*\\use_ii:nn'
        r'\s*\\tex_fi:D\s*\\use_none:n\s*\{\s*\\tex_global:D\s*\\tex_let:D\s*#2\s*#1\s*\}\s*\}',
        conditional_declarations)
    if not conditional_body:
        raise ValueError('expl3 optional primitive copying needs a new audit')
    first = re.findall(pattern, unconditional)
    optional = re.findall(pattern, conditional_declarations[conditional_body.end():])
    entries = [(original, alias, False) for original, alias in first] + [
        (original, alias, True) for original, alias in optional]
    # Stop on a changed declaration inventory instead of silently omitting aliases.
    supported = [(original, alias) for original, alias, _ in entries if original in primitives]
    if len(supported) != len(primitives) or {original for original, _ in supported} != set(primitives):
        raise ValueError('Script primitives and expl3 declarations need a new audit')
    if len(first) != 531 or len(optional) != 468:
        raise ValueError('expl3 primitive declaration inventory needs a new audit')

    section = suffix.split('%% File: l3basics.dtx', 1)[0]
    binding = re.compile(r'\\tex_let:D\s+\\([a-zA-Z_]+:D)\s+\\([a-zA-Z@_]+)')
    conditional = re.compile(r'\\tex_ifdefined:D\s+\\([a-zA-Z@]+)\s*(.*?)\\tex_fi:D', re.S)
    groups = []
    for match in conditional.finditer(section):
        body = match.group(2)
        bindings = binding.findall(body)
        if bindings and not binding.sub('', body).strip():
            groups.append((match.group(1), bindings))
    expected_guards = ['@@end', 'orieveryjob', 'oripdfoutput', 'normalend',
                       'normalitaliccorrection', 'normalleft']
    if [guard for guard, _ in groups] != expected_guards:
        raise ValueError('expl3 conditional aliases need a new audit')
    hyph = section.split('\\tex_ifdefined:D \\@@hyph', 1)[1].split('\\tex_ifdefined:D \\@@shipout', 1)[0]
    hyph_bindings = binding.findall(hyph)
    if not hyph_bindings or binding.sub('', hyph).strip():
        raise ValueError('expl3 hyphen alias branch needs a new audit')
    groups.insert(1, ('@@hyph', hyph_bindings))

    clock_pattern = re.compile(
        r'\\tex_edef:D\s+\\l_tmpa_tl\s*\{\s*\\tex_meaning:D\s*\\(tex_[a-z]+:D)\s*\}'
        r'\s*\\tex_edef:D\s+\\l_tmpb_tl\s*\{\s*\\tex_string:D\s*\\([a-z]+)\s*\}'
        r'\s*\\tex_ifx:D\s+\\l_tmpa_tl\s+\\l_tmpb_tl\s*\\tex_else:D'
        r'\s*\\tex_global:D\s+\\tex_let:D\s+\\\1\s+\\tex_undefined:D\s*\\tex_fi:D')
    clocks = clock_pattern.findall(section)
    if clocks != [('tex_time:D', 'time'), ('tex_day:D', 'day'),
                  ('tex_month:D', 'month'), ('tex_year:D', 'year')]:
        raise ValueError('expl3 clock checks need a new audit')
    ship_list = section.split('\\@tempa \\tex_string:D :=', 1)[1].split('\\do', 1)[0]
    ship_names = re.findall(r'\\([a-zA-Z@]+)', ship_list)
    if len(ship_names) != 10 or re.sub(r'\\[a-zA-Z@]+', '', ship_list).strip():
        raise ValueError('expl3 shipout candidate list needs a new audit')
    ship_checks = [
        r'\\tex_ifdefined:D\s+\\@@shipout\s+\\tex_let:D\s+\\tex_shipout:D\s+\\@@shipout\s+\\tex_fi:D',
        r'\\tex_edef:D\s+\\l_tmpa_tl\s*\{\s*\\tex_string:D\s+\\shipout\s*\}',
        r'\\tex_edef:D\s+\\l_tmpb_tl\s*\{\s*\\tex_meaning:D\s+\\shipout\s*\}',
        r'\\tex_edef:D\s+\\l_tmpb_tl\s*\{\s*\\tex_expandafter:D\s+\\tex_meaning:D\s+\\@tempa\s*\}',
        r'\\tex_ifx:D\s+\\l_tmpa_tl\s+\\l_tmpb_tl\s+'
        r'\\tex_global:D\s+\\tex_expandafter:D\s+\\tex_let:D\s+'
        r'\\tex_expandafter:D\s+\\tex_shipout:D\s+\\@tempa',
    ]
    if any(not re.search(check, section) for check in ship_checks):
        raise ValueError('expl3 shipout comparison needs a new audit')
    font = re.search(r'\\tex_let:D\s+\\tex_tracingfonts:D\s+\\tex_undefined:D\s*'
                     r'\\tex_ifdefined:D\s+\\pdftracingfonts\s*'
                     r'\\tex_let:D\s+\\tex_tracingfonts:D\s+\\pdftracingfonts\s*'
                     r'\\tex_else:D\s*\\tex_ifdefined:D\s+\\tex_directlua:D', section)
    if not font:
        raise ValueError('expl3 font tracing branch needs a new audit')
    pdfmap = re.search(
        r'\\tex_ifnum:D\s+0\s*\\tex_ifdefined:D\s+\\tex_pdftexversion:D\s+1\s+\\tex_fi:D'
        r'\s*\\tex_ifdefined:D\s+\\tex_luatexversion:D\s+1\s+\\tex_fi:D\s*=\s*0\s*%?\s*'
        r'((?:\\tex_let:D\s+\\tex_[a-z]+:D\s+\\tex_undefined:D\s*)+)\\tex_fi:D', section)
    if not pdfmap:
        raise ValueError('expl3 PDF map fallback needs a new audit')
    pdfmap_names = re.findall(r'\\tex_let:D\s+\\(tex_[a-z]+:D)\s+\\tex_undefined:D', pdfmap.group(1))
    if pdfmap_names != ['tex_pdfmapfile:D', 'tex_pdfmapline:D']:
        raise ValueError('expl3 PDF map aliases need a new audit')

    lines = ['// Generated from bundled expl3-code.tex and Script primitives; do not edit.',
             '// Regenerate: python3 utils/generate_latex_primitive_alias_catalog.py',
             '// Source SHA-256: ' + hashlib.sha256(source_bytes).hexdigest(),
             '// Primitives SHA-256: ' + hashlib.sha256(primitive_bytes).hexdigest(), 'let entries = [']
    for index, (original, alias, is_optional) in enumerate(entries):
        lines.append('    {alias:' + json.dumps(alias) + ', source:' + json.dumps(original) +
                     (', optional:true}' if is_optional else '}') +
                     (',' if index + 1 < len(entries) else ''))
    lines.extend(['];', 'pub fn prepared() => entries', 'let rebindings = ['])
    for index, (guard, bindings) in enumerate(groups):
        pairs = ', '.join('{alias:' + json.dumps(alias) + ', source:' + json.dumps(original) + '}'
                          for alias, original in bindings)
        lines.append('    {guard:' + json.dumps(guard) + ', bindings:[' + pairs + ']}' +
                     (',' if index + 1 < len(groups) else ''))
    lines.extend(['];', 'pub fn simple_rebindings() => rebindings', 'let clock_entries = ['])
    for index, (alias, original) in enumerate(clocks):
        lines.append('    {alias:' + json.dumps(alias) + ', source:' + json.dumps(original) + '}' +
                     (',' if index + 1 < len(clocks) else ''))
    lines.extend(['];', 'pub fn clock_checks() => clock_entries',
                  'let shipout_entries = [' + ', '.join(json.dumps(name) for name in ship_names) + ']',
                  'pub fn shipout_candidates() => shipout_entries',
                  'let pdfmap_entries = [' + ', '.join(json.dumps(name) for name in pdfmap_names) + ']',
                  'pub fn pdfmap_fallbacks() => pdfmap_entries'])
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    parser.add_argument('--output', type=Path, default=DEST)
    args = parser.parse_args()
    source, primitives = SOURCE.read_bytes(), PRIMITIVES.read_bytes()
    result = render(source, primitives)
    if SOURCE.read_bytes() != source or PRIMITIVES.read_bytes() != primitives:
        raise SystemExit('catalog sources changed during generation')
    if args.check:
        if not args.output.is_file() or args.output.read_text() != result:
            raise SystemExit('LaTeX primitive alias catalog differs; regenerate it')
    else:
        args.output.write_text(result)


if __name__ == '__main__':
    main()
