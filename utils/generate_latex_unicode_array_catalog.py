#!/usr/bin/env python3
"""Build expl3's Unicode font arrays from the bundled format sources."""

import argparse
import hashlib
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
TEX = ROOT / 'lmd/package/doc/latex/tex/expl3-code.tex'
DATA = ROOT / 'lmd/package/doc/latex/tex/UnicodeData.txt'
DEST = ROOT / 'lmd/package/doc/latex/tex_engine/unicode_array_catalog.ls'


def required(pattern, source):
    match = re.search(pattern, source, re.S)
    if not match:
        raise ValueError('unsupported expl3 Unicode array source: ' + pattern)
    return match


def render(tex_bytes, data_bytes):
    tex = tex_bytes.decode('utf-8')
    block_size = int(required(r'\\int_const:Nn\s*\\c__codepoint_block_size_int\s*\{\s*(\d+)\s*\}', tex)[1])
    limit = int(required(r'\\intarray_new:cn\s*\{\s*g__codepoint_\s*#1\s*_index_intarray\s*\}\s*\{\s*\\int_div_truncate:nn\s*\{\s*"([0-9A-Fa-f]+)', tex)[1], 16)
    types = [s.strip() for s in required(r'\\clist_map_inline:nn\s*\{\s*(category\s*,\s*uppercase\s*,\s*lowercase)\s*\}\s*\{\s*\\cs_set_nopar:cpn\s*\{\s*l__codepoint_', tex)[1].split(',')]
    names = re.findall(r'\{\s*([A-Z][a-z])\s*\}', required(r'\\__codepoint_data_auxi:w\s*\{\s*1\s*\}(.*?)\\q_recursion_tail', tex)[1])
    categories = {name: index + 1 for index, name in enumerate(names)}
    if not block_size or limit % block_size or 'Cn' not in categories:
        raise ValueError('unexpected Unicode array dimensions or category list')
    channels = [[0] * limit for _ in types]
    next_cp = 0
    # UnicodeData columns 0, 1, 2, 12 and 13 supply codepoint, range name, category and simple case maps.
    for line in data_bytes.decode('utf-8').splitlines():
        if not line.strip():
            break
        fields = line.split(';')
        cp = int(fields[0], 16)
        if cp < next_cp or cp >= limit or len(fields) < 15:
            raise ValueError('unexpected UnicodeData row or order')
        values = [categories[fields[2]], int(fields[12], 16) - cp if fields[12] else 0,
                  int(fields[13], 16) - cp if fields[13] else 0]
        fill = values if 'Last>' in fields[1] else [categories['Cn'], 0, 0]
        for channel, value, gap in zip(channels, values, fill):
            channel[next_cp:cp] = [gap] * (cp - next_cp)
            channel[cp] = value
        next_cp = cp + 1
    # Expl3 finalizes the unfilled tail with zero in all channels.
    results = []
    for channel in channels:
        blocks, indices, seen = [], [], {}
        for start in range(0, limit, block_size):
            block = tuple(channel[start:start + block_size])
            if block not in seen:
                blocks.extend(block)
                seen[block] = len(blocks) // block_size
            indices.append(seen[block])
        results.append((indices, blocks))
    entries = [(f'c__codepoint_{name}_{kind}_intarray', values)
               for kind, part in [('index', 0), ('blocks', 1)]
               for name, result in zip(types, results) for values in [result[part]]]
    lines = ['// Generated from bundled expl3-code.tex and UnicodeData.txt; do not edit by hand.',
             '// Regenerate: python3 utils/generate_latex_unicode_array_catalog.py',
             '// expl3-code.tex SHA-256: ' + hashlib.sha256(tex_bytes).hexdigest(),
             '// UnicodeData.txt SHA-256: ' + hashlib.sha256(data_bytes).hexdigest(), 'let arrays = [']
    for index, (name, values) in enumerate(entries):
        lines.append('    {name:"' + name + '", source_name:"' + 'g' + name[1:] + '", dense:true, values:[')
        for pos in range(0, len(values), 16):
            lines.append('        ' + ','.join(map(str, values[pos:pos + 16])) + (',' if pos + 16 < len(values) else ''))
        lines.append('    ]}' + (',' if index + 1 < len(entries) else ''))
    lines.extend(['];', 'pub fn prepared() => arrays'])
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    tex, data = TEX.read_bytes(), DATA.read_bytes()
    output = render(tex, data)
    if TEX.read_bytes() != tex or DATA.read_bytes() != data:
        raise SystemExit('Unicode sources changed during generation')
    if args.check:
        if not DEST.is_file() or DEST.read_text() != output:
            raise SystemExit('LaTeX Unicode array catalog differs; regenerate it')
    else:
        DEST.write_text(output)


if __name__ == '__main__':
    main()
