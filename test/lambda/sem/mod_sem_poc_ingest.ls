// Semantics POC — INGESTION (M4, grown in Stage 5/6): the production parser's
// AST dump, read back as Mark, lowered to the kernel's terms. Design:
// vibe/Lambda_Semantics_DSL_POC.md §6.3. The dump comes from
// `lambda.exe --emit-ast-dump file.ls`; a node is `<KIND attr… field…>`, an
// attribute `<label value>`, a field `<label child…>`.
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

// a literal's source text is Mark scalar syntax, so Mark reads its value; a
// spelling Mark has no reading for (a sized `10i8`, a hex `0x1F`) is outside
// the slice rather than silently null. A `math.pi`-style constant dumps as a
// bare primary too, and is the builtin of that name.
// a bare word that is not one of the keyword literals is a name the dump
// could not resolve (a global import's `pi`), which Mark would read as a symbol
fn is_word_char(c) => lower(c) != upper(c) or c == "_" or (c >= "0" and c <= "9")
fn is_bare_name(s) => len(s) > 0 and (lower(slice(s, 0, 1)) != upper(slice(s, 0, 1)) or slice(s, 0, 1) == "_")
  and all([for (i in 0 to len(s) - 1) is_word_char(slice(s, i, i + 1))])
  and not contains(["true", "false", "null", "inf", "nan"], s)
// `10i8`, `2.5f32`: a sized number is the conversion of that name applied to
// the unsuffixed literal, which is what the literal denotes (S4.1)
fn sized_suffix(s) {
  let hits = [for (x in ["i8", "i16", "i32", "i64", "u8", "u16", "u32", "u64", "f16", "f32"] where ends_with(s, x)) x]
  if (len(hits) > 0 and len(s) > len(hits[0]) and not is_bare_name(s)) hits[0] else null
}
// a constant range the dump kept as one primary (`1 to 5`)
fn range_parts(s) {
  let i = index_of(s, " to ")
  if (i == null) null else [trim(slice(s, 0, i)), trim(slice(s, i + 4, len(s)))]
}
fn literal_term(src) any^ {
  let s = trim(string(src))
  let sized = sized_suffix(s)
  let rp = range_parts(s)
  if (starts_with(s, "math.")) { let nm = "math_" ++ slice(s, 5, len(s));  <syscall n: nm> }
  else if (rp != null and int_spelling(rp[0]) and int_spelling(rp[1]) and len(rp[0]) > 0 and len(rp[1]) > 0) {
    let lo = literal_term(rp[0])^
    let hi = literal_term(rp[1])^;
    <rng lo hi>
  }
  else if (is_bare_name(s)) (<unsupported kind: "dynamic name", src: s>)
  else if (sized != null) {
    let num = literal_term(slice(s, 0, len(s) - len(sized)))^
    // Mark reads an integer past the i64 range as a decimal (S4.3.4), which
    // the conversion would not take back: a `u64` maximum is outside the slice
    if (name(num) == 'lit' and type(num.v) != decimal) (<syscall n: sized, num>)
    else if (name(num) == 'lit') (<unsupported kind: "literal", src: s>)
    else num
  }
  // Mark decodes neither `\u{…}` in a string nor `\u` in a symbol (F24)
  else if (contains(s, "\\u{") or (starts_with(s, "'") and contains(s, "\\u"))) (<unsupported kind: "literal", src: s>)
  else {
    let v = parse(s, 'mark') ^ { <unsupported kind: "literal", src: s> }
    if (type(v) == element and name(v) == 'unsupported') v
    // an integer spelling Mark can only read as a float (`0xFFFFFFFF`, a
    // number past i64) would change the literal's type
    else if (type(v) == float and int_spelling(s)) (<unsupported kind: "literal", src: s>)
    else (<lit v: v>)
  }
}
fn int_spelling(s: string) => not contains(s, ".") and not (contains(lower(s), "e") and not starts_with(lower(s), "0x"))
  and not contains(["inf", "nan", "-inf"], s)

// ===== lowering =====

fn binary_term(op, l, r) any^ => match string(op) {
  case "+": <add l r>
  case "-": <sub l r>
  case "*": <mul l r>
  case "/": <div l r>
  case "div": <idiv l r>
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
  case "to": <rng l r>
  case "in": <inn l r>
  case "is": <iss l r>
  case "|": <tyor l r>
  case "&": <tyand l r>
  case "<:": <sub_ty l r>
  case "at": <att l r>
  default: <unsupported kind: "binary", op: op>
}

fn unary_term(op, o) any^ => match string(op) {
  case "-": <neg o>
  case "+": <pos o>
  case "not": <nott o>
  default: <unsupported kind: "unary", op: op>
}

// a function value: n-ary throughout, named when the declaration has a name
fn func_term(n) any^ {
  let params = kids(n, 'param')
  let ps = [for (p in params) attr(p, 'name')]
  // a type binder (`value: as T`) is a contract the model does not read
  let binders = [for (p in params where len(kids(p, 'contract')) > 0) p]
  // an optional parameter (`a?`), a defaulted one (its `as` is the default)
  // and a variadic signature (`...`) change how a call binds; the kernel's
  // application binds exactly one argument per parameter, so these are
  // outside the modelled core rather than read as plain parameters (F17)
  let loose = [for (p in params where attr(p, 'optional') == true or len(kids(p, 'as')) > 0) p]
  let body = norm(first(n, 'body')^)^
  let nm = attr(n, 'name')
  if (len(binders) > 0) (<unsupported kind: "type binder">)
  else if (len(loose) > 0 or attr(n, 'variadic') == true) (<unsupported kind: "param form">)
  else if (nm == null) (<lamn ps: ps, body>) else (<funn f: nm, ps: ps, body>)
}

// a call: a system function goes through the kernel's declared builtin table
// by name; everything else applies an n-ary closure. A call not engaged by a
// handler propagates an error result (S7.5.1v2) — that is the
// postfix `^` the dump does not record, which a well-typed program must have
// written (E228) — unless the callee is a system function whose failures are
// plain values (S7.4.5; the dump's `may_return_error` marks the `T^E` ones).
// A typed receiving position (`let x: T | error = f()`) is not read.
fn call_term(n, engaged) any^ {
  let fnode = first(n, 'function')^
  let f = norm(fnode)^
  let args = for (a in kids(n, 'argument')) arg_term(a)^
  if (name(f) == 'sysf') {
    let named = [for (a in kids(n, 'argument') where name(a) == 'AST_NODE_NAMED_ARG') a]
    let c = <syscall n: f.n, args>
    if (len(named) > 0) (<unsupported kind: "named builtin argument">)
    // only the `T^E` system functions raise (S7.4.5, E228 on an unengaged
    // call); every other builtin's failure is a value the program keeps
    else if (engaged or not contains(["parse", "input", "fetch", "output"], string(f.n))) c else (<prop c>)
  }
  else { let c = <appn f args>;  if (engaged) c else (<prop c>) }
}

// a named argument `f(y: 1)` keeps its parameter name for the kernel to bind
fn arg_term(a) any^ =>
  if (name(a) == 'AST_NODE_NAMED_ARG') { let v = norm(first(a, 'as')^)^  let nm = attr(a, 'name');  <narg n: nm, v> }
  else norm(a)^

fn norm_engaged(n) any^ => match name(n) {
  case 'AST_NODE_CALL_EXPR': call_term(n, true)^
  case 'AST_NODE_PRIMARY': { let e = kids(n, 'expr');  if (len(e) > 0) norm_engaged(e[0])^ else norm(n)^ }
  default: norm(n)^
}

// a map entry; `*: m` spreads a map's entries
fn kv_term(kv) any^ {
  let k = attr(kv, 'name')
  if (name(kv) != 'AST_NODE_KEY_EXPR' or k == null) (<unsupported kind: name(kv)>)
  else if (string(k) == "*") { let m = norm(first(kv, 'as')^)^;  <spr m> }
  else { let v = norm(first(kv, 'as')^)^;  <kv k: k, v> }
}

// the type operand of `is` or a `type` declaration: a type literal's source
// text, a declared type's name, or — for a composite type node — the nested
// literal's source, which the kernel admits when it spells a base type, an
// optional or a union, and reports as stuck otherwise
fn type_source(n) {
  if (name(n) == 'AST_NODE_TYPE') attr(n, 'source')
  else {
    let found = [for (c in content(n) where type(c) == element) for (s in [type_source(c)] where s != null) s]
    if (len(found) > 0) found[0] else null
  }
}
fn type_term(n) any^ => match name(n) {
  case 'AST_NODE_IDENT': { let nm = attr(n, 'name');  <vr n: nm> }
  case 'AST_NODE_BINARY_TYPE': {
    let l = type_term(first(n, 'left')^)^
    let r = type_term(first(n, 'right')^)^
    let op = string(attr(n, 'op'))
    if (op == "|") (<tyor l r>) else if (op == "&") (<tyand l r>) else (<unsupported kind: "type operator", op: op>)
  }
  // `T that pred`: the constraint is a one-parameter function over `~`
  case 'AST_NODE_CONSTRAINED_TYPE': {
    let b = type_term(first(n, 'base')^)^
    let p = norm(first(n, 'constraint')^)^;
    <tycon b p>
  }
  case 'AST_NODE_PRIMARY': {
    let e = kids(n, 'expr')
    if (len(e) > 0) type_term(e[0])^ else (<unsupported kind: "type literal">)
  }
  default: {
    let s = type_source(n)
    let src = if (s == null) string(name(n)) else s;
    <tylit src: src>
  }
}

// a comprehension: one `forr` per clause, nested left to right (`for (x in xs,
// y in ys)` walks ys per x), the `where` on the innermost where every binder
// is in scope. A `let` clause scopes over the `where` and the body, so both
// are wrapped in its `lett`. `order by`, `limit` and `offset` ride on a single
// clause as attributes. Joins, `group by` and ordering over several clauses
// are outside the modelled core.
fn for_term(n) any^ {
  let loops = kids(n, 'loop')
  let joins = [for (c in loops where len(kids(c, 'on')) > 0 or len(kids(c, 'join_keys')) > 0) c]
  let orders = kids(n, 'order')
  let shaped = len(orders) > 0 or len(kids(n, 'limit')) > 0 or len(kids(n, 'offset')) > 0
  if (len(loops) == 0 or len(joins) > 0 or len(kids(n, 'group')) > 0 or (shaped and len(loops) > 1) or len(orders) > 1) (<unsupported kind: "for">)
  else {
    let lets = [for (d in kids(n, 'let')) d]
    let body = with_lets(lets, 0, norm(first(n, 'then')^)^)^
    let wh = kids(n, 'where')
    let cond = if (len(wh) > 0) [with_lets(lets, 0, norm(wh[0])^)^] else []
    let shape = for_shape(n, orders)^
    nest_loops(loops, 0, body, cond, shape)^
  }
}
fn with_lets(lets, i, inner) any^ {
  if (i >= len(lets)) inner
  else {
    let nm = attr(lets[i], 'name')
    if (nm == null) (<unsupported kind: "declarator">)
    else { let init = norm(first(lets[i], 'init')^)^  let rest = with_lets(lets, i + 1, inner)^;  <lett n: nm, init rest> }
  }
}
// the order key, direction, limit and offset of a single-clause loop
fn for_shape(n, orders) any^ {
  let ok = if (len(orders) > 0) norm(first(orders[0], 'expr')^)^ else null
  let desc = len(orders) > 0 and attr(orders[0], 'descending') == true
  let lim = if (len(kids(n, 'limit')) > 0) norm(kids(n, 'limit')[0])^ else null
  let off = if (len(kids(n, 'offset')) > 0) norm(kids(n, 'offset')[0])^ else null;
  {ok: ok, desc: desc, lim: lim, off: off, last: attr(n, 'limit_last') == true}
}
fn nest_loops(loops, i, body, cond, shape) any^ {
  if (i >= len(loops)) body
  else {
    let c = loops[i]
    let src = norm(first(c, 'as')^)^
    let inner = nest_loops(loops, i + 1, body, cond, shape)^
    let nm = attr(c, 'name')
    let ix = attr(c, 'index')
    // the axis and key class ride as attributes: `kf` keeps one kind of
    // member, `ko` binds the key itself (S8.1.1v2, S8.1.3v2); a decomposing
    // binder has no plain name and is outside the modelled core
    let kf = attr(c, 'key_filter')
    let ko = attr(c, 'key_only') == true
    let innermost = i == len(loops) - 1
    if (nm == null) (<unsupported kind: "for decompose">)
    else if (innermost and len(cond) > 0) { let w = cond[0];  <forr n: nm, i: ix, kf: kf, ko: ko, w: true, ok: shape.ok, desc: shape.desc, lim: shape.lim, off: shape.off, last: shape.last, src inner w> }
    else (<forr n: nm, i: ix, kf: kf, ko: ko, ok: shape.ok, desc: shape.desc, lim: shape.lim, off: shape.off, last: shape.last, src inner>)
  }
}

// the pipe family (S10.1.2v4, S10.1.6): a body with a free `~` is a mapping
// pipe; `|:` always is; a `~`-free body is whole-value application —
// `data |> f` is `f(data)` and `data |> take(3)` is `take(data, 3)`
fn mentions(n, kind) => if (type(n) != element) false
  else if (name(n) == kind) true
  else len([for (c in content(n) where type(c) == element and mentions(c, kind)) c]) > 0
fn has_free_cur(n) => if (type(n) != element) false
  else if (name(n) == 'AST_NODE_CURRENT_ITEM') true
  // a nested pipe's RHS owns its own `~` (S10.1.3); its source may still hold ours
  else if (name(n) == 'AST_NODE_PIPE') has_free_cur(field(n, 'left'))
  else len([for (c in content(n) where type(c) == element and has_free_cur(c)) c]) > 0
fn pipe_term(n) any^ {
  let src = norm(first(n, 'left')^)^
  let rhs = first(n, 'right')^
  let op = string(attr(n, 'op'))
  if (op == "|:") { let p = norm(rhs)^;  <filt src p> }
  else if (op == "that") { let p = norm(rhs)^;  <that src p> }
  else if (op != "|>") (<unsupported kind: "pipe", op: op>)
  else if (has_free_cur(rhs)) { let body = norm(rhs)^;  <pipe src body> }
  else whole_pipe(src, rhs)^
}
fn whole_pipe(src, rhs) any^ {
  if (name(rhs) == 'AST_NODE_CALL_EXPR') {
    let f = norm(first(rhs, 'function')^)^
    let args = for (a in kids(rhs, 'argument')) norm(a)^
    if (name(f) == 'sysf') (<prop <syscall n: f.n, src args>>) else (<prop <appn f src args>>)
  }
  else if (name(rhs) == 'AST_NODE_PRIMARY') whole_pipe(src, first(rhs, 'expr')^)^
  else {
    let f = norm(rhs)^
    if (name(f) == 'sysf') (<prop <syscall n: f.n, src>>) else (<prop <appn f src>>)
  }
}

// an element literal: tag, attributes (a map term) and content (a block term)
fn element_term(n) any^ {
  let tag = attr(n, 'name')
  let items = kids(n, 'item')
  let names = [for (kv in items where attr(kv, 'name') != null and string(attr(kv, 'name')) != "*") string(attr(kv, 'name'))]
  if (tag == null) (<unsupported kind: "element without a tag">)
  else if (len(unique(names)) != len(names)) (<unsupported kind: "duplicate map key">)
  else {
    let ents = for (kv in items) kv_term(kv)^
    let cs = kids(n, 'content')
    let body = if (len(cs) > 0) norm(cs[0])^ else (<cont>)
    let attrs = <mp ents>;
    <elm n: tag, attrs body>
  }
}

// `match` (S11.2.1): `<mtch scrutinee <arm pattern body>… [<dflt body>]>`; a
// pattern is a literal, a type, a declared type's name or an or-pattern of
// those; a constrained or binding pattern is outside the modelled core
fn match_term(n) any^ {
  let arms = for (a in kids(n, 'arms')) arm_term(a)^
  let scrut = norm(first(n, 'scrutinee')^)^;
  <mtch scrut arms>
}
fn arm_term(a) any^ {
  let pats = kids(a, 'pattern')
  let body = norm(first(a, 'body')^)^
  if (len(pats) == 0) (<dflt body>)
  else { let p = pattern_term(pats[0])^;  <arm p body> }
}
fn pattern_term(p) any^ => match name(p) {
  case 'AST_NODE_PRIMARY': { let e = kids(p, 'expr');  if (len(e) > 0) pattern_term(e[0])^ else literal_term(attr(p, 'source'))^ }
  case 'AST_NODE_IDENT': type_term(p)^
  case 'AST_NODE_TYPE': type_term(p)^
  case 'AST_NODE_CONSTRAINED_TYPE': type_term(p)^
  case 'AST_NODE_BINARY_TYPE': {
    let l = pattern_term(first(p, 'left')^)^
    let r = pattern_term(first(p, 'right')^)^
    if (string(attr(p, 'op')) == "|") (<tyor l r>) else (<unsupported kind: "pattern operator", op: attr(p, 'op')>)
  }
  default: if (ends_with(string(name(p)), "_TYPE")) type_term(p)^ else (<unsupported kind: "pattern", n: name(p)>)
}

// `let a, b = e` binds each name to the matching position of the value
fn decompose(d, items, i, tag) any^ {
  let names = [for (c in content(first(d, 'names')^) where type(c) == element) content(c)[0]]
  let init = norm(first(d, 'as')^)^
  let tmp = "_decompose_" ++ join([for (x in names) string(x)], "_")
  let rest = positions(names, 0, tmp, items, i, tag)^;
  <lett n: tmp, init rest>
}
fn positions(names, j, tmp, items, i, tag) any^ {
  if (j >= len(names)) block(items, i, tag)^
  else {
    let rest = positions(names, j + 1, tmp, items, i, tag)^
    let nm = names[j]
    let read = <idx <vr n: tmp> <lit v: j>>;
    <lett n: nm, read rest>
  }
}

// a content block or a list (S2.5.3, S2.5.1v2): a declaration scopes over the
// rest, so it becomes a `lett` wrapping the lowering of what follows — a named
// function binds its own `funn` value, a multi-declarator `let` nests one
// `lett` per declarator in order, a `type` declaration binds a type value —
// and a run of value items becomes the block's `cont` or the list's `lst`.
// When a declaration follows value items, the container's last child is its
// `lett`: that child's value is the list of the remaining items, and lists
// splice (S2.5.1v2), so the value stays the flat list of all the items, as
// the host's `for` builds it.
fn is_decl(it) => name(it) == 'AST_NODE_LET_STAM' or name(it) == 'AST_NODE_FUNC' or
  name(it) == 'AST_NODE_TYPE_STAM' or name(it) == 'AST_NODE_PUB_STAM'

// the index of the next declaration at or after i, or the item count
fn next_decl(items, i) => if (i >= len(items) or is_decl(items[i])) i else next_decl(items, i + 1)

// a run of adjacent function declarations is one recursive group (F18):
// each may name any other, declared before or after it
fn fn_run_end(items, i) => if (i < len(items) and name(items[i]) == 'AST_NODE_FUNC') fn_run_end(items, i + 1) else i
fn block(items, i, tag) any^ {
  if (i >= len(items)) (if (tag == 'lst') (<lst>) else (<cont>))
  else {
    let it = items[i]
    if (name(it) == 'AST_NODE_FUNC') {
      let j = fn_run_end(items, i)
      // an array to inspect (a one-item list would collapse to its item, which
      // `for` would then walk into), a list to splice into the term
      let fn_terms = [for (k in i to j - 1) func_term(items[k])^]
      let fns = for (f in fn_terms) f
      let rest = block(items, j, tag)^
      let plain = [for (f in fn_terms where name(f) != 'funn') f]
      if (len(plain) > 0) plain[0] else (<letrec fns rest>)
    }
    else if (is_decl(it)) declare(kids(it, 'declare'), 0, items, i + 1, tag)^
    else {
      let j = next_decl(items, i)
      let vals = for (k in i to j - 1) norm(items[k])^
      if (j < len(items)) {
        let rest = block(items, j, tag)^
        if (tag == 'lst') (<lst vals rest>) else (<cont vals rest>)
      }
      else (if (tag == 'lst') (<lst vals>) else (<cont vals>))
    }
  }
}

// a declared numeric contract widens the value it receives (S7.8.1: acceptance
// is read from the destination; `let x: float = 4` holds 4.0), which the
// conversion of that name spells; a contract of any other kind is not read
fn declared(d, init) any^ {
  let decl = kids(d, 'declared')
  let src = if (len(decl) > 0) type_source(decl[0]) else attr(d, 'declared_type')
  let nm = if (src == null) "" else trim(string(src))
  if (contains(["float", "f32", "f16", "i64", "u64", "decimal", "i8", "i16", "i32", "u8", "u16", "u32"], nm)) (<syscall n: nm, init>)
  else init
}

// one `lett` per declarator, then the rest of the block; a declarator with no
// plain name (a destructuring pattern) is outside this slice
fn declare(ds, d, items, i, tag) any^ {
  if (d >= len(ds)) block(items, i, tag)^
  else if (name(ds[d]) == 'AST_NODE_DECOMPOSE' and d == len(ds) - 1) decompose(ds[d], items, i, tag)^
  else {
    let nm = attr(ds[d], 'name')
    if (nm == null or len(kids(ds[d], 'init')) == 0) (<unsupported kind: "declarator">)
    else {
      let init = declared(ds[d], norm(first(ds[d], 'init')^)^)^
      let rest = declare(ds, d + 1, items, i, tag)^;
      <lett n: nm, init rest>
    }
  }
}

pub fn norm(n) any^ => match name(n) {
  case 'AST_SCRIPT': norm(first(n, 'child')^)^
  case 'AST_NODE_CONTENT': block(kids(n, 'item'), 0, 'cont')^
  // a list's `let` items are its `declare` field (S2.5.4v2), scoping over the items
  case 'AST_NODE_LIST': declare(kids(n, 'declare'), 0, kids(n, 'item'), 0, 'lst')^
  case 'AST_NODE_PRIMARY': {
    let e = kids(n, 'expr')
    if (len(e) > 0) norm(e[0])^ else literal_term(attr(n, 'source'))^
  }
  case 'AST_NODE_IDENT': {
    let nm = attr(n, 'name');
    <vr n: nm>
  }
  // the pipe's current item `~` and the handler's current error `^` are
  // ordinary variables the kernel binds; an implicit field name (`age` for
  // `~.age`, S10.1.7v2) reaches the dump already resolved, as a member read
  // on a current item whose source is the bare name
  case 'AST_NODE_CURRENT_ITEM': <vr n: "~">
  case 'AST_NODE_CURRENT_ERROR': <vr n: "^">
  case 'AST_NODE_SYS_FUNC': {
    let nm = attr(n, 'name');
    <sysf n: nm>
  }
  case 'AST_NODE_BINARY': {
    let op = string(attr(n, 'op'))
    let l = norm(first(n, 'left')^)^
    let r = if (op == "is") type_term(first(n, 'right')^)^ else norm(first(n, 'right')^)^
    binary_term(op, l, r)^
  }
  case 'AST_NODE_UNARY': {
    let operand = first(n, 'operand')^
    let src = if (name(operand) == 'AST_NODE_PRIMARY' and len(kids(operand, 'expr')) == 0) string(attr(operand, 'source')) else ""
    // a negated numeric literal is one literal: `-9223372036854775808i64` is
    // the i64 minimum, which no positive literal can spell (S4.3.2)
    let folded = if (string(attr(n, 'op')) == "-" and len(src) > 0 and src >= "0" and src <= "9z" and int_spelling(src)) literal_term("-" ++ src)^ else null
    if (folded != null and name(folded) != 'unsupported') folded
    else { let o = norm(operand)^;  unary_term(attr(n, 'op'), o)^ }
  }
  case 'AST_NODE_CALL_EXPR': call_term(n, false)^
  case 'AST_NODE_RAISE_EXPR': { let v = norm(first(n, 'value')^)^;  <rse v> }
  case 'AST_NODE_HANDLER_EXPR': {
    let o = norm_engaged(first(n, 'operand')^)^
    let b = norm(first(n, 'body')^)^
    let arm = for (v in kids(n, 'value')) norm(v)^;
    <hdl o b arm>
  }
  case 'AST_NODE_PIPE': pipe_term(n)^
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
  case 'AST_NODE_SPREAD': { let o = norm(first(n, 'operand')^)^;  <spr o> }
  case 'AST_NODE_INDEX_EXPR': {
    let o = norm(first(n, 'object')^)^
    let i = norm(first(n, 'field')^)^
    let read = <idx o i>
    // `last` inside a subscript is the subject's final position (S7.2.2)
    if (mentions(first(n, 'field')^, 'AST_NODE_LAST_INDEX')) {
      let one = <lit v: 1>
      let n_items = <syscall n: "len", o>
      let last_pos = <sub n_items one>;
      <lett n: "last", last_pos read>
    }
    else read
  }
  case 'AST_NODE_LAST_INDEX': <vr n: "last">
  case 'AST_NODE_CURRENT_INDEX': <vr n: "~#">
  case 'AST_NODE_MEMBER_EXPR': {
    let o = norm(first(n, 'object')^)^
    let k = attr(first(n, 'field')^, 'name');
    <fld k: k, o>
  }
  case 'AST_NODE_MAP': {
    let items = kids(n, 'item')
    // a key written twice in one literal has no ruling yet (F16): refused, not guessed
    let names = [for (kv in items where attr(kv, 'name') != null and string(attr(kv, 'name')) != "*") string(attr(kv, 'name'))]
    if (len(unique(names)) != len(names)) (<unsupported kind: "duplicate map key">)
    else { let ents = for (kv in items) kv_term(kv)^;  <mp ents> }
  }
  case 'AST_NODE_FOR_EXPR': for_term(n)^
  case 'AST_NODE_ELEMENT': element_term(n)^
  case 'AST_NODE_MATCH_EXPR': match_term(n)^
  case 'AST_NODE_TYPE': type_term(n)^
  default: if (ends_with(string(name(n)), "_TYPE")) type_term(n)^ else (<unsupported kind: name(n)>)
}

// the whole dump: `<ast-dump "lambda" <AST_SCRIPT …> <any_census …>>`
pub fn normalize(dump) any^ {
  let scripts = [for (c in content(dump) where type(c) == element and name(c) == 'AST_SCRIPT') c]
  if (len(scripts) != 1) raise error("dump: expected one AST_SCRIPT") else norm(scripts[0])^
}
