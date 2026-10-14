// Semantics POC — SHARED KERNEL: a big-step definitional interpreter for
// Lambda's functional core, written in plain Lambda with no new syntax or
// runtime. Design: vibe/Lambda_Semantics_DSL_POC.md (P1-P12).
//
// This file declares only `pub fn`s and produces no output, so it has no `.txt`
// golden; the `mod_` prefix tells the baseline's discovery it is an imported
// module, not a test. Its consumers are the fixtures beside it:
//   sem_poc_core.ls      positive differential checks (M0-M2)
//   sem_poc_stuck.ls     the negative suite: what the model calls an error (M3)
//   sem_poc_fixtures.ls  real test/lambda programs, hand-translated (M3)
//   sem_poc_ingest.ls    a real fixture, lowered from the parser's dump (M4)
// and the Stage 5 harness (`make semantics-verify`, mod_sem_verify.ls).
//
// Encoding (P3): terms are elements — tag = node kind, attributes = atomic
// metadata (`v` literal, `n` name, `p` parameter, `ps` parameter list, `k` key,
// `f` self-name, `src` type source), children = sub-terms in evaluation order.
// Environments, closures, modelled maps and held errors are elements too. Tags
// are deliberately not Lambda keywords (`lett`, `iff`, `vr`) so a term can
// never collide with surface syntax.
//
// Child access is always `t[i]` (children-only); `len(t)` is read only on
// attribute-free terms and `len(content(t))` elsewhere, since element `len`
// counts attributes as well as children (S8.3.1).

// ===== environment: element assoc-list, newest binding first =====
// values ride in the `v` attribute (attributes hold any value without the
// splicing hazards content position has); first hit wins, so shadowing is
// correct by construction rather than by a rule.
// An attribute stores a list as the array it is (S2.5.6), but a native binding
// passes a list through unchanged, so a bound list must still splice where it
// lands. `l` records list-ness and `lookup` rebuilds the list from the array.
// `[for …]`, not a bare `for`: a computed one-item list IS its item and an
// empty one is null (S2.5.5v2), so `len` of a bare comprehension over one hit
// would read the hit's own content rather than 1.
pub fn lookup(env, n) => {
    let hits = [for (b in env where b.n == n) b]
    if (len(hits) == 0) stuck("unbound variable " ++ string(n))^
    else if (hits[0].l) (for (x in hits[0].v) x)
    else hits[0].v
}

pub fn bind(env, n, v) => {
    let rest = for (b in env) b;
    <env <b n: n, v: v, l: type(v) == list> rest>
}

// An evaluated argument rides in the same wrapper as a binding: a call is
// positional, so a list is ONE argument, but a list item splices into an
// array literal (S2.5.1v2) and would scatter into several.
pub fn wrap(v) => <b v: v, l: type(v) == list>
pub fn unwrap(b) => if (b.l) (for (x in b.v) x) else b.v

// bind a parameter list to already-evaluated (wrapped) arguments, left to
// right. Takes no terms and never calls `ev`, so it needs no mutual recursion
// with the judgment.
pub fn bind_params(env, ps, args, i) =>
    if (i >= len(ps)) env
    else bind_params(bind(env, ps[i], unwrap(args[i])), ps, args, i + 1)

// application is only defined on closure values; an int or a bare term is stuck
// rather than silently reading null out of a non-element. The `and` here is the
// short-circuit that keeps `name(f)` from ever seeing a non-element.
pub fn closure_of(f, kinds) any^ =>
    if (type(f) == element and contains(kinds, name(f))) f
    else stuck("apply non-function")^

// ===== the kernel's two raise channels; host errors as values =====
// Stuckness — no rule, an unbound name, a bad arity, exhausted fuel — is a
// raise with one reserved code, so nothing else can be mistaken for it. The
// modelled language's own error propagation (`rse`, `prop`) rides the same
// `T^` channel under one fixed message and is caught at an application or a
// handler; everything else that reaches a catcher is re-raised.
// A host error is a VALUE the modelled program holds, tests and rescues
// (S7.4.1, S7.9.1–S7.9.4): the kernel keeps one inside `<errv v: e>`, so a
// `^` on `ev(…)` never mistakes it for stuckness. `host` wraps a host result,
// `raw` unwraps before a host operation, `truthy` is S3.1 (an error is falsy).
pub fn stuck(why) any^ => raise error({code: 7001, message: why})
fn propagate(v: any | error) any^ { let o: error = v;  raise error("sem-propagate", o) }
fn is_errv(v) => type(v) == element and name(v) == 'errv'
pub fn host(r: any | error) any^ => if (type(r) == error) (<errv v: r>) else r
pub fn raw(v) any | error => if (is_errv(v)) v.v else v
pub fn truthy(v) => if (is_errv(v)) false else (if (v) true else false)
// A value typed `any | error` cannot be narrowed statically, and the JIT
// mis-handles some conversions on that type (F24b), so a host operation gets
// its operands as plain `any`: `val` passes a non-error through a handled
// call, which strips `error` from the type without touching the value. An
// error operand never reaches an operation: the value family propagates it
// unchanged (S7.9.3), so `first_error` short-circuits the arm to that error.
fn pass(x: any | error) any | error => x
pub fn val(x) any => pass(raw(x)) ^ { null }
pub fn first_error(a, b) => if (is_errv(a)) a else if (is_errv(b)) b else null
fn error_field(e: error, k) any^ => match string(k) {
    case "message": e.message
    case "code": e.code
    // a source position (`line`, `col`) is nothing a term carries
    default: stuck("error field not modelled: " ++ string(k))^
}
// the error constructor (S7.4.4, S7.4.6): an error is itself, a modelled map
// supplies `code` and `message`, any other value is wrapped by the host
fn make_error(v) any | error {
    let e = raw(v)
    if (type(e) == error) e
    else if (is_mapv(v)) error({code: mapv_get(v, "code"), message: mapv_get(v, "message")})
    else error(e)
}

// ===== modelled maps and elements, and what `for … in` walks =====
// A modelled map is `<mapv <ent k v>…>`; a modelled element is
// `<elmv n: tag, <mapv …> <kids children…>>` — its attributes a modelled map,
// its children the content of a host element, so the host's own content
// normalization (S2.6.2–S2.6.4) applies to them. Neither can be a host value:
// no literal builds a map or an element from a computed key or tag (§12.2.5).
fn is_mapv(v) => type(v) == element and name(v) == 'mapv'
fn is_elmv(v) => type(v) == element and name(v) == 'elmv'
fn elmv_attrs(e) => e[0]
fn elmv_kids(e) => [for (c in e[1]) c]
fn is_closure(v) => type(v) == element and contains(['clo', 'rclo', 'clon', 'rclon', 'sysfv'], name(v))
fn is_sequence(v) => not is_errv(v) and not is_closure(v) and
    (type(v) == array or type(v) == list or type(v) == range or type(v) == map or type(v) == element)

// ===== the printing boundary (P12) =====
// The model's values print through the host's own printer, so its two
// representations are turned back into host values first: a held error
// becomes the error, and a modelled map or element is rebuilt by writing it
// as Mark text and reading it back — the one place the model leans on the
// formatter, and only to print. A container holding an error, a type or a
// closure has no Mark spelling that reads back as itself and is left as it is,
// which keeps its fixture pending rather than suspect.
fn key_text(k) => "\"" ++ replace(replace(string(k), "\\", "\\\\"), "\"", "\\\"") ++ "\""
fn unprintable(v) => is_errv(v) or type(v) == error or type(v) == type or is_closure(v)
  or is_mapv(v) or is_elmv(v)
  or ((type(v) == array or type(v) == map) and len([for (x in v where unprintable(x)) x]) > 0)
fn mapv_text(m) => join([for (e in content(m)) key_text(e.k) ++ ": " ++ format(printable(e.v), 'mark')], ", ")
fn elmv_text(e) {
    let attrs = [for (a in content(elmv_attrs(e))) key_text(a.k) ++ ": " ++ format(printable(a.v), 'mark')]
    let kids = [for (c in elmv_kids(e)) format(printable(c), 'mark')]
    let sep = if (len(attrs) > 0 and len(kids) > 0) ", " else "";
    "<" ++ string(e.n) ++ (if (len(attrs) > 0) " " ++ join(attrs, ", ") else "") ++ sep ++ (if (len(kids) > 0) " " ++ join(kids, " ") else "") ++ ">"
}
fn printable_parts(parts) => len([for (x in parts where unprintable(printable(x))) x]) == 0
pub fn printable(v) any | error {
    if (is_errv(v)) v.v
    else if (is_mapv(v)) (if (printable_parts([for (e in content(v)) e.v])) (parse("{" ++ mapv_text(v) ++ "}", 'mark') ^ { v }) else v)
    else if (is_elmv(v)) (if (printable_parts([for (x in [[for (a in content(elmv_attrs(v))) a.v], elmv_kids(v)]) for (y in x) y])) (parse(elmv_text(v), 'mark') ^ { v }) else v)
    else if (type(v) == array) [for (x in v) printable(x)]
    else if (type(v) == list) (for (x in v) printable(x))
    else v
}
// the members `for … in` walks (S8.1.1, S8.1.2v3), materialized so an index
// binder and a filter can address each one: a modelled map by value, anything
// else as the host walks it (a scalar walks nothing). The paired index is the
// position, or the key for a map (S8.1.3v2).
// an element walks its attribute values then its children (S8.1.2v3)
fn walk_items(src) => if (is_mapv(src)) [for (e in src) e.v]
    else if (is_elmv(src)) [for (x in [walk_items(elmv_attrs(src)), elmv_kids(src)]) for (y in x) y]
    else [for (x in raw(src)) x]
// `*v`: a sequence contributes its members, null nothing, and any other
// scalar — text included — itself (`[*5]` is `[5]`, `[*"ab"]` is `["ab"]`)
fn spread_items(v) => if (v is null) [] else if (is_sequence(v)) walk_items(v) else [v]
// a map's or an element's name keys are symbols (S8.1.3v2); a host container
// is asked through its own `at` (names) and `of` (positions) axes (S8.1.1v2)
fn host_names(src) { let h = raw(src);  [for (k at h) k] }
fn host_positions(src) { let h = raw(src);  [for (i of h) i] }
fn walk_keys(src, items) => if (is_mapv(src)) [for (e in src) symbol(e.k)]
    else if (is_elmv(src)) [for (x in [[for (e in elmv_attrs(src)) symbol(e.k)], [for (j in 0 to len(elmv_kids(src)) - 1) j]]) for (y in x) y]
    else if (type(raw(src)) == map or type(raw(src)) == element) [for (x in [host_names(src), host_positions(src)]) for (y in x) y]
    else [for (j in 0 to len(items) - 1) j]
// which members are name-keyed (a map's entries, an element's attributes)
// and which positional (an array's or a range's items, an element's children),
// so an `at`/`of` axis or a `k: symbol`/`k: int` key class can keep one kind
fn walk_kinds(src, items) => if (is_mapv(src)) [for (x in items) "name"]
    else if (is_elmv(src)) [for (x in [[for (a in content(elmv_attrs(src))) "name"], [for (c in elmv_kids(src)) "pos"]]) for (y in x) y]
    else if (type(raw(src)) == map or type(raw(src)) == element) [for (x in [[for (k in host_names(src)) "name"], [for (i in host_positions(src)) "pos"]]) for (y in x) y]
    else [for (x in items) "pos"]
// the one-name `at`/`of` form binds the key itself (S8.1.3v2)
fn for_env(env, t, x, k) => if (t.ko == true) bind(env, t.n, k)
    else if (t.i != null) bind(bind(env, t.n, x), t.i, k) else bind(env, t.n, x)
// What a child contributes in a SPLICE position — an array item, a list item,
// a content item, a `for` or pipe body — as an array of items, never a bare
// list: a list that crosses a function boundary collapses as a value
// (S2.5.5v2: empty to null, one item to the item), while the same list written
// in place splices (S2.5.1v2: `[for (x in []) x, 1]` is `[1]`). So a spread
// contributes its members, a `for`, list or block its items unreturned, and
// anything else its value in a one-item array, which a list value splices into.
fn items_of(c, env, fuel) any^ => match name(c) {
    case 'spr': spread_items(ev(c[0], env, fuel)^)
    // a declaration keeps the splice position of what it scopes over:
    // `[(let x = 5), 9]` is `[9]`, the list it wraps contributing nothing
    case 'lett': { let v = ev(c[0], env, fuel)^;  items_of(c[1], bind(env, c.n, v), fuel)^ }
    case 'forr': forr_items(c, env, fuel)^
    case 'lst': lst_items(c, env, fuel)^
    case 'cont': content_norm(lst_items(c, env, fuel)^)
    default: [ev(c, env, fuel)^]
}
fn lst_items(t, env, fuel) any^ => [for (i in 0 to len(t) - 1) for (x in items_of(t[i], env, fuel)^) x]
// `<forr n:'x', [i:'i',] [w: true,] src body [where]>`: each walked member in
// turn — and its position or key when an index binder is named (S8.1.3v2) —
// with a `where` keeping the members it is truthy for; the body is a splice
// position (S2.5.2v2: `for (x in xs) (for (y in []) y)` is empty)
fn forr_items(t, env, fuel) any^ {
    let src = ev(t[0], env, fuel)^
    let items = walk_items(src)
    let keys = walk_keys(src, items)
    let kinds = walk_kinds(src, items)
    let wanted = if (t.kf == null) null else if (string(t.kf) == "symbol") "name" else "pos"
    let kept = [for (j in 0 to len(items) - 1
                     where (wanted == null or kinds[j] == wanted) and
                           ((t.w != true) or truthy(ev(t[2], for_env(env, t, items[j], keys[j]), fuel)^))) j]
    // `order by` sorts the surviving rows by their key, stably (S6.2.1), then
    // `offset` drops and `limit` keeps a count of rows, in that order
    let ordered = if (t.ok == null) kept
        else { let ks = [for (j in kept) ev(t.ok, for_env(env, t, items[j], keys[j]), fuel)^];  [for (p in sorted_positions(ks, 0, [], t.desc == true)) kept[p]] }
    let off = if (t.off == null) 0 else val(ev(t.off, env, fuel)^)
    let lim = if (t.lim == null) len(ordered) else val(ev(t.lim, env, fuel)^)
    let after = [for (p in off to len(ordered) - 1) ordered[p]]
    // `limit last N` keeps the final N rows instead of the first
    let rows = if (t.last == true) [for (p in len(after) - lim to len(after) - 1 where p >= 0) after[p]]
               else [for (p in 0 to lim - 1 where p < len(after)) after[p]];
    [for (j in rows) for (x in items_of(t[1], for_env(env, t, items[j], keys[j]), fuel)^) x]
}

// ===== types =====
// a type literal's source text, admitted when it spells a base type, an
// optional `T?` or a union of those; the host's own type values do the rest
fn base_type(nm) any^ => match nm {
    case "int": int
    case "float": float
    case "number": number
    case "decimal": decimal
    case "string": string
    case "symbol": symbol
    case "bool": bool
    case "null": null
    case "array": array
    case "list": list
    case "range": range
    case "map": map
    case "element": element
    case "function": function
    case "fn": function
    case "error": error
    case "datetime": datetime
    case "binary": binary
    case "i8": i8
    case "i16": i16
    case "i32": i32
    case "i64": i64
    case "u8": u8
    case "u16": u16
    case "u32": u32
    case "u64": u64
    case "f16": f16
    case "f32": f32
    case "any": any
    default: stuck("type not modelled: " ++ nm)^
}
fn union_type(parts, i, acc) any^ =>
    if (i >= len(parts)) acc
    else { let p = type_value(parts[i])^;  union_type(parts, i + 1, if (i == 0) p else acc | p)^ }
// a type may continue across a line break (`int ⏎ ?`, S16.2.2v2), and the
// dump's span can run on into a following comment (F20), so the source is
// read line by line with comments dropped
fn code_line(line) { let i = index_of(line, "//");  if (i == null) line else slice(line, 0, i) }
fn type_value(src) any^ {
    let s = replace(trim(join([for (line in split(string(src), "\n")) trim(code_line(line))], " ")), " ?", "?")
    if (contains(s, "|")) union_type(split(s, "|"), 0, null)^
    else if (ends_with(s, "?")) { let b = type_value(slice(s, 0, len(s) - 1))^;  b | null }
    // `T[]` is the array-of-T type (S11.3.1v2, structural over the items); the
    // host cannot spell it from a value, so it is a modelled type value
    else if (ends_with(s, "[]")) { let e = type_value(slice(s, 0, len(s) - 2))^;  <tyarrv e: e> }
    else base_type(s)^
}

// ===== the builtin boundary (P8, DSL §12.3.6) =====
// Every host function the model trusts, by name and arity. Nothing else is
// callable: an undeclared name is stuck, never guessed. Each row is `host(…)`,
// so a failed call is a value, as it is natively (S7.4.5).
// a modelled map or element handed to a host builtin (an options map, a
// document to format) is rebuilt as a host value first (P12); one that
// cannot be stays itself and the builtin's own arm decides
fn arg(args, i) any => if (i < len(args)) val(host_arg(unwrap(args[i]))) else null
fn host_arg(v) => if (is_mapv(v) or is_elmv(v)) printable(v) else v
fn error_arg(args) {
    let errs = [for (x in args where is_errv(unwrap(x))) unwrap(x)]
    if (len(errs) > 0) errs[0] else null
}
// a stable insertion sort of positions by their keys, the host's `<` deciding
// (S6.2.1); shared by `order by` and the keyed `sort`
fn insert_pos(order, keys, j, desc) {
    let before = [for (i in order where (if (desc) keys[i] >= keys[j] else keys[i] <= keys[j])) i]
    let after = [for (i in order where not (if (desc) keys[i] >= keys[j] else keys[i] <= keys[j])) i];
    [for (x in [before, [j], after]) for (y in x) y]
}
fn sorted_positions(keys, j, order, desc) =>
    if (j >= len(keys)) order else sorted_positions(keys, j + 1, insert_pos(order, keys, j, desc), desc)
fn keyed_sort(a, f, desc, fuel) any^ {
    let items = walk_items(a)
    let keys = [for (x in items) apply_closure(f, [wrap(x)], fuel)^]
    let order = sorted_positions(keys, 0, [], desc);
    [for (i in order) items[i]]
}
pub fn builtin(nm, args, fuel) any^ {
    let k = len(args)
    let err = error_arg(args)
    let a = arg(args, 0)
    let b = arg(args, 1)
    let c = arg(args, 2)
    let d = arg(args, 3)
    // an error argument is the result of every value function (S7.9.3);
    // only the type family looks at it (S7.9.1: `type(err)` is `error`)
    if (err != null) (if (string(nm) == "type") error else err)
    else match string(nm) {
    // the modelled containers answer the element and map families themselves
    case "len": if (is_elmv(a)) len(elmv_attrs(a)) + len(elmv_kids(a)) else host(len(a))^
    case "type": if (is_closure(a)) function
                 else if (is_mapv(a)) map
                 else host(type(a))^
    case "name": if (is_elmv(a) or (is_closure(a) and name(a) == 'sysfv')) symbol(a.n) else host(name(a))^
    case "content": if (is_elmv(a)) elmv_kids(a) else host(content(a))^
    case "map": host(map(a))^
    // higher-order builtins over a modelled function run it through the kernel
    case "call": if (is_closure(a)) apply_closure(a, [for (x in b) wrap(x)], fuel)^ else host(call(a, b))^
    case "reduce": if (is_closure(b)) fold(a, b, fuel)^ else host(reduce(a, b))^
    case "string": { let pa = printable(a);  host(string(pa))^ }
    case "int": host(int(a))^
    case "float": host(float(a))^
    case "sum": if (k == 1) host(sum(a))^ else host(sum(a, b))^
    case "avg": if (k == 1) host(avg(a))^ else host(avg(a, b))^
    case "min": if (k == 1) host(min(a))^ else host(min(a, b))^
    case "max": if (k == 1) host(max(a))^ else host(max(a, b))^
    case "abs": host(abs(a))^
    case "round": host(round(a))^
    case "floor": host(floor(a))^
    case "ceil": host(ceil(a))^
    case "sign": host(sign(a))^
    case "trunc": host(trunc(a))^
    // a keyed sort by a modelled function: keys through the kernel, positions
    // ordered stably by the host's `<` (S6.2.1)
    case "sort": if (k == 1) host(sort(a))^
                 else if (is_closure(b)) keyed_sort(a, b, false, fuel)^
                 // an options map: `{by: fn, dir: 'desc'}`
                 else if (is_mapv(b)) {
                     let by = mapv_get(b, "by")
                     let desc = string(mapv_get(b, "dir")) == "desc"
                     if (is_closure(by)) keyed_sort(a, by, desc, fuel)^
                     else if (by == null) (if (desc) host(sort(a, 'desc'))^ else host(sort(a))^)
                     else stuck("builtin sort options")^
                 }
                 else host(sort(a, b))^
    case "unique": host(unique(a))^
    case "reverse": host(reverse(a))^
    case "slice": if (k == 2) host(slice(a, b))^ else host(slice(a, b, c))^
    case "take": host(take(a, b))^
    case "drop": host(drop(a, b))^
    case "contains": host(contains(a, b))^
    case "index_of": host(index_of(a, b))^
    case "last_index_of": host(last_index_of(a, b))^
    case "starts_with": host(starts_with(a, b))^
    case "ends_with": host(ends_with(a, b))^
    case "join": host(join(a, b))^
    case "split": if (k == 2) host(split(a, b))^ else host(split(a, b, c))^
    case "replace": if (k == 3) host(replace(a, b, c))^ else host(replace(a, b, c, d))^
    case "upper": host(upper(a))^
    case "lower": host(lower(a))^
    case "trim": host(trim(a))^
    case "trim_start": host(trim_start(a))^
    case "trim_end": host(trim_end(a))^
    case "all": host(all(a))^
    case "any": host(any(a))^
    case "count": host(count(a))^
    case "fill": host(fill(a, b))^
    case "zip": host(zip(a, b))^
    // the formatters see host values: a modelled container is rebuilt first (P12)
    case "format": {
        let pa = printable(a)
        if (unprintable(pa)) stuck("format of a value the model cannot print")^
        else if (k == 1) host(format(pa))^ else host(format(pa, b))^
    }
    case "error": if (k == 1) host(make_error(unwrap(args[0])))^ else stuck("builtin error with a source")^
    // sized numbers: a `10i8` literal lowers to the conversion of that name
    case "i8": host(i8(a))^
    case "i16": host(i16(a))^
    case "i32": host(i32(a))^
    case "i64": host(i64(a))^
    case "u8": host(u8(a))^
    case "u16": host(u16(a))^
    case "u32": host(u32(a))^
    case "u64": host(u64(a))^
    case "f16": host(f16(a))^
    case "f32": host(f32(a))^
    case "decimal": host(decimal(a))^
    // the two `T^E` builtins: their raise becomes the value the program engages
    case "parse": if (k == 1) host(parse(a) ^ { ^ })^ else host(parse(a, b) ^ { ^ })^
    case "input": if (k == 1) host(input(a) ^ { ^ })^ else host(input(a, b) ^ { ^ })^
    case "math_pi": math.pi
    case "math_e": math.e
    case "math_max_int": math.max_int
    // vector shape functions: the host reads a literal nested array as a matrix
    // and a computed one as nested arrays, which the model cannot tell apart (F22)
    case "shape": host(shape(a))^
    case "ndim": host(ndim(a))^
    case "reshape": host(reshape(a, b))^
    case "band": host(band(a, b))^
    case "bor": host(bor(a, b))^
    case "bxor": host(bxor(a, b))^
    case "bnot": host(bnot(a))^
    case "shl": host(shl(a, b))^
    case "shr": host(shr(a, b))^
    case "math_sqrt": host(math.sqrt(a))^
    case "math_cbrt": host(math.cbrt(a))^
    case "math_pow": host(math.pow(a, b))^
    case "math_hypot": host(math.hypot(a, b))^
    case "math_log": host(math.log(a))^
    case "math_log2": host(math.log2(a))^
    case "math_log10": host(math.log10(a))^
    case "math_exp": host(math.exp(a))^
    case "math_sin": host(math.sin(a))^
    case "math_cos": host(math.cos(a))^
    case "math_tan": host(math.tan(a))^
    case "math_asin": host(math.asin(a))^
    case "math_acos": host(math.acos(a))^
    case "math_atan": host(math.atan(a))^
    case "math_atan2": host(math.atan2(a, b))^
    case "math_mean": host(math.mean(a))^
    case "math_median": host(math.median(a))^
    case "math_variance": host(math.variance(a))^
    case "math_deviation": host(math.deviation(a))^
    case "math_prod": host(math.prod(a))^
    case "math_log1p": host(math.log1p(a))^
    case "math_sinh": host(math.sinh(a))^
    case "math_cosh": host(math.cosh(a))^
    case "math_tanh": host(math.tanh(a))^
    case "math_cumprod": host(math.cumprod(a))^
    case "matmul": host(matmul(a, b))^
    case "flatten": host(flatten(a))^
    case "except": host(except(a, b))^
    case "intersect": host(intersect(a, b))^
    case "math_cumsum": host(math.cumsum(a))^
    case "math_quantile": host(math.quantile(a, b))^
    case "ord": host(ord(a))^
    case "chr": host(chr(a))^
    case "find": if (k == 2) host(find(a, b))^ else host(find(a, b, c))^
    case "binary": host(binary(a))^
    case "ushr": host(ushr(a, b))^
    case "transpose": host(transpose(a))^
    case "subview": host(subview(a, b, c))^
    case "is_view": host(is_view(a))^
    default: stuck("builtin not declared: " ++ string(nm))^
    }
}

// ===== content normalization (S2.6.2–S2.6.4) =====
// A block's or a script's value is content. After splicing (S2.6.3, done by
// the host's `for` when it built `items`), null and "" items are dropped
// (S2.6.2) and each run of adjacent strings becomes one string (S2.6.4).
// Binaries merge the same way; the modelled core has no binary literal.
fn string_run_end(xs, i) =>
    if (i + 1 < len(xs) and type(xs[i + 1]) == string) string_run_end(xs, i + 1) else i
// `is null`, not `== null`: the null TYPE (`type(null)`) compares equal to
// null under `==` yet is an item content keeps and prints as `type.null`
fn content_norm(items) {
    let xs = [for (x in items where not (x is null) and x != "") x];
    [for (i in 0 to len(xs) - 1
          where not (i > 0 and type(xs[i]) == string and type(xs[i - 1]) == string))
        (if (type(xs[i]) == string) join([for (k in i to string_run_end(xs, i)) xs[k]], "") else xs[i])]
}

// ===== the judgment =====
// `ev` is what the DSL design's §12.3.4 says a moded judgment compiles to: a
// recursive fn returning `T ^ StuckErr`. The `match` is the rule set, one arm
// per node kind; `^` on each recursive call is stuckness propagation. Nothing
// unmodelled can silently produce a value — the default arm raises.
// Evaluation order is inherited from the meta-language: `ev(t[0]) + ev(t[1])`
// IS left-to-right strictness, which is why no `strict` annotation is needed.
//
// `fuel` bounds recursion DEPTH and makes the judgment TOTAL: without it a term
// like omega (below) diverges, and because a script's top level prints only
// after the whole file evaluates (S16.7), one diverging term erased every other
// result and reported a bare stack overflow instead (finding F5). Exhausting
// fuel is ordinary stuckness, so divergence now localizes to its own check.
// There is no tail-call escape: every arm decrements, tail positions included.
// The guard is an `if … else`, never a statement before the `match`: a block is
// the list of ALL its statements' results (S2.5.3) and an else-less `if` yields
// null in value position (S16.6.3), so a guard statement would make every
// result `(null, v)`.
pub fn ev(t, env, fuel) any^ =>
    if (fuel <= 0) stuck("out of fuel")^
    else match name(t) {
    case 'lit': t.v
    case 'vr':  lookup(env, t.n)^
    // `*x` in a value position is the list of x's members (S12.3.5v2)
    case 'spr': for (x in spread_items(ev(t[0], env, fuel - 1)^)) x
    // a system function named as a value: applicable through `appn` or `call`
    case 'sysf': <sysfv n: t.n>
    // arithmetic — model scalars ARE host scalars (P5), so `/` is Lambda's
    // true division (7/2 = 3.5, x/0 = inf) rather than a separate numeric tower;
    // operands reach the host raw, so an error operand propagates as the value
    // family says it does (S7.9.3), and the result is held as a value
    case 'add': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "+")^
    case 'sub': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "-")^
    case 'mul': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "*")^
    case 'div': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "/")^
    // `div` stays in its operand domain (S4.5.3, SI7); the host's `div` is that rule
    case 'idiv': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "div")^
    case 'mod': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "%")^
    case 'cat': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "++")^
    case 'pow': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "**")^
    case 'neg': { let x = ev(t[0], env, fuel - 1)^;  if (is_errv(x)) x else { let a = val(x);  host(- a)^ } }
    case 'pos': { let x = ev(t[0], env, fuel - 1)^;  if (is_errv(x)) x else { let a = val(x);  host(+ a)^ } }
    // comparison
    case 'lt':  binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "<")^
    case 'le':  binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "<=")^
    case 'gt':  binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, ">")^
    case 'ge':  binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, ">=")^
    // equality is structural and key-order-free on maps (S5.3.1, SI4), which a
    // modelled map's entry order would hide, so `sem_eq` compares those by key
    case 'eq':  { let a = ev(t[0], env, fuel - 1)^  let b = ev(t[1], env, fuel - 1)^;  sem_eq(a, b)^ }
    case 'ne':  { let a = ev(t[0], env, fuel - 1)^  let b = ev(t[1], env, fuel - 1)^  let r = sem_eq(a, b)^;  if (is_errv(r)) r else not r }
    // ranges and membership: the host's sequence family (S5.3.1, S8.1.1);
    // `in` on a modelled map tests what `for … in` walks — its values
    case 'rng': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "to")^
    // the subtype test on type values (S9.2.1) and membership on the name axis
    // (S8.1.1v2: `k at m` tests what `for … at` walks)
    case 'sub_ty': binop(ev(t[0], env, fuel - 1)^, ev(t[1], env, fuel - 1)^, "<:")^
    case 'att': {
        let x = ev(t[0], env, fuel - 1)^
        let b = ev(t[1], env, fuel - 1)^
        if (is_mapv(b) or is_elmv(b)) contains([for (kk in walk_keys(b, walk_items(b))) string(kk)], string(raw(x)))
        else binop(x, b, "at")^
    }
    case 'inn': {
        let x = ev(t[0], env, fuel - 1)^
        let b = ev(t[1], env, fuel - 1)^
        let e = first_error(x, b)
        if (e != null) e
        else { let a = val(x)  let members = if (is_mapv(b) or is_elmv(b)) walk_items(b) else val(b);  host(a in members)^ }
    }
    // `is` against a host type value (S11.3.1v2): the right child is a `tylit`
    // or a variable bound to one, so the host's own test decides
    // an error IS tested, not propagated (S7.9.1: `err is error` is true); a
    // modelled container answers for its own kind (`type_test`)
    case 'iss': { let v = ev(t[0], env, fuel - 1)^  let ty = ev(t[1], env, fuel - 1)^;  type_test(v, ty, fuel - 1)^ }
    case 'tylit': type_value(t.src)^
    // `T that pred`: a base type with a predicate over `~` (S11.2.1, S11.3)
    case 'tycon': { let b = ev(t[0], env, fuel - 1)^  let p = ev(t[1], env, fuel - 1)^;  <tyconv b: b, p: p> }
    // `A | B` and `A & B` are type operators everywhere (S10.1.1v3)
    case 'tyor':  { let a = val(ev(t[0], env, fuel - 1)^)  let b = val(ev(t[1], env, fuel - 1)^);  host(a | b)^ }
    case 'tyand': { let a = val(ev(t[0], env, fuel - 1)^)  let b = val(ev(t[1], env, fuel - 1)^);  host(a & b)^ }
    // logical — the result is an OPERAND, not a boolean: `or` is the coalescing
    // operator (S3.3v2), so `a or b` is `a` when truthy (S3.1) and `b` otherwise;
    // `and` mirrors it, `a and b` being `b` when `a` is truthy and `a` otherwise
    // (the runtime's reading; no S# rules `and`'s value yet). Short-circuit is
    // expressed by NOT recursing into the second child, so an ill-formed or
    // stuck right operand is never reached; the checks prove that by putting an
    // unbound variable there. Found by the Stage 5 harness on comp_expr.ls.
    case 'andd': { let l = ev(t[0], env, fuel - 1)^; if (truthy(l)) ev(t[1], env, fuel - 1)^ else l }
    case 'orr':  { let l = ev(t[0], env, fuel - 1)^; if (truthy(l)) l else ev(t[1], env, fuel - 1)^ }
    // `not err` is true: the truthy family participates (S7.9.2)
    case 'nott': { let x = ev(t[0], env, fuel - 1)^;  not truthy(x) }
    case 'iff': if (truthy(ev(t[0], env, fuel - 1)^)) ev(t[1], env, fuel - 1)^
                else ev(t[2], env, fuel - 1)^
    // a closure captures its defining environment as data — this is what makes
    // the model environment-based (P2) and so needs no capture-avoiding subst
    case 'lam': {
        let body = t[0];
        <clo p: t.p, body env>
    }
    // named function: same shape plus a self-name, consumed by `app` below
    case 'fun': {
        let body = t[0];
        <rclo f: t.f, p: t.p, body env>
    }
    // n-ary abstraction: parameters ride in the `ps` attribute as an array
    case 'lamn': {
        let body = t[0];
        <clon ps: t.ps, body env>
    }
    // n-ary named function: `fun` with a parameter list, consumed by `appn`
    case 'funn': {
        let body = t[0];
        <rclon f: t.f, ps: t.ps, body env>
    }
    case 'app': {
        let f = closure_of(ev(t[0], env, fuel - 1)^, ['clo', 'rclo'])^
        let a = ev(t[1], env, fuel - 1)^
        let body = f[0]
        let cenv = f[1]
        let env1 = bind(cenv, f.p, a)
        // recursion without mutation or a cyclic environment: a named closure
        // re-binds ITSELF before its body runs, so the body's own name resolves.
        // A plain `clo` carries no `f` attribute, so `f.f` reads null (falsy)
        // and both closure kinds share this one arm.
        let env2 = if (f.f) bind(env1, f.f, f) else env1
        apply_body(body, env2, fuel - 1)^
    }
    case 'appn': {
        let f = closure_of(ev(t[0], env, fuel - 1)^, ['clon', 'rclon', 'sysfv'])^
        // `appn` carries no attributes, so len(t) is its child count: the
        // callee followed by the arguments; a `<narg n: p, v>` child is a
        // named argument, bound to the parameter of that name
        let args = [for (i in 1 to len(t) - 1) (if (name(t[i]) == 'narg') narg_wrap(t[i], env, fuel - 1)^ else wrap(ev(t[i], env, fuel - 1)^))]
        apply_closure(f, args, fuel - 1)^
    }
    case 'lett': ev(t[1], bind(env, t.n, ev(t[0], env, fuel - 1)^), fuel - 1)^
    // a group of mutually visible function declarations (F18): each closure
    // carries the group, and an application re-binds every member, so a
    // function may name a sibling declared after it
    case 'letrec': {
        let group = [for (i in 0 to len(t) - 2) t[i]]
        let rest = t[len(t) - 1]
        ev(rest, bind_group(env, group, env, 0), fuel - 1)^
    }

    // ===== errors (S7.4–S7.6) =====
    // `raise v`: the modelled program's own propagation, up to the nearest
    // application or handler (S7.4.6: a non-error operand is wrapped)
    case 'rse': propagate(make_error(ev(t[0], env, fuel - 1)^))^
    // postfix `^`: a RAISED error propagates on (S7.6.3v2); a soft error value
    // and anything else pass
    case 'prop': { let v = ev(t[0], env, fuel - 1)^;  if (is_errv(v) and v.r == true) propagate(v.v)^ else v }
    // `e ^ { body }`: channel-agnostic (S7.6.1v4) — a held error value and a
    // propagated error alike reach the body with `^` bound to the error; a
    // non-error operand passes through unchanged
    case 'hdl': {
        let v = ev(t[0], env, fuel - 1) ^ { if (^.message == "sem-propagate") (<errv v: ^.source>) else raise ^ }
        // `^` in the body is the error as a plain value, raised or not
        if (is_errv(v)) { let e = <errv v: v.v>;  ev(t[1], bind(env, "^", e), fuel - 1)^ }
        // the two-arm form `e ^ { … } ~ { … }` runs its second arm on a
        // non-error value with `~` bound to it (S7.6.1v4)
        else if (len(t) > 2) ev(t[2], bind(env, "~", v), fuel - 1)^
        else v
    }

    // ===== M2: collections =====
    // array literal — attribute-free, so len(t) is the element count. The
    // result is a REAL host array, so checks compare it with `==` directly.
    // `[for …]` is the host's own array construction: it never collapses and
    // is `[]` when empty (S2.5.5v2), an array item stays one item, and a list
    // item splices exactly as it does in a native array literal (S2.5.1v2).
    case 'arr': lst_items(t, env, fuel - 1)^
    // list literal — the host's own list construction (S2.5.1v2): items keep
    // their nulls (lists never normalize, S2.6.1v2), list items splice, and
    // the value collapses as every list does (S2.5.5v2)
    case 'lst': for (x in lst_items(t, env, fuel - 1)^) x
    // indexing inherits the host's total read: out of bounds and negative
    // indices are null rather than errors (S7.1.1v2, C15), not a rule of ours
    case 'idx': {
        let a = ev(t[0], env, fuel - 1)^
        let i = raw(ev(t[1], env, fuel - 1)^)
        if (is_mapv(a)) mapv_get(a, i)
        else if (is_elmv(a)) { let ks = elmv_kids(a);  host(ks[i])^ }
        else { let h = raw(a);  host(h[i])^ }
    }
    case 'slice': {
        let a = raw(ev(t[0], env, fuel - 1)^)
        let i = raw(ev(t[1], env, fuel - 1)^)
        let j = raw(ev(t[2], env, fuel - 1)^);
        host(a[i to j])^
    }
    // comprehension: `<forr n:'x', [i:'i',] [w: true,] src body [where]>` binds
    // each walked member in turn — and its position or key when an index
    // binder is named (S8.1.3v2); a `where` keeps the members it is truthy for.
    // Clauses nest in the lowering, the filter on the innermost. The result is
    // the host's for-expression LIST, not an array: a native `for` produces a
    // list (S2.5.2v2) that collapses to its item or to null as a value
    // (S2.5.5v2) and splices where it lands (S2.5.1v2), and the model must
    // answer the same.
    case 'forr': for (x in forr_items(t, env, fuel - 1)^) x
    // the mapping pipe (S10.1.2v4): the body runs once per walked member with
    // `~` bound to it and `~#` to its key or index (S10.1.3); the result is an
    // ARRAY for every sequence source (S2.5.7v4), the value itself for a
    // scalar. Text pipes by code point and keeps its kind (S2.5.8) — not modelled.
    case 'pipe': {
        let src = ev(t[0], env, fuel - 1)^
        if (type(src) == string) {
            // text pipes by code point and keeps its kind when every result
            // item is text (S2.5.8, S10.1.2v4)
            let chars = [for (c in src) c]
            let out = [for (j in 0 to len(chars) - 1)
                       for (x in items_of(t[1], bind(bind(env, "~", chars[j]), "~#", j), fuel - 1)^) x]
            if (all([for (x in out) type(x) == string])) join(out, "") else out
        }
        else if (type(src) == symbol) stuck("symbol pipe not modelled")^
        else if (not is_sequence(src)) ev(t[1], bind(env, "~", src), fuel - 1)^
        else {
            let items = walk_items(src)
            let keys = walk_keys(src, items);
            [for (j in 0 to len(items) - 1)
                for (x in items_of(t[1], bind(bind(env, "~", items[j]), "~#", keys[j]), fuel - 1)^) x]
        }
    }
    // the proviso `c that p` (S10.1.7v2): the value itself when `p` holds of
    // it, null otherwise
    case 'that': {
        let src = ev(t[0], env, fuel - 1)^
        if (truthy(ev(t[1], bind(env, "~", src), fuel - 1)^)) src else null
    }
    // the filter stage `|:` (S10.1.6): the pipe's walk and kind, keeping the
    // members its body is truthy for; a scalar gives itself or null
    case 'filt': {
        let src = ev(t[0], env, fuel - 1)^
        if (type(src) == string) {
            let chars = [for (c in src) c];
            join([for (j in 0 to len(chars) - 1
                       where truthy(ev(t[1], bind(bind(env, "~", chars[j]), "~#", j), fuel - 1)^)) chars[j]], "")
        }
        else if (type(src) == symbol) stuck("symbol pipe not modelled")^
        else if (not is_sequence(src)) (if (truthy(ev(t[1], bind(env, "~", src), fuel - 1)^)) src else null)
        else {
            let items = walk_items(src)
            let keys = walk_keys(src, items);
            [for (j in 0 to len(items) - 1
                  where truthy(ev(t[1], bind(bind(env, "~", items[j]), "~#", keys[j]), fuel - 1)^))
                items[j]]
        }
    }
    // an element literal: its attributes a modelled map, its children the
    // lowered content, which the host element `<kids …>` normalizes as content
    case 'elm': {
        let attrs = ev(t[0], env, fuel - 1)^
        let kids = for (x in items_of(t[1], env, fuel - 1)^) x
        let kids_el = <kids kids>;
        <elmv n: t.n, attrs kids_el>
    }
    // `match` is a type match (S11.2.1): arms in order, first match wins, a
    // literal arm is a singleton dispatched by `==`, a type arm by `is`, `~`
    // is the matched value in every arm body; no arm and no default is null
    case 'mtch': {
        let v = ev(t[0], env, fuel - 1)^
        match_arms(t, 1, v, env, fuel - 1)^
    }
    // a modelled map is an assoc-list element, NOT a host map: map literals
    // reject a computed key, so the model cannot build `{k: v}` from an
    // evaluated key at all. Field reads are therefore what the checks compare;
    // the harness turns one back into a host map only to print it (P12).
    // A spread entry contributes the entries of the map it names.
    case 'mp': {
        // an array: a lone entry must stay one item, and a spread's list splices
        let ents = [for (i in 0 to len(t) - 1) {
            let kv = t[i]
            if (name(kv) == 'spr') {
                let m = ev(kv[0], env, fuel - 1)^
                if (is_mapv(m)) (for (e in m) e)
                else if (is_elmv(m)) (for (e in elmv_attrs(m)) e)
                else if (type(raw(m)) == map) { let h = raw(m);  for (k, v in h) <ent k: string(k), v: v> }
                else stuck("map spread of a host value")^
            }
            else { let v = ev(kv[0], env, fuel - 1)^;  <ent k: kv.k, v: v> }
        }]
        let unique_ents = for (e in dedupe_ents(ents, 0, [])) e;
        <mapv unique_ents>
    }
    // a field read: on a modelled map the LATER writer wins, which is how an
    // explicit key overrides a spread's (map_spread_override.ls; duplicate
    // literal keys are refused by the lowering until ruled, F16); on a held
    // error the error's own fields (S7.4.4); on any other host value the
    // host's member read (S7.1.1v3: an absent member reads null)
    case 'fld': {
        let m = ev(t[0], env, fuel - 1)^
        if (is_mapv(m)) mapv_get(m, t.k)
        else if (is_elmv(m)) mapv_get(elmv_attrs(m), t.k)
        else if (is_errv(m)) error_field(m.v, t.k)^
        else { let h = raw(m)  let k = string(t.k);  host(h[k])^ }
    }
    // the trusted-primitive boundary (DSL design §12.3.6): `len` is not derived from
    // rules, it is delegated to the host and declared as such here
    case 'lenn': { let a = raw(ev(t[0], env, fuel - 1)^);  host(len(a))^ }
    // a system function by name, through the declared builtin table above
    case 'syscall': {
        let args = [for (i in 0 to len(content(t)) - 1) wrap(ev(t[i], env, fuel - 1)^)]
        builtin(t.n, args, fuel - 1)^
    }

    // ===== M4: a content block's value items =====
    // Declarations never reach here: `normalize` folds them into `lett`
    // wrappers (S2.5.4v2). What remains is the list the block builds
    // (S2.5.3), normalized as content (S2.6.1v2, S16.7.1): the host's `for`
    // splices list-valued items (S2.6.3), and `content_norm` drops absent
    // items and merges adjacent strings. As a value it collapses exactly as a
    // script's or block's content does (S2.5.5v2): none is null, one is the
    // item, more is the list.
    case 'cont': for (x in content_norm(lst_items(t, env, fuel - 1)^)) x

    default: stuck("no rule for term " ++ string(name(t)))^
    }

// a callee's body: the modelled program's propagation stops here and becomes
// the application's RAISED error value (`r: true`) — the caller must engage it
// (S7.5.1): postfix `^` propagates it on, a handler or an `or` takes it. An
// error the body merely returned stays a soft value (S7.4.1) and flows past a
// `^`. Stuckness and anything else pass through.
fn apply_body(body, env, fuel) any^ =>
    ev(body, env, fuel) ^ { if (^.message == "sem-propagate") (<errv v: ^.source, r: true>) else raise ^ }

// a named argument: the wrapper also records the parameter name
fn narg_wrap(t, env, fuel) any^ { let v = ev(t[0], env, fuel)^;  <b v: v, l: type(v) == list, p: t.n> }
// bind the group's members as closures over `cenv`, each carrying the group
fn bind_group(env, group, cenv, i) =>
    if (i >= len(group)) env
    else {
        let g = group[i]
        let body = g[0]
        let clo = <rclon f: g.f, ps: g.ps, grp: group, body cenv>;
        bind_group(bind(env, g.f, clo), group, cenv, i + 1)
    }
// apply an n-ary closure to wrapped arguments: positional ones in order, named
// ones by parameter; arity is part of the semantics, not a host detail — a
// mismatch or an unknown name has no rule, so it is stuck rather than binding
// null or dropping arguments
pub fn apply_closure(f, args, fuel) any^ {
    if (name(f) == 'sysfv') builtin(f.n, args, fuel)^
    else apply_modelled(f, args, fuel)^
}
fn apply_modelled(f, args, fuel) any^ {
    let ps = f.ps
    let body = f[0]
    let cenv = f[1]
    let positional = [for (a in args where a.p == null) a]
    let named = [for (a in args where a.p != null) a]
    let known = [for (a in named where contains(ps, a.p)) a]
    if (len(ps) != len(args) or len(known) != len(named)) stuck("arity mismatch")^
    else {
        let env1 = bind_params(cenv, ps, positional, 0)
        let env2 = bind_named(env1, named, 0)
        // a named closure re-binds itself as `app` does; `clon` has no `f`;
        // a group member re-binds its whole group
        let env3 = if (f.grp != null) bind_group(env2, f.grp, cenv, 0) else if (f.f) bind(env2, f.f, f) else env2
        apply_body(body, env3, fuel)^
    }
}
fn bind_named(env, named, i) =>
    if (i >= len(named)) env else bind_named(bind(env, named[i].p, unwrap(named[i])), named, i + 1)
// a left fold with a modelled function: `reduce([])` is null, one item is itself
fn fold(xs, f, fuel) any^ {
    let items = walk_items(xs)
    if (len(items) == 0) null else fold_from(items, f, items[0], 1, fuel)^
}
fn fold_from(items, f, acc, i, fuel) any^ =>
    if (i >= len(items)) acc
    else fold_from(items, f, apply_closure(f, [wrap(acc), wrap(items[i])], fuel)^, i + 1, fuel)^

// the type test behind `is` and `match`: a constrained type adds its
// predicate; a modelled map or element is a `map` / an `element` (and `any`)
// and nothing structural the model can see into
fn type_test(v, ty, fuel) any^ {
    if (type(ty) == element and name(ty) == 'tyarrv') {
        // the sequence family answers `array` as one (S5.3.1): a list and a
        // range are arrays to `is`, so `T[]` tests their items too
        let x = raw(v)
        if (not (type(x) == array or type(x) == list or type(x) == range)) false
        else all([for (item in x) truthy(type_test(item, ty.e, fuel)^)])
    }
    else if (type(ty) == element and name(ty) == 'tyconv') {
        let base = type_test(v, ty.b, fuel)^
        if (truthy(base)) truthy(apply_closure(ty.p, [wrap(v)], fuel)^) else false
    }
    else if (is_mapv(v) or is_elmv(v) or is_closure(v)) {
        let kind = if (is_mapv(v)) map else if (is_elmv(v)) element else function
        let t2 = val(ty)
        let base = [int, float, number, decimal, string, symbol, bool, array, list, range, map, element, function, error, datetime, any]
        if (type(t2) != type) false
        else if (t2 == kind or t2 == any) true
        else if (contains(base, t2)) (kind is t2)
        else stuck("structural `is` on a modelled container")^
    }
    else { let x = raw(v)  let t2 = val(ty);  host(x is t2)^ }
}
fn match_arms(t, i, v, env, fuel) any^ {
    if (i >= len(t)) null
    else {
        let arm = t[i]
        if (name(arm) == 'dflt') ev(arm[0], bind(env, "~", v), fuel)^
        else {
            let p = ev(arm[0], env, fuel)^
            let is_type = type(val(p)) == type or (type(p) == element and (name(p) == 'tyconv' or name(p) == 'tyarrv'))
            let hit = if (is_type) type_test(v, p, fuel)^ else sem_eq(v, p)^
            if (truthy(hit)) ev(arm[1], bind(env, "~", v), fuel)^ else match_arms(t, i + 1, v, env, fuel)^
        }
    }
}
// keys compare by spelling: the dump names them as strings, a walk as symbols
// map keys are unique: a later writer — an explicit key after a spread —
// replaces the earlier entry's value in its place (map_spread_override.ls,
// map_spread_len.ls); `len` and iteration then see each key once (S8.3.1)
fn dedupe_ents(ents, i, acc) {
    if (i >= len(ents)) acc
    else {
        let e = ents[i]
        let seen = [for (x in acc where string(x.k) == string(e.k)) x]
        let next = if (len(seen) == 0) [for (x in [acc, [e]]) for (y in x) y]
                   else [for (x in acc) (if (string(x.k) == string(e.k)) e else x)]
        dedupe_ents(ents, i + 1, next)
    }
}
fn mapv_get(m, k) {
    let hits = [for (e in m where string(e.k) == string(k)) e.v]
    if (len(hits) > 0) hits[len(hits) - 1] else null
}
fn mapv_keys(m) => unique([for (e in m) e.k])
fn sem_eq(a, b) any^ {
    if (is_elmv(a) and is_elmv(b))
        a.n == b.n and sem_eq(elmv_attrs(a), elmv_attrs(b))^ and sem_eq(elmv_kids(a), elmv_kids(b))^
    else if (is_mapv(a) and is_mapv(b)) {
        let ka = mapv_keys(a)
        let kb = mapv_keys(b)
        len(ka) == len(kb) and all([for (k in ka) contains(kb, k) and sem_eq(mapv_get(a, k), mapv_get(b, k))^])
    }
    else if (type(a) == array and type(b) == array)
        len(a) == len(b) and all([for (i in 0 to len(a) - 1) sem_eq(a[i], b[i])^])
    // equality is total (SI4): an error compares like any value and answers a
    // boolean (`error("x") == 1` is false), so no short-circuit here
    else { let x = raw(a)  let y = raw(b);  host(x == y)^ }
}
// one host binary operation on two evaluated operands; an error operand is
// the result (S7.9.3), otherwise the operands reach the host typed `any`
fn binop(x, y, op) any^ {
    let e = first_error(x, y)
    if (e != null) e
    else {
        let a = val(x)
        let b = val(y)
        match op {
        case "+": host(a + b)^
        case "-": host(a - b)^
        case "*": host(a * b)^
        case "/": host(a / b)^
        case "div": host(a div b)^
        case "%": host(a % b)^
        case "++": host(a ++ b)^
        case "**": host(a ** b)^
        case "<": host(a < b)^
        case "<=": host(a <= b)^
        case ">": host(a > b)^
        case ">=": host(a >= b)^
        case "to": host(a to b)^
        case "<:": host(a <: b)^
        case "at": host(a at b)^
        default: stuck("no host operator " ++ op)^
        }
    }
}

// one entry point for every check: fixed initial fuel, empty environment, and
// stuckness surfaced as the symbol 'stuck' rather than a raised error. FUEL is
// a literal here so the module exports only functions.
pub fn run(t) => ev(t, <env>, 400) ^ { 'stuck' }
