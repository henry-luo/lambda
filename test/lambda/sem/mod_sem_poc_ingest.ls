// Semantics POC — INGESTION (M4): the production parser's AST dump, read back
// as Mark, lowered to the kernel's terms. Design: vibe/Lambda_Semantics_DSL_POC.md
// §6.3. The dump comes from `lambda.exe --emit-ast-dump file.ls`; a node is
// `<KIND attr… field…>`, an attribute `<label value>`, a field `<label child…>`.
//
// `normalize` knows shapes only, never semantics: it maps node kinds to the
// §4.1 vocabulary and leaves anything outside the functional core as an
// `<unsupported>` term, which the kernel's default arm reports as stuck. The
// dump's inference annotations (`value_type`, contracts) are never read — the
// model has no inference, which is what makes a divergence an SI3v2 witness.
//
// Every child sequence is spliced through a list-valued binding (a bare `for`
// result): in content position `*xs` after a name reads as multiplication and
// `(…)` as a call (findings F4/F10), so no spread or parenthesis appears there.
// An element literal never follows `if (…)` bare either: `if (c) <cont>` reads
// `<` as less-than (`(c) < cont > …`, finding F13), so the branches below are
// parenthesized elements, blocks, or `match` arms.

// ===== reading the dump =====

// the child element carrying a field or attribute, or null
fn field(n, label) {
  let hits = [for (c in content(n) where type(c) == element and name(c) == label) c]
  if (len(hits) > 0) hits[0] else null
}

// a field's element children, in order
fn kids(n, label) {
  let f = field(n, label)
  if (f == null) [] else [for (c in content(f) where type(c) == element) c]
}

// an attribute's single scalar
fn attr(n, label) {
  let f = field(n, label)
  if (f == null) null else content(f)[0]
}

fn first(n, label) any^ {
  let ks = kids(n, label)
  if (len(ks) == 0) raise error("dump: missing field " ++ string(label)) else ks[0]
}

// a literal's source text is Mark scalar syntax, so Mark reads its value
fn literal(src) => parse(string(src), 'mark') ^ { null }

// ===== lowering =====

fn binary_term(op, l, r) any^ => match string(op) {
  case "+": <add l r>
  case "-": <sub l r>
  case "*": <mul l r>
  case "/": <div l r>
  case "%": <mod l r>
  case "++": <cat l r>
  case "**": <pow l r>
  case "<": <lt l r>
  case "<=": <le l r>
  case ">": <gt l r>
  case ">=": <ge l r>
  case "==": <eq l r>
  case "!=": <ne l r>
  case "and": <andd l r>
  case "or": <orr l r>
  default: <unsupported kind: "binary", op: op>
}

fn unary_term(op, o) any^ => match string(op) {
  case "-": <neg o>
  case "not": <nott o>
  default: <unsupported kind: "unary", op: op>
}

// a function value: n-ary throughout, named when the declaration has a name
fn func_term(n) any^ {
  let ps = [for (p in kids(n, 'param')) attr(p, 'name')]
  let body = norm(first(n, 'body')^)^
  let nm = attr(n, 'name')
  if (nm == null) (<lamn ps: ps, body>) else (<funn f: nm, ps: ps, body>)
}

// a call: a system function keeps its name, so the builtin boundary can be
// mapped by name; everything else applies an n-ary closure
fn call_term(n) any^ {
  let f = norm(first(n, 'function')^)^
  let args = for (a in kids(n, 'argument')) norm(a)^
  if (name(f) == 'sysf') {
    if (f.n == "len") (<lenn args>) else (<unsupported kind: "sysfunc", name: f.n>)
  }
  else (<appn f args>)
}

fn kv_term(kv) any^ {
  let v = norm(first(kv, 'as')^)^
  let k = attr(kv, 'name');
  <kv k: k, v>
}

// a one-clause comprehension with no index, join, let, where, group, order,
// limit or offset; anything richer is outside the modelled core
fn for_term(n) any^ {
  let loops = kids(n, 'loop')
  let plain = len(loops) == 1 and attr(loops[0], 'index') == null and
    len(kids(loops[0], 'on')) == 0 and len(kids(loops[0], 'join_keys')) == 0 and
    len(kids(n, 'let')) == 0 and len(kids(n, 'where')) == 0 and len(kids(n, 'group')) == 0 and
    len(kids(n, 'order')) == 0 and len(kids(n, 'limit')) == 0 and len(kids(n, 'offset')) == 0
  if (not plain) (<unsupported kind: "for">)
  else {
    let src = norm(first(loops[0], 'as')^)^
    let body = norm(first(n, 'then')^)^
    let nm = attr(loops[0], 'name');
    <forr n: nm, src body>
  }
}

// a content block: leading declarations become nested `lett` wrappers
// (a named function binds its own `funn` value); the value items that follow
// form a `cont`. A declaration after a value item is outside this slice.
fn block(items, i) any^ {
  if (i >= len(items)) (<cont>)
  else {
    let it = items[i]
    let kind = name(it)
    if (kind == 'AST_NODE_LET_STAM') {
      let ds = kids(it, 'declare')
      if (len(ds) != 1) raise error("unsupported: multi-declarator let")
      else {
        let init = norm(first(ds[0], 'init')^)^
        let rest = block(items, i + 1)^
        let nm = attr(ds[0], 'name');
        <lett n: nm, init rest>
      }
    }
    else if (kind == 'AST_NODE_FUNC') {
      let fnv = func_term(it)^
      let rest = block(items, i + 1)^
      let nm = attr(it, 'name');
      <lett n: nm, fnv rest>
    }
    else {
      let vals = for (j in i to len(items) - 1) value_item(items[j])^;
      <cont vals>
    }
  }
}

fn value_item(it) any^ {
  let kind = name(it)
  if (kind == 'AST_NODE_LET_STAM' or kind == 'AST_NODE_FUNC')
    raise error("unsupported: declaration after a value item")
  else norm(it)^
}

pub fn norm(n) any^ => match name(n) {
  case 'AST_SCRIPT': norm(first(n, 'child')^)^
  case 'AST_NODE_CONTENT': block(kids(n, 'item'), 0)^
  case 'AST_NODE_PRIMARY': {
    let e = kids(n, 'expr')
    if (len(e) > 0) norm(e[0])^
    else {
      let v = literal(attr(n, 'source'));
      <lit v: v>
    }
  }
  case 'AST_NODE_IDENT': {
    let nm = attr(n, 'name');
    <vr n: nm>
  }
  case 'AST_NODE_SYS_FUNC': {
    let nm = attr(n, 'name');
    <sysf n: nm>
  }
  case 'AST_NODE_BINARY': {
    let l = norm(first(n, 'left')^)^
    let r = norm(first(n, 'right')^)^
    binary_term(attr(n, 'op'), l, r)^
  }
  case 'AST_NODE_UNARY': {
    let o = norm(first(n, 'operand')^)^
    unary_term(attr(n, 'op'), o)^
  }
  case 'AST_NODE_CALL_EXPR': call_term(n)^
  case 'AST_NODE_IF_EXPR': {
    let c = norm(first(n, 'cond')^)^
    let t = norm(first(n, 'then')^)^
    let es = kids(n, 'else')
    let e = if (len(es) > 0) norm(es[0])^ else (<lit v: null>);
    <iff c t e>
  }
  case 'AST_NODE_FUNC': func_term(n)^
  case 'AST_NODE_FUNC_EXPR': func_term(n)^
  case 'AST_NODE_ARRAY': {
    let items = for (it in kids(n, 'item')) norm(it)^;
    <arr items>
  }
  case 'AST_NODE_INDEX_EXPR': {
    let o = norm(first(n, 'object')^)^
    let i = norm(first(n, 'field')^)^;
    <idx o i>
  }
  case 'AST_NODE_MEMBER_EXPR': {
    let o = norm(first(n, 'object')^)^
    let k = attr(first(n, 'field')^, 'name');
    <fld k: k, o>
  }
  case 'AST_NODE_MAP': {
    let ents = for (kv in kids(n, 'item')) kv_term(kv)^;
    <mp ents>
  }
  case 'AST_NODE_FOR_EXPR': for_term(n)^
  default: <unsupported kind: name(n)>
}

// the whole dump: `<ast-dump "lambda" <AST_SCRIPT …> <any_census …>>`
pub fn normalize(dump) any^ {
  let scripts = [for (c in content(dump) where type(c) == element and name(c) == 'AST_SCRIPT') c]
  if (len(scripts) != 1) raise error("dump: expected one AST_SCRIPT") else norm(scripts[0])^
}
