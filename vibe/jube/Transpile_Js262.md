# JS262 Transpiler — Fixes Tried & Backed Off

**Date**: 2026-05-23
**Context**: Working down the remaining test262 failures after the baseline reached
34,114 → 34,115. Records what was attempted on the harder remaining failures,
why each was reverted, and what a correct fix would require. The intent is so the
next person does **not** re-derive the same dead ends.

**Runtime baseline**: `test/js262/test262_baseline.txt` at commit `4074119a5`,
release build. Suite is ~99.9% passing; everything left is in deep or
interconnected subsystems.

---

## 0. What landed (for contrast)

| Fix | File | Result |
|-----|------|--------|
| `Symbol` subclass `super()` throws TypeError | `lambda/js/js_runtime.cpp` (`js_builtin_super_constructs_via_construct`) | +1, 0 regressions, baseline 34,115 |

Added `Symbol` to `js_builtin_super_constructs_via_construct` so `super()` to
`Symbol` routes through `js_new_from_class_object` (the `[[Construct]]` path) and
hits the existing "Symbol is not a constructor" throw (`js_runtime.cpp:~1731`).
That helper is **only** consulted from `js_super_call_native`, so blast radius is
limited to `class extends <builtin>` super-calls. This is the template for the
clean cases; the cases below are the ones that look similar but are not.

---

## 1. Per-iteration closure environment (the `CRASH_139` functional-replace tests)

**Tests**: `built-ins/RegExp/named-groups/functional-replace-{global,non-global}.js`
(in `t262_partial.txt`, tagged `CRASH_139`). Also reproduces in
`language/statements/let/let-closure-inside-next-expression` and the
`for-of` body in many forms.

### Symptom
A closure created inside a loop body that **mutates** a captured block-scoped
`let` does not propagate the mutation to the enclosing scope read in the **same
iteration**:

```js
for (let flags of ["g", "gu"]) {
  let i = 0;
  "abcd".replace(re, (m, ...rest, groups) => { i++; return ... });
  assert.sameValue(i, 2);   // FAILS: i === 0
}
```

The replacer runs synchronously and increments its own copy of `i`; the outer
read sees the stale value. (`CRASH_139` is a separate SIGSEGV that only shows up
in a 50-test batch — never reproduced standalone; likely the same machinery under
specific heap state.)

### Root cause
`lambda/js/js_mir_expression_lowering.cpp` (two identical blocks, ~line 10931 and
~11111):

```c
if (use_scope_env && mt->iteration_depth > 0) {
    for (int ci = 0; ci < fc->capture_count; ci++) {
        JsMirVarEntry* cv = jm_find_var(mt, fc->captures[ci].name);
        if (cv && cv->is_let_const) { use_scope_env = false; break; }
    }
}
```

Inside any loop, a closure capturing a `let`/`const` is forced onto a **private
env snapshot** instead of sharing the function scope env. This gives correct
*cross-iteration distinctness* (`for(let i…) fns.push(()=>i)` → `[1,2,3]`) but
sacrifices *same-iteration coherence* (closure writes its private copy; outer
reads the live scope-env slot).

Confirmed via diagnostic: working cases (plain block, function body) keep the
closure aliasing the function scope env; the for-of case hands the closure a
private copy. All escaping cases already work — the **only** broken pattern is
"mutate in closure, read in outer, same iteration", plus function-level captures
mutated by loop closures (`total += x` summed after the loop → 0 not 6).

### Attempted fix (reverted)
Per-iteration scope env, modeled on the **switch statement's per-block env**
(`jm_transpile_switch`, `js_mir_statement_lowering.cpp:3061-3139`), which already
allocates a fresh env and works because every var carries its own
`scope_env_reg` (function-level captures keep the function-env reg, untouched).
Added a `per_iter_env_active` flag to `JsMirContext`, allocated a fresh env at the
top of each for-of iteration, and skipped the snapshot-forcing when active.

### Why backed off
- In **function** bodies it produced `null` reads — a slot/remap mismatch between
  the closure's capture slot (from Phase 1.7b remapping) and the per-iteration
  env layout.
- In **`js_main`** (where test262 tests actually run) it never engaged: top-level
  captured vars use a *different* mechanism (module-var / `from_env`), not
  `in_scope_env`, so the hook never fired.
- The capture model differs between `js_main` and function bodies, so a bounded
  change cannot cover both.

### What a correct fix needs
True per-iteration environments with **environment chaining**: a per-iteration
env for loop-body lexicals, chained to the function env for function-level
captures. That requires changing the env object layout (`js_alloc_env` returns a
flat `Item*`, no parent pointer — `js_runtime_function.cpp:147`), `js_new_closure`,
and **every** `in_scope_env`/`from_env` slot-access site (dozens across
`js_mir_expression_lowering.cpp`). Major architectural change; high regression
risk to the whole closure/loop surface.

### Note on the SIGSEGV (`CRASH_139`)
Never reproduced outside a full 50-test batch with hot-reload. A standalone batch
of just the two tests does not crash (both fail their assertion, exit 1). The
crash needs a specific preceding test to pollute the persistent heap. Not
root-caused; presumed downstream of the same private-env machinery.

---

## 2. Class-expression inner name leaks to the outer scope

**Test**: `language/statements/class/syntax/class-expression-binding-identifier-opt-class-element-list.js`

### Symptom
```js
var A = class B { method(){} static method(){} ; }
typeof B;   // "object" (B === null), should be "undefined"
```
The class **expression**'s inner name `B` is bound in the enclosing scope as
`null` (typeof "object"). It should only be visible inside the class body.

### Root cause
`lambda/js/js_mir_module_batch_lowering.cpp:~4060`, the "Bind class names as
hoisted variables" loop, binds **all** class names — including class
*expressions* — as scope vars in the program scope. `is_declaration` is correctly
`false` for expressions, but the loop's guard only skips *nested declarations*,
not expressions, so `_js_B` gets `jm_set_var` at depth 1.

### Attempted fixes (reverted)
1. **Containment guard** in `jm_current_inner_class_binding`
   (`js_mir_expression_lowering.cpp:167`): verify the access node is lexically
   inside the class span before resolving to the inner binding. Correct in
   principle (and inner self-refs still passed) but did **not** fix the leak —
   `B` resolves via the scope var, a different path.
2. **Skip class expressions** in the hoisting loop (`if (!ce->is_declaration) continue;`).
   Fixed `typeof B` → "undefined" and the target test passed.

### Why backed off
Removing the hoist broke **static-method** inner self-reference:
```js
var A = class B { static who(){ return B; } };
A.who();   // ReferenceError: B is not defined
```
The hoisted scope var `_js_B` serves **two** roles: the (buggy) outer leak **and**
the (needed) static-method inner binding. Instance methods resolve the inner name
via `inner_module_var` (still work); static methods rely on the scope var.
`new C().m()===C` keeps working; `A.who()===A` breaks. Net regression → reverted
both changes.

### What a correct fix needs
A proper class-body lexical scope for the inner name: a `const` binding wrapping
the class body that **static and instance methods** can both see, but the
enclosing scope cannot. The class-expression case (`js_mir_expression_lowering.cpp`
`case JS_AST_NODE_CLASS_EXPRESSION:` ~12120) does not currently set
`inner_module_var` for the expression name and relies on the outer hoist instead.

---

## 3. `Boolean` subclassing loses `[[BooleanData]]`

**Test**: `language/statements/class/subclass/builtin-objects/Boolean/regular-subclassing.js`

### Symptom
```js
class Bln extends Boolean {}
var b1 = new Bln(1);
b1.valueOf();   // TypeError: Boolean.prototype.valueOf requires that 'this' be a Boolean
```

### Attempted fix (reverted)
Add `Boolean` to `js_builtin_super_constructs_via_construct` (same approach as
`Symbol` and the working `Map`/`Set`). `Map` subclassing works perfectly via this
path (instance gets `MyMap.prototype` + working map data), and the `Boolean` case
in `js_new_from_class_object` (~line 1611) does call
`js_new_boolean_wrapper(arg)` then `js_apply_constructed_builtin_prototype(...)`.

### Why backed off
After the change, `b1 instanceof Bln` became **true** (prototype threaded
correctly via `new.target`), but `valueOf()` **still threw** — the final `this`
the derived constructor adopts does not carry the `__primitiveValue__`
(`[[BooleanData]]`) slot that `js_new_boolean_wrapper` set. Map works because its
data is intrinsic to the object; the Boolean wrapper's slot is lost somewhere in
the derived-constructor `this`-adoption of the `super()` return. Since the test
still failed, the change only added a code path without benefit → reverted.

### What a correct fix needs
Trace how the derived constructor adopts the `super()` return value for the
construct-via-construct path (`js_super_call_native:11024`), and ensure the
returned wrapper (with `__primitiveValue__`) becomes the instance rather than a
freshly-allocated `this`. Likely the same applies to `String` subclassing
(`new (class extends String{})("x")` currently throws
"String.prototype method called on incompatible receiver"). `Number` subclassing
already works, so compare those three paths.

---

## 4. Assessed but not attempted (deep / out of scope)

| Test(s) | Why hard |
|---------|----------|
| `RegExp/lookBehind/*` (17) | Lookbehind not implemented in the regex engine |
| `RegExp/named-groups/*unicode_property_names*` | `\u{...}` escapes in group names; regex parser |
| `RegExp/unicode_full_case_folding`, `nullable_quantifier`, `lookahead_quantifier_match_groups` | Regex engine feature gaps |
| `class/elements/privatefieldset-typeerror-{1,6,8}` | Need incremental private-field install in declaration order; throw on set to a not-yet-installed field |
| `class/subclass/.../ArrayBuffer/regular-subclassing` | `Symbol.species` lookup on subclass returns a non-constructor (`ArrayBuffer.prototype.slice`) |
| `class/subclass/.../TypedArray/super-must-be-called` | "Constructor is not defined" in the TA super path |
| `class/subclass/.../Array/...super_multiple_arguments` | Array super with multiple args |
| `tagged-template/cache-eval-inner-function` | Tagged-template caching across `eval` + inner function; eval+closure |
| `statements/break/S12.8_A7` | `eval("break LABEL")` must throw SyntaxError (break target not in eval's own scope) |
| `arrow-function/lexical-super-call-from-within-constructor` | Expected ReferenceError for arrow lexical super |

---

## 5. Diagnostic techniques that worked

- **`run_t262.sh`** (`temp/run_t262.sh`) runs a single test with `sta.js`+`assert.js`.
  Caveat: it does **not** handle `negative:` frontmatter (negative tests show
  exit 0 even when they should throw) and does not auto-add `includes:` files —
  pass them as extra args (`bash temp/run_t262.sh <file> compareArray.js`).
- **Reproduce the batch crash path** by feeding `lambda.exe js-test-batch` a
  `harness:<len>` block + test paths on stdin (hot-reload persistent heap is what
  makes batch behavior differ from standalone).
- **`getenv("JM_DIAG_*")`-gated `log_warn`** in the transpiler is the fastest way
  to see variable classification (`in_scope_env` / `from_env` / `scope_env_reg` /
  slot) at a specific resolution site. Remember `log_warn` goes to `log.txt`, so
  run **without** `--no-log`. Remove all such probes before building for real.
- **Always validate with the full batch** (`test_js_test262_gtest.exe --batch-only
  --feature-summary --js-timeout=30`) and read the "Regression Check vs Baseline"
  box. `--js-timeout=30` avoids false timeouts on slow debug tests. Use the
  release runtime for any run that touches the baseline.

## 6. Baseline-update gotcha: `CRASH_139` batch-instability

A cluster of `class/dstr/async-gen-meth-*` and `class/elements/privatefieldset-*`
tests are **batch-unstable** (SIGSEGV in some batch orderings, pass individually).
They are simultaneously in the baseline **and** flip to `crash-exit` /
non-fully-passing depending on batch timing. The `--update-baseline` gate requires
`crash-exit == 0`, so an update is **blocked** on a run where they crash.

Workflow that works: a crashing run writes them into `t262_partial.txt`; the
**next** run skips them (carry-forward keeps their baseline status), so the update
proceeds cleanly without dropping them. After a successful update,
`clean_partial_list_after_baseline_update` releases them again (they pass
individually) — so the partial list oscillates between ~2 and ~10 CRASH entries.
This is expected, not a new bug.

---

# Part II — Deep RegExp Features: Analysis & Fix Plan

**Date**: 2026-05-23. Covers the ~34 remaining `built-ins/RegExp/*` and
`language/literals/regexp/*` failures.

## 7. Engine architecture (the binding constraint)

The JS regex engine is **Google RE2 + a "match-wider + post-filter" rewriter**
(`lambda/js/js_regex_wrapper.{h,cpp}`, pattern preprocessing in
`js_runtime.cpp` ~13390-14230).

RE2 is a **DFA/NFA engine with no backtracking**. It *cannot* natively do:
lookbehind, lookahead, backreferences, or backtracking-dependent matching.
The wrapper approximates some of these:

- Trailing positive lookahead `X(?=Y)` → `X(Y)` + `PF_TRIM_GROUP`
- Negative lookahead `(?!Y)X` / `X(?!Y)` → `X` + `PF_REJECT_MATCH`
- Backreference `\N` → `(.+)` + `PF_GROUP_EQUALITY` (approximation only)
- **Lookbehind `(?<=Y)` / `(?<!Y)` → STRIPPED ENTIRELY** (`js_runtime.cpp:14009`,
  `processed_pattern.erase(...)`). The constraint is *discarded*, not emulated.

Verified: `"yabc".match(/(?<=x)abc/)` returns `["abc"]` (should be `null`);
`"xabc".match(/(?<!x)abc/)` returns `["abc"]` (should be `null`); captures inside
lookbehind are lost (`/(?<=(abc))def/` on `"abcdef"` → `["def"]` not
`["def","abc"]`). This single gap accounts for all 17 lookbehind failures.

## 8. Failure categorization

| # | Category | Tests | Root cause | Tier |
|---|----------|-------|-----------|------|
| C1 | Lookbehind | 17 (`lookBehind/*` + `named_groups_lookbehind`) | stripped, not emulated | 2/3 |
| C2 | Named-group unicode identifiers | ~6 (`*unicode_property_names_valid/invalid`, `*non_unicode_property_names*`, the `destructure null` ones) | `\u{...}`/surrogate escapes in group **names** not decoded; invalid ones not rejected | 1 |
| C3 | Named-group semantics | ~3 (`groups_object`, `groups_object_subclass`, `unicode_match`, `*_references`) | groups object / `\k<name>` resolution | 1 (mostly) |
| C4 | Unicode case folding | 2 (`unicode_full_case_folding`, `language…u_case_mapping`) | RE2 folding ≠ JS Canonicalize (simple/common fold) | 1 |
| C5 | Backtracking semantics | 3 (`nullable_quantifier`, `do_not_backtrack`(parse), `greedy_loop`) | RE2 leftmost-longest ≠ JS leftmost-greedy+backtrack | 3 |
| C6 | functional-replace | 2 | closure-in-loop bug — **not regex**, see Part I §1 | — |
| C7 | lookahead quantifier groups | 1 (`lookahead_quantifier_match_groups`) | quantified-lookahead capture not preserved by post-filter | 2 |

## 9. Fix plan — tiered

### Tier 1 — Within the RE2 wrapper (contained, low risk, ~9-11 tests)

**T1a. Decode `\u`/`\u{}`/surrogate escapes in named-group identifiers (C2).**
In the `(?<name>` parsing (`js_runtime.cpp:14013` group-name conversion and the
wrapper's name scan), the group **name** is currently taken literally. Add: when
scanning `(?<…>`, decode `\uHHHH`, `\u{H..}`, and surrogate pairs to the actual
code points, validate `ID_Start`/`ID_Continue` (property tables already exist in
`js_regex_generated_property_tables.inc`), and emit the decoded UTF-8 name to
RE2's `(?P<name>`. Reject invalid identifiers with SyntaxError (fixes the
`*_invalid` tests that currently don't throw). High leverage: also fixes the
`Cannot destructure 'null'` failures, which are downstream of the name failing to
parse (so `match.groups.<name>` is missing).

**T1b. Named `\k<name>` backreferences + groups object (C3).** Ensure `\k<name>`
maps to the group's numeric index for the existing `PF_GROUP_EQUALITY` path, and
that the `groups` object is always constructed (even when a named group does not
participate, the key must exist with value `undefined`). Check
`groups_object`/`groups_object_subclass` expectations against the result builder
in `js_runtime.cpp` (the `match` result `groups` assembly).

**T1c. JS Canonicalize for `iu` (C4).** For the `i`+`u` flags, JS uses *simple/
common* case folding (`CaseFolding.txt`), which differs from RE2's folding for
characters like `ΐ`/`ΐ`. A casefold table already exists
(`casefold_seqindex`, `js_runtime.cpp:12510`). Instead of delegating to
`opts.set_case_sensitive(false)` for `iu`, expand each literal char / class member
to its fold-equivalence set in the rewriter (e.g. `[ΐ]` → `[ΐΐ]`),
then compile case-sensitively. Keep RE2 folding for the non-`u` `i` case.

> Tier 1 is the recommended first deliverable: no engine change, each item is
> independently testable, and it clears the parse-level and folding failures.

### Tier 2 — Lookbehind, properly (C1, C7) — the big one

RE2 cannot do lookbehind. Two viable strategies:

**Option 2a (preferred long-term): hybrid engine.** Add a small **ECMAScript
backtracking matcher** (NFA with backtracking, captures, lookaround,
backreferences) and **route only patterns that contain lookbehind /
backreferences / backtracking-sensitive constructs to it**, keeping RE2 for the
linear-time common case (the vast majority of patterns). Detection already half
exists (the wrapper scans for assertions). This fixes C1, C5, C7, the backref
approximation, and the captures-in-assertion gap in one stroke. Cost: implementing
a correct, bounded (step-limited) backtracking engine — significant but
self-contained, and it is the only thing that makes JS regex semantics fully
correct. Candidate: port a compact ES-spec NFA matcher, or integrate **PCRE2**
(JIT off) / **Oniguruma** behind the same `JsRegexCompiled` interface and dispatch
on feature detection.

**Option 2b (interim, lookbehind-only): evaluate lookbehind as a post-filter.**
Keep RE2 for the body; for each candidate match start `p` that RE2 returns,
evaluate the lookbehind subpattern *ending at* `p`:
- Fixed-length `Y`: check `input[p-len .. p]` against `Y` (compile `Y` as its own
  RE2, anchored).
- Variable-length `Y`: try each end-anchored length; expensive but bounded.
- Captures inside the lookbehind: run `Y` with captures and merge the group
  spans into the result's group array (respecting JS group-numbering order).
- Negative `(?<!Y)`: reject the candidate if `Y` matches ending at `p`.
This is a `JS_PF_*` filter added at the *match-start* boundary (mirror of the
existing end-boundary `PF_REJECT_MATCH`). It handles most of the 17 lookbehind
tests; the genuinely backtracking ones (`do_not_backtrack`, `mutual_recursive`,
backref-into-lookbehind) still need Option 2a.

### Tier 3 — Backtracking semantics (C5)

`nullable_quantifier`, `greedy_loop`, `do_not_backtrack` depend on JS's
leftmost-*greedy*-with-backtracking semantics, which RE2's leftmost-longest
automaton does not reproduce. **Only Option 2a (a real backtracking engine)
fixes these.** Do not attempt to coerce RE2 here.

## 10. Recommended sequencing

1. **Tier 1a** (named-group unicode escapes) — highest leverage / lowest risk
   (~6 tests, parser-local). Validate each against the full batch.
2. **Tier 1c** (case folding) and **Tier 1b** (named groups object) — ~3-4 tests.
3. **Decide on the engine question** before touching lookbehind. Option 2b is a
   contained interim win (~12-15 lookbehind tests) but adds a second matching path
   to maintain; Option 2a is the correct end state (fixes C1+C5+C7 and the backref
   approximation, ~22 tests) but is a multi-week project. Recommendation: do
   Tier 1, then pursue **Option 2a (hybrid backtracking engine)** rather than
   investing in 2b, since 2b cannot reach the backtracking-dependent cases and
   would be partly thrown away.

**Risk note:** every change here runs through the shared regex path used by
thousands of passing tests. Each Tier-1 item must be gated/feature-detected so it
only affects patterns that actually use the feature (decoded names, `iu` folding,
named backrefs), and validated with a full `--batch-only` run showing 0
regressions before the next item.

---

## 11. Tier 1a — DONE (2026-05-23): named-group unicode identifiers

**Result: +6 tests, 0 regressions** (debug batch). Tests fixed:
`named-groups/{non-,}unicode-property-names-{valid,invalid,}` (6).
Remaining `*-references` (2) need real backreference semantics (forward/self
`\k<name>` matching empty) — that is the RE2 backref limitation (Tier 2/3).

**Root cause recap.** RE2 itself validates *literal* UTF-8 capture names
(accepts letters like 𝓑, rejects emoji), so literal-name patterns already
worked. The gaps were: (a) `\u`/`\u{}`/surrogate escapes in names were never
decoded — the frontend validator rejected them (name starts with `\`); (b) RE2's
unicode name rules are *more permissive* than JS (it accepts `𝟚` as a start char
and some emoji), so invalid-name tests didn't throw; (c) RE2 rejects ZWNJ/ZWJ
inside names even though JS allows them in IdentifierPart.

**Implementation (all in `lambda/js/js_runtime.cpp`):**
1. `js_regex_decode_name_escapes` / `js_regex_append_decoded_name`: decode
   `\uHHHH`, `\u{H..}`, and surrogate-pair `\u` escapes **only inside** `(?<name>`
   and `\k<name>` regions to UTF-8. Wired in `js_create_regex` before the frontend
   validator and as the base of `effective_pattern`; the original `pattern` is
   kept for `.source`/caching. This makes escaped names behave like literal names.
2. `js_regex_cp_is_id_start` / `js_regex_cp_is_id_continue` (using the existing
   `js_regex_generated_ranges_70_id_start` / `_69_id_continue` tables + `$ _`
   ZWNJ ZWJ) and `js_regex_named_groups_valid`: validate every named group whose
   identifier contains a **non-ASCII** code point (ASCII names are left to RE2),
   throwing SyntaxError on an invalid IdentifierName.
3. `js_regex_re2_group_name_needs_alias`: also alias names containing ZWNJ
   (U+200C) / ZWJ (U+200D) so RE2 accepts them (alias → `JsCapN`, JS name keyed
   from the UTF-8 substring).

**Why blast radius is contained:** the decode only runs when both a `\u` escape
and a `(?<` / `\k<` are present; the ID validation only checks non-ASCII names
(the overwhelmingly common ASCII-named regexes are untouched and still validated
by RE2). Full batch confirmed 0 regressions before baseline update.
