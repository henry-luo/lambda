// Semantics POC — VERIFY (Stage 5): the model side of `make semantics-verify`.
// Driver: test/lambda/sem/semantics_verify.py; design:
// vibe/Lambda_Semantics_DSL_POC.md §13.1. The driver dumps a fixture with
// `lambda.exe --emit-ast-dump` and generates two scripts under temp/sem/ that
// import this module: a procedural one printing `diag(...)` as JSON (the
// lowering census, the model's stuckness, the `// expect:` checks) and a
// functional one whose top-level value is `model_value(...)`, so the model's
// answer is printed by the same top-level printer that produced the golden
// (S16.7) and the two texts diff like for like.

import .mod_sem_poc_kernel
import .mod_sem_poc_ingest

// a whole fixture nests deeper than the POC checks: fuel bounds nesting depth
// (ev decrements per level), not step count, so this is a recursion ceiling
fn fuel() => 5000

// the fixture's term: the dump read back and lowered. A failed read or a
// lowering that raises becomes an `unsupported` term, so every way of falling
// outside the model shows up in one census rather than as a crash.
pub fn term_of(dump_path) {
  let dump = input(dump_path, 'mark') or null
  if (dump == null) (<unsupported kind: "dump", why: "unreadable">)
  else normalize(dump) ^ { <unsupported kind: "normalize", why: ^.message> }
}

fn detail(t) {
  let k = string(t.kind)
  if (t.op != null) k ++ " " ++ string(t.op)
  else if (t.n != null) k ++ " " ++ string(t.n)
  else if (t.src != null) k ++ " " ++ string(t.src)
  else if (t.why != null) k ++ ": " ++ string(t.why)
  else k
}

// every `unsupported` leaf of a term, as "kind", "kind op", "kind name" or
// "kind: why" strings — the pending reason the manifest records
pub fn unsupported_kinds(t) => if (type(t) != element) []
  else if (name(t) == 'unsupported') [detail(t)]
  else [for (c in content(t)) for (k in unsupported_kinds(c)) k]

// the leading Mark value of an `// expect:` comment. Trailing prose is dropped
// a word at a time until what is left parses: `125 (15 + 110)` is 125.
fn expect_value(text) any^ => parse(text, 'mark') ^ {
  let i = last_index_of(text, " ")
  if (i == null) raise error("no value") else expect_value(trim(slice(text, 0, i)))^
}

// the same program with its value items replaced by one read of an annotated
// binding at the end of its declarations, wrapped in an array so a list-valued binding keeps its spread
// visible as the array's items (S2.5.1v2)
fn probe(t, nm) => match name(t) {
  case 'lett': { let init = t[0]  let rest = probe(t[1], nm);  <lett n: t.n, init rest> }
  // a recursive group's last child is the rest of the block
  case 'letrec': {
    let fns = for (i in 0 to len(t) - 2) t[i]
    let rest = probe(t[len(t) - 1], nm);
    <letrec fns rest>
  }
  case 'cont': {
    // a block's later declarations hang off its last child (mod_sem_poc_ingest.ls)
    let tail = if (len(t) > 0) t[len(t) - 1] else null
    if (type(tail) == element and name(tail) == 'lett') { let rest = probe(tail, nm);  <cont rest> }
    else (<cont <arr <vr n: nm>>>)
  }
  default: <cont <arr <vr n: nm>>>
}

// one `// expect:` check (§13.1 item 2): the model's value of the binding, on
// its own, against the comment's value. A `(…)` expectation is already the
// spread the probe's array shows; any other value is wrapped to match it.
fn check_expect(t, e) {
  let v = printable(ev(probe(t, e.n), <env>, fuel()) ^ { 'stuck' })
  let expected = expect_value(e.e) ^ { 'unparsable' }
  let want = if (starts_with(trim(e.e), "(")) expected else [expected]
  let status = if (expected == 'unparsable') "unparsable"
    else if (v == 'stuck') "stuck"
    else if (v == want) "ok"
    else "mismatch";
  {n: e.n, expect: e.e, status: status, model: format(v, 'mark')}
}

// the lowering census, the model's stuckness and the `// expect:` results, as
// one map the driver reads back as JSON
// the printing boundary (P12) lives in the kernel (`printable`); what it
// cannot rebuild stays a modelled container and keeps its fixture pending
fn is_mapv(v) => type(v) == element and (name(v) == 'mapv' or name(v) == 'elmv')
// a modelled map or element that survived `printable` cannot print as the host's does
fn holds_mapv(v) => if (type(v) == element) (is_mapv(v) or len([for (c in content(v) where holds_mapv(c)) c]) > 0)
  else if (type(v) == array or type(v) == list) len([for (c in v where holds_mapv(c)) c]) > 0
  else false

pub fn diag(dump_path, expects) {
  let t = term_of(dump_path)
  let kinds = unsupported_kinds(t)
  let outcome = if (len(kinds) > 0) "unsupported"
    else {
      let v = printable(ev(t, <env>, fuel()) ^ { if (^.message == "sem-propagate") (<sem_raised why: ^.source.message>) else (<sem_stuck why: ^.message>) })
      if (type(v) == element and name(v) == 'sem_stuck') "stuck: " ++ string(v.why)
      // the modelled program raised past its top level: natively the script fails
      else if (type(v) == element and name(v) == 'sem_raised') "raised: " ++ string(v.why)
      else if (holds_mapv(v)) "model: map or element value in output"
      else "value"
    };
  {outcome: outcome, unsupported: kinds, expects: [for (e in expects) check_expect(t, e)]}
}

// the model's value of the whole fixture; the generated script's top level
// splices it (S2.5.1v2) exactly as the fixture's own content is spliced
pub fn model_value(dump_path) any | error => printable(ev(term_of(dump_path), <env>, fuel()) ^ { <sem_stuck why: ^.message> })
