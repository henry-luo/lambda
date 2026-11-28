#!/usr/bin/env python3
"""Extract expl3's later count declarations from the bundled format source."""

import argparse
import ast
import hashlib
import json
from pathlib import Path
import re

ROOT = Path(__file__).resolve().parents[1]
SOURCE = ROOT / 'lmd/package/doc/latex/tex/expl3-code.tex'
KERNEL = ROOT / 'lmd/package/doc/latex/tex_engine/kernel.tex'
DEST = ROOT / 'lmd/package/doc/latex/tex_engine/format_count_catalog.ls'


def integer(expression):
    # Expl3 ignores spaces within integer literals and accepts TeX hex and character constants.
    source = re.sub(r'(?<=\d)\s+(?=\d)', '', expression.strip())
    source = re.sub(r'"([0-9a-fA-F]+)', lambda match: str(int(match[1], 16)), source)
    source = re.sub(r'`([^\s])', lambda match: str(ord(match[1])), source)
    if not re.fullmatch(r'[0-9+*()\-\s]+', source):
        raise ValueError('unsupported expl3 integer expression: ' + expression)

    def evaluate(node):
        if isinstance(node, ast.Constant) and type(node.value) is int:
            return node.value
        if isinstance(node, ast.UnaryOp) and isinstance(node.op, (ast.UAdd, ast.USub)):
            return evaluate(node.operand) * (1 if isinstance(node.op, ast.UAdd) else -1)
        if isinstance(node, ast.BinOp) and isinstance(node.op, (ast.Add, ast.Sub, ast.Mult)):
            left, right = evaluate(node.left), evaluate(node.right)
            return left + right if isinstance(node.op, ast.Add) else left - right if isinstance(node.op, ast.Sub) else left * right
        raise ValueError('unsupported expl3 arithmetic: ' + expression)

    return evaluate(ast.parse(source, mode='eval').body)


def render(data, kernel_bytes):
    source = data.decode('utf-8')
    kernel = kernel_bytes.decode('utf-8')
    start = source.index('\\int_new:N \\l__intarray_loop_int')
    branch = re.search(r'\\dim_const:Nn\s+\\c__intarray_sp_dim\s*\{\s*1\s+sp\s*\}\s*'
                       r'\\int_new:N\s+\\(g__intarray_font_int)', source[start:])
    limits = re.findall(r'\\tex_global:D\s+\\__int_constdef:Nw\s+\\c__int_max_constdef_int\s+(\d+)\s*~', source)
    if branch is None or len(limits) != 2:
        raise ValueError('unsupported expl3 font-array branch or constant limit')
    # The second branch is the non-LuaTeX mathchardef used by this profile.
    threshold = int(limits[1])
    # Reject unmodelled top-level mutations instead of emitting a stale initial value.
    mutations = re.finditer(r'(?m)^\\int_(?:g?set(?:_eq)?|g?zero|g?incr|g?decr|g?add|g?sub|g?mul|g?div):[A-Za-z]+', source[start:])
    if any(match.group() != '\\int_set:Nn' for match in mutations):
        raise ValueError('unsupported expl3 top-level integer mutation')
    # Top-level declarations are unindented; the font allocator is in the selected branch.
    pattern = re.compile(r'(?m)^(\\int_new:N|\\int_const:Nn|\\int_set:Nn)\s+\\([a-zA-Z_]+)'
                         r'(?:\s*\{([^{}]*)\})?')
    entries = []
    declarations = {}
    for match in pattern.finditer(source, start):
        kind, name, body = match.groups()
        if kind == '\\int_new:N':
            item = {'name': name, 'kind': 'count'}
            entries.append(item)
            declarations[name] = item
        elif kind == '\\int_const:Nn':
            value = integer(body)
            item = {'name': name, 'kind': 'immediate' if 0 <= value <= threshold else 'count', 'initial': value}
            entries.append(item)
            declarations[name] = item
        else:
            if name not in declarations or declarations[name]['kind'] != 'count':
                raise ValueError('unexpected expl3 integer initialization: ' + name)
            declarations[name]['initial'] = integer(body)
    if not entries or entries[0]['name'] != 'l__intarray_loop_int':
        raise ValueError('unexpected expl3 integer declaration ordering')
    static_arrays = re.findall(r'(?m)^\\intarray_const_from_clist:Nn\s+\\([a-zA-Z_]+)', source)
    if not static_arrays:
        raise ValueError('missing expl3 constant arrays')
    entries[0]['derived'] = 'array_loop'
    entries[0]['array_name'] = static_arrays[-1]
    entries.insert(1, {'name': branch[1], 'kind': 'count', 'derived': 'array_font_count'})
    # This non-LuaTeX branch declares its object block size inside a conditional.
    pdf_constant = re.search(r'(?m)^    \\int_const:Nn\s+\\(c__pdf_object_block_size_int)\s*\{([^{}]*)\}', source)
    if pdf_constant is None or not 0 <= integer(pdf_constant[2]) <= threshold:
        raise ValueError('unsupported expl3 PDF object block constant')
    pdf_position = next(i for i, entry in enumerate(entries) if entry['name'] == 'g__pdf_backend_object_int')
    entries.insert(pdf_position + 1, {'name': pdf_constant[1], 'kind': 'immediate', 'initial': integer(pdf_constant[2])})
    lines = ['// Generated from bundled expl3-code.tex and kernel.tex; do not edit by hand.',
             '// Regenerate: python3 utils/generate_latex_format_count_catalog.py',
             '// Source SHA-256: ' + hashlib.sha256(data).hexdigest(),
             '// Kernel SHA-256: ' + hashlib.sha256(kernel_bytes).hexdigest(), 'let entries = [']
    for index, entry in enumerate(entries):
        fields = ['name:"' + entry['name'] + '"', 'kind:"' + entry['kind'] + '"']
        if 'initial' in entry:
            fields.append('initial:' + str(entry['initial']))
        if 'derived' in entry:
            fields.append('derived:"' + entry['derived'] + '"')
        if 'array_name' in entry:
            fields.append('array_name:"' + entry['array_name'] + '"')
        lines.append('    {' + ', '.join(fields) + '}' + (',' if index + 1 < len(entries) else ''))
    lines.extend(['];', 'pub fn prepared() => entries', 'let allocator_sources = ['])
    macro_names = ('newcount', 'alloc@', 'ch@ck', 'wlog')
    for index, name in enumerate(macro_names):
        pattern = r'(?m)^\\def\\' + re.escape(name) + r'((?:#[1-9])*)\{((?:[^{}]|\{[^{}]*\})*)\}'
        matches = list(re.finditer(pattern, kernel))
        if len(matches) != 1:
            raise ValueError('unsupported kernel allocator definition: ' + name)
        params, body = matches[0].groups()
        count = len(params) // 2
        if params != ''.join('#' + str(i) for i in range(1, count + 1)):
            raise ValueError('unsupported kernel allocator parameters: ' + name)
        lines.append('    {name:' + json.dumps(name) + ', count:' + str(count) +
                     ', body:' + json.dumps(body) + '}' + (',' if index + 1 < len(macro_names) else ''))
    lines.extend(['];', 'pub fn allocator_macros() => allocator_sources'])
    return '\n'.join(lines) + '\n'


def main():
    parser = argparse.ArgumentParser(description=__doc__)
    parser.add_argument('--check', action='store_true')
    args = parser.parse_args()
    source, kernel = SOURCE.read_bytes(), KERNEL.read_bytes()
    result = render(source, kernel)
    if SOURCE.read_bytes() != source or KERNEL.read_bytes() != kernel:
        raise SystemExit('expl3 source or kernel changed during generation')
    if args.check:
        if not DEST.is_file() or DEST.read_text() != result:
            raise SystemExit('LaTeX format count catalog differs; regenerate it')
    else:
        DEST.write_text(result)


if __name__ == '__main__':
    main()
