#!/usr/bin/env python3
"""
JavaScript Mutation Fuzzer for Lambda JS Engine

Takes seed JS files and applies random mutations:
- Character-level: insert, delete, replace, swap
- Token-level: drop/duplicate/swap tokens, replace operators
- Statement-level: delete/duplicate/reorder lines, inject statements
- Structural: remove braces, break nesting, corrupt strings
- Semantic: swap var/let/const, remove 'this.', change === to ==, etc.

Usage:
  python3 js_mutator.py --seeds-dir=../../js --output-dir=./temp/js_fuzz/mut --count=3
  python3 js_mutator.py --seeds-dir=../../js --max-seeds=50 --count=2
"""

import argparse
import os
import random
import re
import sys

# ── Mutations ────────────────────────────────────────────────────────────────

def mut_delete_char(code):
    if len(code) < 2:
        return code
    pos = random.randint(0, len(code) - 1)
    return code[:pos] + code[pos+1:]

def mut_insert_char(code):
    chars = '()[]{}+-*/%^<>=!&|;:,."\'\\ \n\t0123456789abcdefghijklmnopqrstuvwxyz'
    pos = random.randint(0, len(code))
    ch = random.choice(chars)
    return code[:pos] + ch + code[pos:]

def mut_replace_char(code):
    if len(code) < 1:
        return code
    chars = '()[]{}+-*/%^<>=!&|;:,."\'\\ \n0123456789abcdefghijklmnopqrstuvwxyz'
    pos = random.randint(0, len(code) - 1)
    ch = random.choice(chars)
    return code[:pos] + ch + code[pos+1:]

def mut_swap_chars(code):
    if len(code) < 2:
        return code
    pos = random.randint(0, len(code) - 2)
    return code[:pos] + code[pos+1] + code[pos] + code[pos+2:]

def mut_delete_line(code):
    lines = code.split('\n')
    if len(lines) < 2:
        return code
    idx = random.randint(0, len(lines) - 1)
    lines.pop(idx)
    return '\n'.join(lines)

def mut_duplicate_line(code):
    lines = code.split('\n')
    if not lines:
        return code
    idx = random.randint(0, len(lines) - 1)
    lines.insert(idx, lines[idx])
    return '\n'.join(lines)

def mut_swap_lines(code):
    lines = code.split('\n')
    if len(lines) < 2:
        return code
    i, j = random.sample(range(len(lines)), 2)
    lines[i], lines[j] = lines[j], lines[i]
    return '\n'.join(lines)

def mut_inject_statement(code):
    stmts = [
        'throw new Error("fuzz");',
        'return;',
        'break;',
        'continue;',
        'debugger;',
        'var _fuzz = undefined;',
        'null.x;',
        'undefined();',
        '[][0].toString();',
        'arguments;',
        'this;',
        'delete this;',
        'yield 1;',
        'await 1;',
        'super();',
        'new.target;',
        'eval("1+2");',
        'with({}) {}',
    ]
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    lines.insert(pos, random.choice(stmts))
    return '\n'.join(lines)

def mut_remove_brace(code):
    braces = [i for i, c in enumerate(code) if c in '{}()[]']
    if not braces:
        return code
    pos = random.choice(braces)
    return code[:pos] + code[pos+1:]

def mut_replace_operator(code):
    ops = {'+': '-', '-': '+', '*': '/', '/': '*', '%': '**',
           '===': '==', '!==': '!=', '==': '===', '!=': '!==',
           '&&': '||', '||': '&&', '??': '||',
           '<': '>', '>': '<', '<=': '>=', '>=': '<='}
    for old, new in ops.items():
        if old in code:
            # replace one occurrence
            idx = code.index(old)
            return code[:idx] + new + code[idx+len(old):]
    return code

def mut_swap_var_kind(code):
    mapping = {'var ': 'let ', 'let ': 'const ', 'const ': 'var '}
    for old, new in mapping.items():
        if old in code:
            idx = code.index(old)
            return code[:idx] + new + code[idx+len(old):]
    return code

def mut_remove_semicolons(code):
    return code.replace(';', '', random.randint(1, 3))

def mut_add_deep_nesting(code):
    wrap = random.choice([
        'if (true) {{ {} }}',
        'try {{ {} }} catch(e) {{}}',
        '(function() {{ {} }})()',
        '(() => {{ {} }})()',
        'for (let _=0; _<1; _++) {{ {} }}',
    ])
    return wrap.format(code)

def mut_corrupt_string(code):
    # find a string literal and break it
    match = re.search(r'"[^"]*"', code)
    if match:
        s, e = match.span()
        return code[:s] + '"' + code[s+1:]  # remove closing quote
    match = re.search(r"'[^']*'", code)
    if match:
        s, e = match.span()
        return code[:s] + "'" + code[s+1:]
    return code

def mut_remove_this(code):
    return code.replace('this.', '', 1)

def mut_insert_null_access(code):
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    lines.insert(pos, 'var _n = null; _n.prop;')
    return '\n'.join(lines)

def mut_insert_large_array(code):
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    n = random.choice([100, 1000, 10000])
    lines.insert(pos, f'var _big = Array({n}).fill(0);')
    return '\n'.join(lines)

def mut_insert_deep_recursion(code):
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    lines.insert(pos, 'function _rec(n) { return n <= 0 ? 0 : _rec(n-1); } try { _rec(100000); } catch(e) {}')
    return '\n'.join(lines)

def mut_insert_infinite_loop_guarded(code):
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    lines.insert(pos, 'var _cnt = 0; while(true) { if(++_cnt > 1000) break; }')
    return '\n'.join(lines)

def mut_duplicate_function(code):
    """Duplicate a function to create redeclaration."""
    match = re.search(r'(function \w+\([^)]*\)\s*\{)', code)
    if match:
        return code[:match.start()] + match.group() + '}\n' + code[match.start():]
    return code

MUTATIONS = [
    mut_delete_char,
    mut_insert_char,
    mut_replace_char,
    mut_swap_chars,
    mut_delete_line,
    mut_duplicate_line,
    mut_swap_lines,
    mut_inject_statement,
    mut_remove_brace,
    mut_replace_operator,
    mut_swap_var_kind,
    mut_remove_semicolons,
    mut_add_deep_nesting,
    mut_corrupt_string,
    mut_remove_this,
    mut_insert_null_access,
    mut_insert_large_array,
    mut_insert_deep_recursion,
    mut_insert_infinite_loop_guarded,
    mut_duplicate_function,
]

def mutate(code, num_mutations=None):
    if num_mutations is None:
        num_mutations = random.randint(1, 4)
    for _ in range(num_mutations):
        fn = random.choice(MUTATIONS)
        try:
            code = fn(code)
        except Exception:
            pass
    return code

# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='JS mutation fuzzer')
    parser.add_argument('--seeds-dir', required=True, help='Directory with seed .js files')
    parser.add_argument('--output-dir', default='./temp/js_fuzz/mut', help='Output directory')
    parser.add_argument('--count', type=int, default=3, help='Mutations per seed')
    parser.add_argument('--max-seeds', type=int, default=0, help='Max seed files (0=all)')
    parser.add_argument('--max-size', type=int, default=51200, help='Max seed file size in bytes')
    parser.add_argument('--seed', type=int, default=None, help='Random seed')
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    os.makedirs(args.output_dir, exist_ok=True)

    # Load seeds
    seeds = []
    for f in sorted(os.listdir(args.seeds_dir)):
        if not f.endswith('.js'):
            continue
        path = os.path.join(args.seeds_dir, f)
        if os.path.getsize(path) > args.max_size:
            continue
        with open(path, 'r', errors='replace') as fh:
            seeds.append((f, fh.read()))
        if args.max_seeds > 0 and len(seeds) >= args.max_seeds:
            break

    if not seeds:
        print(f'No .js seed files found in {args.seeds_dir}', file=sys.stderr)
        sys.exit(1)

    total = 0
    for name, code in seeds:
        base = os.path.splitext(name)[0]
        for i in range(args.count):
            mutated = mutate(code)
            out_name = f'mut_{base}_{i:03d}.js'
            out_path = os.path.join(args.output_dir, out_name)
            with open(out_path, 'w') as fh:
                fh.write(mutated)
            total += 1

    print(f'Generated {total} mutated JS files from {len(seeds)} seeds in {args.output_dir}')

if __name__ == '__main__':
    main()
