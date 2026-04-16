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

def gen_gc_pressure():
    """Generate code that creates lots of short-lived objects to stress GC."""
    v = fresh_ident('_gc')
    kind = random.randint(0, 7)
    if kind == 0:
        n = random.randint(100, 5000)
        return f'for (var {v} = 0; {v} < {n}; {v}++) {{ var _tmp = {{a: {v}, b: [{v}], c: "s" + {v}}}; }}'
    elif kind == 1:
        n = random.randint(50, 500)
        return f'var _arr = []; for (var {v} = 0; {v} < {n}; {v}++) {{ _arr.push({{k: {v}, v: String({v})}}); }} _arr.length;'
    elif kind == 2:
        n = random.randint(50, 300)
        return f'var _m = new Map(); for (var {v} = 0; {v} < {n}; {v}++) {{ _m.set("k" + {v}, [{v}]); }} _m.size;'
    elif kind == 3:
        n = random.randint(50, 300)
        return f'var _closures = []; for (var {v} = 0; {v} < {n}; {v}++) {{ _closures.push((function(x) {{ return function() {{ return x; }}; }})({v})); }} _closures[0]();'
    elif kind == 4:
        n = random.randint(100, 2000)
        return f'var _s = ""; for (var {v} = 0; {v} < {n}; {v}++) {{ _s = _s + String.fromCharCode(65 + ({v} % 26)); }} _s.length;'
    elif kind == 5:
        # rapid creation and abandonment
        n = random.randint(100, 1000)
        return f'for (var {v} = 0; {v} < {n}; {v}++) {{ var _t = [{v}, {v}+1, {v}+2].map(x => x*2).filter(x => x > {v}); }}'
    elif kind == 6:
        # nested object trees
        return 'var _tree = {}; var _cur = _tree; for (var _d = 0; _d < 50; _d++) { _cur.child = {val: _d}; _cur = _cur.child; } _tree;'
    else:
        # prototype chain allocation
        n = random.randint(20, 100)
        return f'function _PC() {{}} for (var {v} = 0; {v} < {n}; {v}++) {{ var _inst = new _PC(); _inst["p" + {v}] = {v}; }}'

def gen_type_confusion():
    """Generate code that rapidly changes types on the same variable to stress inline caches."""
    v = fresh_ident('_tc')
    types = [
        f'{v} = 42;',
        f'{v} = 3.14;',
        f'{v} = "hello";',
        f'{v} = true;',
        f'{v} = null;',
        f'{v} = undefined;',
        f'{v} = [1, 2, 3];',
        f'{v} = {{x: 1}};',
        f'{v} = function() {{ return 1; }};',
        f'{v} = Symbol("s");',
        f'{v} = /regex/;',
        f'{v} = new Map();',
        f'{v} = new Set();',
        f'{v} = new Date();',
    ]
    random.shuffle(types)
    uses = [
        f'{v} + 1;',
        f'{v} + "str";',
        f'{v} == null;',
        f'{v} === undefined;',
        f'typeof {v};',
        f'{v} instanceof Object;',
        f'String({v});',
        f'Number({v});',
        f'Boolean({v});',
        f'JSON.stringify({v});',
        f'Object.keys({v} || {{}});',
    ]
    lines = [f'var {v};']
    for t in types[:random.randint(5, len(types))]:
        lines.append(t)
        lines.append(f'try {{ {random.choice(uses)} }} catch(e) {{}}')
    return '\n'.join(lines)

def gen_scope_torture():
    """Generate complex scoping patterns: shadowing, TDZ, closures over mutated vars."""
    kind = random.randint(0, 7)
    if kind == 0:
        # deep shadowing
        return '''var x = 1;
(function() {
  var x = 2;
  (function() {
    var x = 3;
    (function() {
      var x = 4;
      (function() { var x = 5; })();
    })();
  })();
})();
x;'''
    elif kind == 1:
        # let in blocks with same name
        return '''let r = [];
{ let x = 1; r.push(x); }
{ let x = 2; r.push(x); }
{ let x = 3; r.push(x); }
r;'''
    elif kind == 2:
        # closure captures mutable variable
        return '''var fns = [];
for (var i = 0; i < 5; i++) { fns.push(function() { return i; }); }
var fns2 = [];
for (let j = 0; j < 5; j++) { fns2.push(function() { return j; }); }
[fns[0](), fns[4](), fns2[0](), fns2[4]()];'''
    elif kind == 3:
        # function hoisting vs let TDZ
        return '''try { x; } catch(e) {}
let x = 42;
function hoisted() { return 1; }
hoisted();'''
    elif kind == 4:
        # eval creating variables
        return '''var _ev = 10;
try { eval("var _ev = 20;"); } catch(e) {}
_ev;'''
    elif kind == 5:
        # arguments vs parameter names
        return '''function _f(a, b) {
  arguments[0] = 99;
  return [a, b, arguments[0], arguments[1], arguments.length];
}
_f(1, 2);'''
    elif kind == 6:
        # with-like property lookup (computed)
        v = fresh_ident('_scope')
        return f'var {v} = {{a: 1, b: 2, c: 3}}; var keys = Object.keys({v}); for (var _k of keys) {{ {v}[_k] += 10; }} {v};'
    else:
        # named function expression scope
        return '''var _r = (function _named() {
  try { return typeof _named; } catch(e) { return "error"; }
})();
try { typeof _named; } catch(e) {}'''

def gen_property_storm():
    """Generate code that creates many properties, delete/redefine, polymorphic shapes."""
    kind = random.randint(0, 5)
    n = random.randint(50, 200)
    v = fresh_ident('_ps')
    if kind == 0:
        # add many properties
        return f'var {v} = {{}}; for (var _i = 0; _i < {n}; _i++) {{ {v}["prop" + _i] = _i; }} Object.keys({v}).length;'
    elif kind == 1:
        # add then delete
        return f'var {v} = {{}}; for (var _i = 0; _i < {n}; _i++) {{ {v}["p" + _i] = _i; }} for (var _i = 0; _i < {n}; _i += 2) {{ delete {v}["p" + _i]; }} Object.keys({v}).length;'
    elif kind == 2:
        # defineProperty with various descriptors
        return f'''var {v} = {{}};
Object.defineProperty({v}, "ro", {{value: 1, writable: false}});
Object.defineProperty({v}, "hidden", {{value: 2, enumerable: false}});
Object.defineProperty({v}, "locked", {{value: 3, configurable: false}});
try {{ {v}.ro = 99; }} catch(e) {{}}
try {{ delete {v}.locked; }} catch(e) {{}}
Object.getOwnPropertyNames({v});'''
    elif kind == 3:
        # getter/setter that modify other properties
        return f'''var {v} = {{
  _count: 0,
  get count() {{ return this._count; }},
  set count(v) {{ this._count = v; this.last = v; }},
  history: []
}};
for (var _i = 0; _i < 20; _i++) {{ {v}.count = _i; {v}.history.push({v}.count); }}
{v}.history.length;'''
    elif kind == 4:
        # megamorphic: many different shapes at same call site
        lines = ['function _readX(o) { return o.x; }']
        for i in range(20):
            props = ', '.join(f'p{j}: {j}' for j in range(i + 1))
            lines.append(f'try {{ _readX({{x: {i}, {props}}}); }} catch(e) {{}}')
        return '\n'.join(lines)
    else:
        # prototype property shadowing
        return f'''function _Base() {{ this.x = 1; }}
_Base.prototype.x = 0;
_Base.prototype.y = 100;
var {v} = new _Base();
{v}.x;
{v}.y;
delete {v}.x;
{v}.x;
{v}.y = 200;
_Base.prototype.y;'''

def gen_reentrant():
    """Generate code with re-entrant patterns: toString/valueOf during operations."""
    kind = random.randint(0, 5)
    if kind == 0:
        return '''var _calls = 0;
var _tricky = { valueOf() { _calls++; return _calls; } };
_tricky + _tricky + _tricky;
_calls;'''
    elif kind == 1:
        return '''var _log = [];
var _a = { toString() { _log.push("a"); return "A"; } };
var _b = { toString() { _log.push("b"); return "B"; } };
_a + _b;
_log;'''
    elif kind == 2:
        # valueOf that throws
        return '''var _bad = { valueOf() { throw new Error("nope"); } };
try { _bad + 1; } catch(e) { e.message; }
try { _bad < 2; } catch(e) { e.message; }
try { +_bad; } catch(e) { e.message; }'''
    elif kind == 3:
        # toPrimitive
        return '''var _obj = {
  [Symbol.toPrimitive](hint) {
    if (hint === "number") return 42;
    if (hint === "string") return "hello";
    return true;
  }
};
+_obj;
`${_obj}`;
_obj + "";'''
    elif kind == 4:
        # comparator that mutates during sort
        return '''var _arr = [5, 3, 8, 1, 9, 2, 7];
try {
  _arr.sort(function(a, b) {
    if (_arr.length < 10) _arr.push(0);
    return a - b;
  });
} catch(e) {}
_arr.length;'''
    else:
        # getter that modifies object shape
        return '''var _obj = {
  _n: 0,
  get val() {
    this._n++;
    this["dynamic" + this._n] = this._n;
    return this._n;
  }
};
_obj.val; _obj.val; _obj.val;
Object.keys(_obj).length;'''

def gen_exception_stress():
    """Generate code that exercises exception paths heavily."""
    kind = random.randint(0, 5)
    if kind == 0:
        # rapid throw/catch
        return '''var _count = 0;
for (var _i = 0; _i < 200; _i++) {
  try { throw _i; } catch(e) { _count += e; }
}
_count;'''
    elif kind == 1:
        # finally overwrites return
        return '''function _f() {
  try { return 1; } finally { return 2; }
}
_f();'''
    elif kind == 2:
        # nested try with various throw types
        return '''try {
  try {
    try { throw null; }
    catch(e) { throw {msg: "inner", prev: e}; }
  }
  catch(e) { throw [e, "outer"]; }
}
catch(e) { e; }'''
    elif kind == 3:
        # exception during iteration
        return '''var _arr = [1, 2, 3, 4, 5];
var _r = [];
try {
  _arr.forEach(function(x) {
    if (x === 3) throw "stop";
    _r.push(x * 2);
  });
} catch(e) {}
_r;'''
    elif kind == 4:
        # error in constructor
        return '''function _Bad() { throw new Error("construct fail"); }
try { new _Bad(); } catch(e) { e.message; }
try { new _Bad(); } catch(e) { e.message; }'''
    else:
        # exception in property access chain
        return '''var _chain = {a: {b: {c: null}}};
try { _chain.a.b.c.d.e; } catch(e) {}
try { _chain.a.b.c(); } catch(e) {}
try { _chain.x.y; } catch(e) {}'''

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

def gen_mode_gc():
    """GC pressure: allocate many short-lived objects, strings, closures."""
    lines = []
    for _ in range(random.randint(4, 12)):
        lines.append(gen_gc_pressure())
    return '\n'.join(lines)

def gen_mode_typeconfuse():
    """Type confusion: rapidly change types on variables to stress inline caches."""
    lines = []
    for _ in range(random.randint(3, 8)):
        lines.append(gen_type_confusion())
    return '\n'.join(lines)

def gen_mode_scope():
    """Scope torture: shadowing, TDZ, hoisting, closures over mutated vars."""
    lines = []
    for _ in range(random.randint(3, 8)):
        lines.append(gen_scope_torture())
    return '\n'.join(lines)

def gen_mode_propstorm():
    """Property storm: add/delete/redefine properties, megamorphic shapes."""
    lines = []
    for _ in range(random.randint(3, 8)):
        lines.append(gen_property_storm())
    return '\n'.join(lines)

def gen_mode_reentrant():
    """Re-entrancy: toString/valueOf/toPrimitive during operations, mutating sort."""
    lines = []
    for _ in range(random.randint(4, 10)):
        lines.append(one_of(
            gen_reentrant,
            gen_exception_stress,
        ))
    return '\n'.join(lines)

def gen_mode_mixed():
    """Mix of everything."""
    generators = [gen_mode_expr, gen_mode_func, gen_mode_class,
                  gen_mode_control, gen_mode_destruct, gen_mode_edge,
                  gen_mode_advanced, gen_mode_gc, gen_mode_typeconfuse,
                  gen_mode_scope, gen_mode_propstorm, gen_mode_reentrant]
    parts = [random.choice(generators)() for _ in range(random.randint(2, 5))]
    return '\n\n'.join(parts)

MODE_MAP = {
    'expr': gen_mode_expr,
    'func': gen_mode_func,
    'class': gen_mode_class,
    'control': gen_mode_control,
    'destruct': gen_mode_destruct,
    'edge': gen_mode_edge,
    'advanced': gen_mode_advanced,
    'gc': gen_mode_gc,
    'typeconfuse': gen_mode_typeconfuse,
    'scope': gen_mode_scope,
    'propstorm': gen_mode_propstorm,
    'reentrant': gen_mode_reentrant,
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
