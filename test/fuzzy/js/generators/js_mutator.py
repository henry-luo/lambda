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

# ── Engine-targeted mutations ────────────────────────────────────────────────

def mut_inject_type_change(code):
    """Insert code that rapidly changes a variable's type (stresses inline caches)."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        'var _tc = 1; _tc = "str"; _tc = [1]; _tc = {x:1}; _tc = null; _tc = true; _tc + 1;',
        'var _tc = {}; _tc.x = 1; _tc.x = "s"; _tc.x = []; _tc.x = null; _tc.x;',
        'var _tc = []; _tc.push(1); _tc.push("s"); _tc.push(null); _tc.push({}); _tc.length;',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_inject_property_delete(code):
    """Insert property deletion to invalidate hidden class / shape."""
    # find a variable assignment and add delete after it
    match = re.search(r'var (\w+)\s*=\s*\{', code)
    if match:
        name = match.group(1)
        lines = code.split('\n')
        # insert after the match line
        for i, line in enumerate(lines):
            if match.group(0) in line:
                lines.insert(i + 1, f'try {{ delete {name}[Object.keys({name})[0]]; }} catch(e) {{}}')
                break
        return '\n'.join(lines)
    return code

def mut_inject_defineProperty(code):
    """Insert Object.defineProperty to create non-standard property descriptors."""
    match = re.search(r'var (\w+)\s*=\s*\{', code)
    if match:
        name = match.group(1)
        lines = code.split('\n')
        snippet = random.choice([
            f'try {{ Object.defineProperty({name}, "_fuzz", {{get() {{ return 42; }}, set(v) {{}}, configurable: true}}); }} catch(e) {{}}',
            f'try {{ Object.defineProperty({name}, "_fuzz", {{value: 0, writable: false, enumerable: false}}); }} catch(e) {{}}',
            f'try {{ Object.freeze({name}); {name}._new = 1; }} catch(e) {{}}',
            f'try {{ Object.seal({name}); delete {name}[Object.keys({name})[0]]; }} catch(e) {{}}',
            f'try {{ Object.preventExtensions({name}); {name}._new = 1; }} catch(e) {{}}',
        ])
        lines.insert(len(lines) // 2, snippet)
        return '\n'.join(lines)
    return code

def mut_inject_prototype_swap(code):
    """Insert __proto__ manipulation to break prototype chain assumptions."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        'var _o = {a:1}; _o.__proto__ = {b:2}; _o.__proto__ = null; try { _o.toString(); } catch(e) {}',
        'var _o = {a:1}; Object.setPrototypeOf(_o, {b:2}); Object.setPrototypeOf(_o, null); try { _o.hasOwnProperty("a"); } catch(e) {}',
        'function _F() {} _F.prototype = {x: 1}; var _i = new _F(); _F.prototype = {y: 2}; _i.x;',
        'var _o = Object.create(Object.create(Object.create({deep: 1}))); _o.deep;',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_inject_valueOf_trap(code):
    """Insert objects with valueOf/toString traps that create re-entrancy."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        'var _t = {valueOf() { return NaN; }}; _t + 1; _t < 2; _t == null;',
        'var _t = {toString() { throw new Error("trap"); }}; try { "" + _t; } catch(e) {}',
        'var _t = {valueOf() { return {}; }, toString() { return "ok"; }}; try { +_t; } catch(e) {} "" + _t;',
        'var _t = {[Symbol.toPrimitive](h) { if (h==="number") throw 1; return "s"; }}; try { +_t; } catch(e) {} `${_t}`;',
        'var _sideEffect = 0; var _t = {valueOf() { _sideEffect++; return _sideEffect; }}; [_t + _t, _t + _t, _sideEffect];',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_inject_gc_pressure(code):
    """Insert allocation-heavy code that stresses GC."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    n = random.choice([200, 500, 1000, 2000])
    snippet = random.choice([
        f'for (var _g = 0; _g < {n}; _g++) {{ var _tmp = {{k: _g, v: [_g, _g+1]}}; }}',
        f'var _s = ""; for (var _g = 0; _g < {n}; _g++) {{ _s += String.fromCharCode(65 + (_g % 26)); }}',
        f'var _a = []; for (var _g = 0; _g < {n}; _g++) {{ _a.push(function(x) {{ return x + _g; }}); }} _a.length;',
        f'var _m = new Map(); for (var _g = 0; _g < {n}; _g++) {{ _m.set("k"+_g, [_g]); }} _m.size;',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_inject_exception_storm(code):
    """Insert rapid throw/catch to stress exception handling paths."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        'var _ec = 0; for (var _i = 0; _i < 100; _i++) { try { throw _i; } catch(e) { _ec += e; } }',
        'try { try { try { throw 1; } finally { throw 2; } } finally { throw 3; } } catch(e) {}',
        'function _thrower() { throw new Error("x"); } for (var _i = 0; _i < 50; _i++) { try { _thrower(); } catch(e) {} }',
        'for (var _i = 0; _i < 50; _i++) { try { ({}).nonexistent.prop; } catch(e) {} }',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_inject_scope_confusion(code):
    """Insert variable shadowing / redeclaration that could confuse scope analysis."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        '{ let _sc = 1; { let _sc = 2; { let _sc = 3; } } }',
        'var _sc = 1; (function() { var _sc = 2; (function() { var _sc = 3; return _sc; })(); return _sc; })();',
        'var _sc = "outer"; { let _sc = "inner"; } _sc;',
        'function _sf(_sc) { return function() { return _sc; }; } _sf(42)();',
        'var _sc = 1; try { throw 2; } catch(_sc) { _sc; } _sc;',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_wrap_in_class(code):
    """Wrap code inside a class method to test class context handling."""
    return f'class _FuzzClass {{\n  static run() {{\n{code}\n  }}\n}}\ntry {{ _FuzzClass.run(); }} catch(e) {{}}'

def mut_wrap_in_generator(code):
    """Wrap code in a generator function and iterate it."""
    return f'function* _fuzzGen() {{\n  yield 1;\n{code}\n  yield 2;\n}}\nvar _g = _fuzzGen();\ntry {{ while(true) {{ var _r = _g.next(); if (_r.done) break; }} }} catch(e) {{}}'

def mut_inject_getterSetter_on_array(code):
    """Define getter/setter on Array.prototype or array index (engine stress)."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        'var _a = [1,2,3]; Object.defineProperty(_a, 1, {get() { return 99; }}); _a[1];',
        'var _a = [1,2,3,4,5]; _a.length = 2; _a;',
        'var _a = []; _a[100] = 1; _a.length; _a[50];',
        'var _a = [1,2,3]; _a.constructor = null; try { _a.map(x => x); } catch(e) {}',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_inject_computed_property(code):
    """Insert computed/dynamic property access patterns."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        'var _o = {}; for (var _i = 0; _i < 50; _i++) { _o["p" + _i] = _i; } Object.keys(_o).length;',
        'var _o = {}; var _keys = ["a","b","c","d","e"]; for (var _k of _keys) { _o[_k] = _k.charCodeAt(0); }',
        'var _o = {a:1,b:2,c:3}; var _k = ["a","b","c","d"]; _k.map(k => _o[k]);',
        'var _sym = Symbol("fuzz"); var _o = {[_sym]: 42, ["computed"]: 1}; _o[_sym]; _o.computed;',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

def mut_inject_arguments_abuse(code):
    """Insert patterns that abuse the arguments object."""
    lines = code.split('\n')
    pos = random.randint(0, len(lines))
    snippet = random.choice([
        'function _fa() { arguments[0] = 99; return arguments; } _fa(1,2,3);',
        'function _fa() { var a = Array.prototype.slice.call(arguments); a.push(4); return a; } _fa(1,2,3);',
        'function _fa() { return [...arguments]; } _fa(1,2,3);',
        'function _fa(a,b) { delete arguments[0]; return [a, arguments[0], arguments.length]; } _fa(1,2);',
    ])
    lines.insert(pos, snippet)
    return '\n'.join(lines)

MUTATIONS = [
    # Character-level (lightweight)
    mut_delete_char,
    mut_insert_char,
    mut_replace_char,
    mut_swap_chars,
    # Line-level
    mut_delete_line,
    mut_duplicate_line,
    mut_swap_lines,
    # Statement injection
    mut_inject_statement,
    # Structural
    mut_remove_brace,
    mut_replace_operator,
    mut_swap_var_kind,
    mut_remove_semicolons,
    mut_add_deep_nesting,
    mut_corrupt_string,
    mut_remove_this,
    # Allocation/crash-targeting
    mut_insert_null_access,
    mut_insert_large_array,
    mut_duplicate_function,
    # Engine-internal stress (new)
    mut_inject_type_change,
    mut_inject_type_change,         # double weight — high value
    mut_inject_property_delete,
    mut_inject_defineProperty,
    mut_inject_prototype_swap,
    mut_inject_prototype_swap,      # double weight
    mut_inject_valueOf_trap,
    mut_inject_valueOf_trap,         # double weight
    mut_inject_gc_pressure,
    mut_inject_gc_pressure,          # double weight
    mut_inject_exception_storm,
    mut_inject_scope_confusion,
    mut_inject_scope_confusion,      # double weight
    mut_wrap_in_class,
    mut_wrap_in_generator,
    mut_inject_getterSetter_on_array,
    mut_inject_computed_property,
    mut_inject_arguments_abuse,
]

def mutate(code, num_mutations=None):
    if num_mutations is None:
        num_mutations = random.randint(1, 5)
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
