#!/usr/bin/env python3
"""
JavaScript Generative Fuzzer for Lambda JS Engine

Generates adversarial JavaScript programs covering:
- Expressions, operators, type coercions
- Functions (declarations, arrows, closures, generators, async)
- Classes (inheritance, static, private, getters/setters)
- Control flow (loops, exceptions, labeled breaks)
- Destructuring, spread, rest parameters
- Prototypes, proxies, symbols, iterators
- Edge cases (eval, arguments, with, typeof on undeclared)
- Numeric edge cases (NaN, Infinity, -0, BigInt-like)
- Deeply nested / recursive constructs

Usage:
  python3 js_gen.py --count=50 --output-dir=./temp/js_fuzz/gen
  python3 js_gen.py --count=10 --mode=class
"""

import argparse
import os
import random
import sys

# ── Atoms ────────────────────────────────────────────────────────────────────

IDENTS = ['a', 'b', 'c', 'x', 'y', 'z', 'i', 'j', 'n', 'val', 'obj', 'arr',
          'fn', 'cb', 'tmp', 'res', 'sum', 'key', 'len', 'idx', 'str']

RESERVED = ['break', 'case', 'catch', 'class', 'const', 'continue', 'debugger',
            'default', 'delete', 'do', 'else', 'export', 'extends', 'finally',
            'for', 'function', 'if', 'import', 'in', 'instanceof', 'let', 'new',
            'return', 'super', 'switch', 'this', 'throw', 'try', 'typeof',
            'var', 'void', 'while', 'with', 'yield', 'async', 'await']

LITERALS = ['0', '1', '-1', '42', '3.14', '-0', 'NaN', 'Infinity', '-Infinity',
            '0x1F', '0o17', '0b1010', '1e10', '1e-10', '1e308', '5e-324',
            '9007199254740991', '9007199254740992',  # MAX_SAFE_INTEGER, +1
            'null', 'undefined', 'true', 'false',
            '""', '"hello"', '"\\n\\t"', '"\\\\"', '"\\u0041"',
            "''", "'abc'", '`template`', '`${1+2}`']

UNARY_OPS = ['-', '+', '!', '~', 'typeof ', 'void ', 'delete ']
BINARY_OPS = ['+', '-', '*', '/', '%', '**',
              '==', '!=', '===', '!==', '<', '>', '<=', '>=',
              '&', '|', '^', '<<', '>>', '>>>',
              '&&', '||', '??', 'in', 'instanceof']
ASSIGN_OPS = ['=', '+=', '-=', '*=', '/=', '%=', '**=',
              '&=', '|=', '^=', '<<=', '>>=', '>>>=', '&&=', '||=', '??=']

# ── Helpers ──────────────────────────────────────────────────────────────────

def ident():
    return random.choice(IDENTS)

def fresh_ident(prefix='_v'):
    return f'{prefix}{random.randint(0, 999)}'

def literal():
    return random.choice(LITERALS)

def maybe(fn, prob=0.3):
    return fn() if random.random() < prob else ''

def one_of(*fns):
    return random.choice(fns)()

def indent(code, level=1):
    prefix = '  ' * level
    return '\n'.join(prefix + line for line in code.split('\n'))

# ── Expression generators ────────────────────────────────────────────────────

def gen_expr(depth=0):
    if depth > 4:
        return literal()
    return one_of(
        lambda: literal(),
        lambda: ident(),
        lambda: f'{random.choice(UNARY_OPS)}{gen_expr(depth+1)}',
        lambda: f'({gen_expr(depth+1)} {random.choice(BINARY_OPS)} {gen_expr(depth+1)})',
        lambda: f'({gen_expr(depth+1)} ? {gen_expr(depth+1)} : {gen_expr(depth+1)})',
        lambda: f'[{", ".join(gen_expr(depth+1) for _ in range(random.randint(0,5)))}]',
        lambda: f'{{{", ".join(f"{ident()}: {gen_expr(depth+1)}" for _ in range(random.randint(0,4)))}}}',
        lambda: f'({gen_expr(depth+1)})',
        lambda: f'{ident()}({", ".join(gen_expr(depth+1) for _ in range(random.randint(0,3)))})',
        lambda: f'{ident()}[{gen_expr(depth+1)}]',
        lambda: f'{ident()}.{ident()}',
        lambda: f'new {ident()}({gen_expr(depth+1)})',
        lambda: f'({ident()} => {gen_expr(depth+1)})',
        lambda: f'...{ident()}',
    )

def gen_assignment():
    lhs = random.choice([
        ident(),
        f'{ident()}.{ident()}',
        f'{ident()}[{gen_expr()}]',
    ])
    return f'{lhs} {random.choice(ASSIGN_OPS)} {gen_expr()};'

# ── Statement generators ─────────────────────────────────────────────────────

def gen_var_decl():
    kind = random.choice(['var', 'let', 'const'])
    name = fresh_ident()
    val = gen_expr()
    return f'{kind} {name} = {val};'

def gen_if(depth=0):
    cond = gen_expr()
    body = gen_block(depth+1)
    else_part = ''
    if random.random() < 0.4:
        else_part = f' else {{\n{gen_block(depth+1)}\n}}'
    return f'if ({cond}) {{\n{body}\n}}{else_part}'

def gen_for_loop(depth=0):
    kind = random.randint(0, 3)
    if kind == 0:
        v = fresh_ident('_i')
        return f'for (let {v} = 0; {v} < {random.randint(1,10)}; {v}++) {{\n{gen_block(depth+1)}\n}}'
    elif kind == 1:
        return f'for (let k in {gen_expr()}) {{\n{gen_block(depth+1)}\n}}'
    elif kind == 2:
        return f'for (let v of {gen_expr()}) {{\n{gen_block(depth+1)}\n}}'
    else:
        return f'while ({gen_expr()}) {{\n{gen_block(depth+1)}\n  break;\n}}'

def gen_try(depth=0):
    lines = [f'try {{\n{gen_block(depth+1)}\n}}']
    if random.random() < 0.7:
        lines.append(f' catch(e) {{\n{gen_block(depth+1)}\n}}')
    if random.random() < 0.3:
        lines.append(f' finally {{\n{gen_block(depth+1)}\n}}')
    return ''.join(lines)

def gen_switch(depth=0):
    val = gen_expr()
    cases = []
    for _ in range(random.randint(1, 4)):
        cases.append(f'  case {literal()}:\n{gen_block(depth+2)}\n    break;')
    if random.random() < 0.5:
        cases.append(f'  default:\n{gen_block(depth+2)}')
    return f'switch ({val}) {{\n' + '\n'.join(cases) + '\n}'

def gen_function(depth=0):
    name = fresh_ident('_fn')
    params = ', '.join(fresh_ident('_p') for _ in range(random.randint(0, 4)))
    kind = random.randint(0, 3)
    if kind == 0:
        body = gen_block(depth+1)
        return f'function {name}({params}) {{\n{body}\n  return {gen_expr()};\n}}'
    elif kind == 1:
        return f'const {name} = ({params}) => {gen_expr()};'
    elif kind == 2:
        return f'const {name} = ({params}) => {{\n{gen_block(depth+1)}\n  return {gen_expr()};\n}};'
    else:
        body = gen_block(depth+1)
        return f'function* {name}({params}) {{\n  yield {gen_expr()};\n{body}\n  yield {gen_expr()};\n}}'

def gen_class(depth=0):
    name = fresh_ident('_C')
    extends = ''
    if random.random() < 0.3:
        extends = f' extends {fresh_ident("_Base")}'
    members = []
    # constructor
    if random.random() < 0.7:
        p = fresh_ident('_p')
        members.append(f'  constructor({p}) {{\n    {"super();" if extends else ""}\n    this.{ident()} = {p};\n  }}')
    # methods
    for _ in range(random.randint(0, 3)):
        m = fresh_ident('_m')
        static = 'static ' if random.random() < 0.2 else ''
        members.append(f'  {static}{m}() {{ return {gen_expr()}; }}')
    # getter/setter
    if random.random() < 0.3:
        g = fresh_ident('_g')
        members.append(f'  get {g}() {{ return this.{ident()}; }}')
    if random.random() < 0.2:
        s = fresh_ident('_s')
        members.append(f'  set {s}(v) {{ this.{ident()} = v; }}')
    body = '\n'.join(members)
    return f'class {name}{extends} {{\n{body}\n}}'

def gen_destructuring():
    kind = random.randint(0, 3)
    if kind == 0:
        names = ', '.join(fresh_ident() for _ in range(random.randint(1, 4)))
        return f'let [{names}] = {gen_expr()};'
    elif kind == 1:
        fields = ', '.join(ident() for _ in range(random.randint(1, 3)))
        return f'let {{{fields}}} = {gen_expr()};'
    elif kind == 2:
        rest = fresh_ident('_rest')
        return f'let [{fresh_ident()}, ...{rest}] = {gen_expr()};'
    else:
        rename = f'{ident()}: {fresh_ident()}'
        return f'let {{{rename}}} = {gen_expr()};'

def gen_prototype_manipulation():
    obj = fresh_ident('_obj')
    lines = [
        f'let {obj} = {{}};',
        one_of(
            lambda: f'{obj}.__proto__ = {gen_expr()};',
            lambda: f'Object.setPrototypeOf({obj}, {gen_expr()});',
            lambda: f'Object.defineProperty({obj}, "{ident()}", {{value: {gen_expr()}, writable: true, enumerable: true}});',
            lambda: f'Object.create({gen_expr()});',
            lambda: f'Object.freeze({obj});',
            lambda: f'Object.seal({obj});',
            lambda: f'Object.keys({obj});',
            lambda: f'Object.getOwnPropertyDescriptor({obj}, "{ident()}");',
        ),
    ]
    return '\n'.join(lines)

def gen_symbol_usage():
    s = fresh_ident('_sym')
    return one_of(
        lambda: f'let {s} = Symbol("{ident()}"); let obj = {{[{s}]: {gen_expr()}}}; obj[{s}];',
        lambda: f'let {s} = Symbol.for("{ident()}");\nSymbol.keyFor({s});',
        lambda: f'let obj = {{[Symbol.iterator]() {{ let i = 0; return {{next() {{ return i < 3 ? {{value: i++, done: false}} : {{done: true}}; }}}}; }}}};',
        lambda: f'let obj = {{[Symbol.toPrimitive](hint) {{ return hint === "number" ? 42 : "str"; }}}};',
    )

def gen_proxy():
    t = fresh_ident('_target')
    p = fresh_ident('_proxy')
    traps = []
    if random.random() < 0.5:
        traps.append(f'get(t, prop) {{ return t[prop]; }}')
    if random.random() < 0.5:
        traps.append(f'set(t, prop, val) {{ t[prop] = val; return true; }}')
    if random.random() < 0.3:
        traps.append(f'has(t, prop) {{ return prop in t; }}')
    if random.random() < 0.3:
        traps.append(f'deleteProperty(t, prop) {{ delete t[prop]; return true; }}')
    handler = '{' + ', '.join(traps) + '}'
    return f'let {t} = {{a: 1, b: 2}};\nlet {p} = new Proxy({t}, {handler});\n{p}.a;\n{p}.c = 3;'

def gen_promise():
    return one_of(
        lambda: 'Promise.resolve(42).then(v => v * 2);',
        lambda: 'Promise.reject("err").catch(e => e);',
        lambda: f'Promise.all([{", ".join(f"Promise.resolve({gen_expr()})" for _ in range(random.randint(1,3)))}]);',
        lambda: 'new Promise((resolve, reject) => { resolve(1); });',
    )

def gen_edge_case():
    return one_of(
        # typeof on undeclared variable (should not crash)
        lambda: f'typeof {fresh_ident("_undef")};',
        # delete on various things
        lambda: f'var _obj = {{a:1}}; delete _obj.a; delete _obj[0]; delete _obj;',
        # void expressions
        lambda: f'void {gen_expr()};',
        # comma operator
        lambda: f'({gen_expr()}, {gen_expr()}, {gen_expr()});',
        # labeled statement
        lambda: f'outer: for (let i=0; i<3; i++) {{ inner: for (let j=0; j<3; j++) {{ if (j===1) break outer; }} }}',
        # empty statements
        lambda: ';;;',
        # assignment to const (error handling)
        lambda: f'const _cst = 1; try {{ _cst = 2; }} catch(e) {{}}',
        # arguments object
        lambda: f'function _fa() {{ return arguments.length; }} _fa(1,2,3);',
        # callee-like patterns
        lambda: f'(function _self(n) {{ return n <= 0 ? 1 : n * _self(n-1); }})(5);',
        # chained optional
        lambda: f'var _o = null; _o?.a?.b?.c;',
        # nullish coalescing
        lambda: f'null ?? undefined ?? 0 ?? "" ?? false ?? 42;',
        # tagged template (syntax)
        lambda: f'function _tag(s, ...v) {{ return s.join(""); }} _tag`hello ${{1+2}} world`;',
        # computed property
        lambda: f'var _k = "x"; var _o = {{[_k]: 1, ["y"]: 2}}; _o.x + _o.y;',
        # shorthand methods
        lambda: f'var _o = {{ m() {{ return 1; }}, get p() {{ return 2; }} }}; _o.m() + _o.p;',
        # numeric separators (if supported)
        lambda: '1_000_000;',
        # regex
        lambda: '/^abc$/i.test("ABC");',
    )

def gen_numeric_edge():
    return one_of(
        lambda: 'Number.MAX_SAFE_INTEGER + 1 === Number.MAX_SAFE_INTEGER + 2;',
        lambda: '0.1 + 0.2;',
        lambda: '-0 === 0;',
        lambda: 'Object.is(-0, 0);',
        lambda: '1/0; -1/0; 0/0;',
        lambda: 'NaN === NaN; NaN !== NaN; isNaN(NaN);',
        lambda: 'Number.isFinite(Infinity);',
        lambda: 'parseInt("0x1F"); parseInt("111", 2); parseInt("", 10);',
        lambda: 'parseFloat("3.14e2"); parseFloat("Infinity");',
        lambda: 'Math.pow(2, 53);',
        lambda: '~~3.9; ~~-3.9; ~~NaN; ~~Infinity;',
        lambda: '"5" - 3; "5" + 3; "5" * "2";',
        lambda: 'true + true; false + null; undefined + 1;',
    )

def gen_string_edge():
    return one_of(
        lambda: '"abc".charAt(999); "abc"[-1]; "abc"[NaN];',
        lambda: '"".split(""); "a,b,c".split(",", 2);',
        lambda: '"hello".slice(-3); "hello".slice(1, -1);',
        lambda: '"abc".repeat(0); "abc".repeat(1);',
        lambda: '"  trim  ".trim(); "  trim  ".trimStart(); "  trim  ".trimEnd();',
        lambda: '"abc".padStart(6, "0"); "abc".padEnd(6, ".");',
        lambda: '"abc".includes("b"); "abc".startsWith("a"); "abc".endsWith("c");',
        lambda: '"hello world".replace("world", "JS"); "aaa".replaceAll("a", "b");',
        lambda: 'String.fromCharCode(65, 66, 67);',
        lambda: '"A".charCodeAt(0); "A".codePointAt(0);',
    )

def gen_array_edge():
    return one_of(
        lambda: '[1,2,3].map(x => x*2).filter(x => x > 2).reduce((a,b) => a+b, 0);',
        lambda: '[3,1,2].sort(); [3,1,2].sort((a,b) => b-a);',
        lambda: '[1,[2,[3]]].flat(Infinity);',
        lambda: 'Array.from({length: 5}, (_, i) => i);',
        lambda: 'Array.isArray([]); Array.isArray("abc");',
        lambda: '[1,2,3].find(x => x > 1); [1,2,3].findIndex(x => x > 1);',
        lambda: '[1,2,3].every(x => x > 0); [1,2,3].some(x => x > 2);',
        lambda: '[1,2,3].includes(2); [1,2,3].indexOf(2); [1,2,3].lastIndexOf(2);',
        lambda: 'let a = [1,2]; a.push(3); a.pop(); a.shift(); a.unshift(0); a;',
        lambda: '[1,2,3].slice(1); [1,2,3,4,5].splice(1, 2);',
        lambda: '[].reduce((a,b) => a+b);',  # should throw
    )

# ── Block generators ─────────────────────────────────────────────────────────

def gen_stmt(depth=0):
    if depth > 3:
        return gen_var_decl()
    return one_of(
        lambda: gen_var_decl(),
        lambda: gen_if(depth),
        lambda: gen_for_loop(depth),
        lambda: gen_try(depth),
        lambda: gen_assignment(),
        lambda: f'{gen_expr()};',
        lambda: gen_switch(depth),
        lambda: gen_destructuring(),
    )

def gen_block(depth=0, max_stmts=5):
    n = random.randint(1, max_stmts)
    stmts = [gen_stmt(depth) for _ in range(n)]
    return indent('\n'.join(stmts))

# ── Program generators (modes) ───────────────────────────────────────────────

def gen_mode_expr():
    """Pure expression and operator fuzzing."""
    lines = ['"use strict";']
    for _ in range(random.randint(5, 20)):
        lines.append(one_of(
            lambda: gen_var_decl(),
            lambda: f'{gen_expr()};',
            lambda: gen_assignment(),
            lambda: gen_numeric_edge(),
            lambda: gen_string_edge(),
        ))
    return '\n'.join(lines)

def gen_mode_func():
    """Function declaration, arrow, generator, closure fuzzing."""
    lines = ['"use strict";']
    for _ in range(random.randint(3, 8)):
        lines.append(gen_function())
    # call some
    for _ in range(random.randint(2, 5)):
        lines.append(f'try {{ {fresh_ident("_fn")}({gen_expr()}); }} catch(e) {{}}')
    return '\n'.join(lines)

def gen_mode_class():
    """Class, inheritance, prototype fuzzing."""
    lines = ['"use strict";']
    base = fresh_ident('_Base')
    lines.append(f'class {base} {{ constructor() {{ this.x = 1; }} m() {{ return this.x; }} }}')
    for _ in range(random.randint(2, 5)):
        lines.append(gen_class())
    for _ in range(random.randint(1, 3)):
        lines.append(gen_prototype_manipulation())
    return '\n'.join(lines)

def gen_mode_control():
    """Control flow: loops, switches, exceptions, labels."""
    lines = ['"use strict";']
    for _ in range(random.randint(3, 10)):
        lines.append(one_of(
            lambda: gen_if(),
            lambda: gen_for_loop(),
            lambda: gen_switch(),
            lambda: gen_try(),
        ))
    return '\n'.join(lines)

def gen_mode_destruct():
    """Destructuring, spread, rest parameters."""
    lines = ['"use strict";']
    for _ in range(random.randint(5, 15)):
        lines.append(one_of(
            lambda: gen_destructuring(),
            lambda: f'let [{", ".join(fresh_ident() for _ in range(3))}, ...{fresh_ident("_rest")}] = [1,2,3,4,5];',
            lambda: f'function _f({{a, b = 10}}) {{ return a + b; }} _f({{a: 1}});',
            lambda: f'let _a = [1,2]; let _b = [..._a, 3, ..._a];',
            lambda: f'let _o = {{a:1}}; let _p = {{..._o, b:2}};',
        ))
    return '\n'.join(lines)

def gen_mode_edge():
    """Edge cases, coercions, unusual patterns."""
    lines = ['"use strict";']
    for _ in range(random.randint(5, 20)):
        lines.append(one_of(
            lambda: gen_edge_case(),
            lambda: gen_numeric_edge(),
            lambda: gen_string_edge(),
            lambda: gen_array_edge(),
        ))
    return '\n'.join(lines)

def gen_mode_advanced():
    """Symbols, proxies, iterators, promises."""
    lines = []
    for _ in range(random.randint(1, 3)):
        lines.append(gen_symbol_usage())
    for _ in range(random.randint(0, 2)):
        lines.append(gen_proxy())
    for _ in range(random.randint(0, 2)):
        lines.append(gen_promise())
    return '\n'.join(lines)

def gen_mode_mixed():
    """Mix of everything."""
    generators = [gen_mode_expr, gen_mode_func, gen_mode_class,
                  gen_mode_control, gen_mode_destruct, gen_mode_edge,
                  gen_mode_advanced]
    parts = [random.choice(generators)() for _ in range(random.randint(2, 4))]
    return '\n\n'.join(parts)

MODE_MAP = {
    'expr': gen_mode_expr,
    'func': gen_mode_func,
    'class': gen_mode_class,
    'control': gen_mode_control,
    'destruct': gen_mode_destruct,
    'edge': gen_mode_edge,
    'advanced': gen_mode_advanced,
    'mixed': gen_mode_mixed,
}

# ── Main ─────────────────────────────────────────────────────────────────────

def main():
    parser = argparse.ArgumentParser(description='JS generative fuzzer')
    parser.add_argument('--count', type=int, default=50, help='Number of files to generate')
    parser.add_argument('--output-dir', default='./temp/js_fuzz/gen', help='Output directory')
    parser.add_argument('--mode', default='all', choices=list(MODE_MAP.keys()) + ['all'],
                        help='Generator mode (default: all)')
    parser.add_argument('--seed', type=int, default=None, help='Random seed')
    args = parser.parse_args()

    if args.seed is not None:
        random.seed(args.seed)

    os.makedirs(args.output_dir, exist_ok=True)

    modes = list(MODE_MAP.keys()) if args.mode == 'all' else [args.mode]

    for i in range(args.count):
        mode = random.choice(modes)
        try:
            code = MODE_MAP[mode]()
        except RecursionError:
            code = '"use strict";\n1 + 2;'
        filename = f'gen_{mode}_{i:04d}.js'
        filepath = os.path.join(args.output_dir, filename)
        with open(filepath, 'w') as f:
            f.write(code + '\n')

    print(f'Generated {args.count} JS files in {args.output_dir}')

if __name__ == '__main__':
    main()
