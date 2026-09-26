# Lambda Core Runtime — Fixed Issue Ledger

> **Archive of fixed, resolved, ruled-not-a-defect, closed, and obsolete records**
> moved from the [central issue ledger](Lambda_Issue_Ledger.md).
>
> The central ledger is the only active issue ledger. Add new issues to
> [Lambda_Issue_Ledger.md](Lambda_Issue_Ledger.md), not here. This archive
> preserves the original IDs, resolution evidence, and historical verification
> notes; it is not a working issue list.
>
> **Audience:** engine developers. **Status:** historical fixed-issue archive
> (vibe/), not normative. **Split from the central ledger:** 2026-09-08.
> Semantic and design rulings remain cited by S# / D#; where no formal ruling
> covers a point, the original vibe ledger ID is retained.

## Archive index

This archive contains **165 historical records**: 160 fixed or resolved entries,
one CLOSED design decision, and four records CLOSED by consolidation into
[LR12-24](Lambda_Issue_Ledger.md#lr12-24). LR03-11, LR07-16, LR07-17 and LR10-7, from the
wrong-value group, were fixed on 2026-09-25 (see the central ledger's
"Wrong-value fix pass — 2026-09-25"), and LR07-21, which that pass found, later the same day.
LR07-23 to LR07-27 were found and fixed together by the JIT golden sweep of the same day. LR03-22, LR03-23, LR07-31 and LR13-11 were found and fixed together by the constrained type and `~key` fix pass of the same day. LR03-24 and LR03-25, found by that pass, were fixed on 2026-09-26 by the predicate evaluation pass (S11.4.11). LR07-32 to LR07-36 were found by the subscript probe pass of 2026-09-25/26 and fixed on 2026-09-26 with the S8.2.4v3 implementation; LR07-37, found by the same pass, was ruled and fixed the same day (S10.1.1v2). LR03-30, found by the empty type pass (S11.1.7), was fixed the same day, and so were LR03-31, LR07-38 and LR13-12, found by that fix and by the audit of the day's rulings. LR03-27, LR03-28, LR08-6 and LR11-2 to LR11-4 were closed the same day by the hazard and recursion pass. Also on 2026-09-25, twenty records closed between 2026-09-17 and 2026-09-24 that had stayed in the central ledger were moved here: LR01-14 to LR01-16, LR02-18, LR02-19, LR12-11 to LR12-13, LR12-15 to LR12-23, LR12-26, LR12-29 and LR12-30. §12 was added for them, and LR12-28 moved into it from the end of §11. LR02-20, LR02-24 and LR02-26, C parser gaps, were fixed and moved here the same day. LR02-28, filed and resolved by the S10.1.7 implicit-field ruling the same day, followed. LR03-14 and LR03-18, two symptoms of one range-type defect, followed later that day. LR03-20, which that fix found, was fixed the same day. LR13-10, the element-content check, was fixed by P0 of [the element type plan](<impl/Lambda_Impl_Element_Type_Sharing.md>) the same day. LR05-14 and LR05-15, filed by the string function tuning survey, were fixed by P0 of [its implementation](<impl/Lambda_Impl_String_Func_Tuning.md>) on 2026-09-24. The list/array kind records closed
by [Lambda_List_Fixes (done)](<impl/Lambda_List_Fixes (done).md>) on
2026-09-23 — LR03-12, LR05-9, LR05-10, LR05-11, LR05-12, LR05-13 and LR12-28 — were moved
here with their original IDs, as every central-ledger move is. Duplicate and split records remain separate so their
provenance is not lost. The first sections contain records formerly
interleaved with live entries; §15 preserves the 44 records from the former
resolved/obsolete appendix.

## 1. Compilation pipeline, CLI & REPL (LR_01)

<a id="lr01-r12"></a>**LR01-R12 · `g_template_registry` was a process-global registry · RESOLVED 2026-09-13**
The runtime-globals migration in `46a2c9ee72` moved the registry into
`EvalContext::template_registry`. `template_registry_current_slot()` now resolves
the active TLS-bound context slot, while `g_template_registry` is only the
compatibility macro `(*template_registry_current_slot())`; destruction clears
the active context slot. This removes cross-isolate template visibility and
collision.

Current source confirms there is no process-global registry pointer. The focused
concurrency coverage is present, but its module-state test currently stops at a
separate `module-key-link: name allocation failed for property key 4` in
`chart/vega.ls`; that failure does not restore a global registry and remains an
independent open issue.

<a id="lr01-8"></a>**LR01-8 · `init_module_import` pointer-walk is layout-coupled · RESOLVED 2026-09-05**
`init_module_import` was unreachable legacy C2MIR glue: no MIR Direct path
called it, while the live importer links individual symbols and immutable module
layout records. Removing the dead `uint8_t*` field walk removes its unguarded
`Mod` layout contract outright, consistent with the single MIR Direct pipeline
in **D8.2.5**. Existing paired-loop and module fixtures still pass after the
removal.

<a id="lr01-14"></a>**LR01-14 · A rebuilt `lang-python` module bound `_map_read_field` by its C name · FIXED 2026-09-22**
`py_runtime.h` included `lambda-data.hpp` inside its own `extern "C"` block,
so that header's C++ declarations took C linkage in the Python module alone.
Once 22911fb88 (2026-09-18) declared `_map_read_field` there, a freshly built
`lang-python.dylib` imported `__map_read_field`, which `lambda.exe` does not
export (`-undefined dynamic_lookup` bound it to null), and the first class
definition crashed (`test_py_gtest` 13 of 43 at SIGSEGV; the dylib in use had
been built before that commit, which hid it). The header now includes
`lambda-data.hpp` first, outside the block, and the rebuilt module imports
the C++ name: `test_py_gtest` 39 of 43. The remaining four fail identically
on a clean build of upstream HEAD `bb3909e36` with the same header fix:
`test_py_advanced_oop` prints `[]` for its plugin registry
("py-call-into: unsupported MIR public function dispatch"), and
`test_py_import`, `test_py_packages`, `test_py_pkg_simple` crash on import.

<a id="lr01-15"></a>**LR01-15 · `test-batch` reset the heap under an in-flight satellite compile · FIXED 2026-09-22**
After each script, `test-batch` calls `runtime_reset_heap` and only then
`runtime_teardown_batch_scripts`, which is where a script's satellite queue
was retired and awaited. A worker still lowering the finished script
(`interp_satellite_compile_job`) read types the reset had freed and crashed in
`find_shape_field_by_name` (SIGSEGV at 0x7), killing every later script in
that batch. `run mutation_limits.ls` followed by `nesting_limits.ls` crashed
3 of 10 runs on the pre-rebase HEAD, 0 of 10 on `bb3909e36`, and 7 of 10 with
P1 of the list fixes (timing), taking out 41 of 105 `test_lambda_std_gtest`
cases. `runtime_reset_heap` now quiesces satellite workers first, as
`runtime_cleanup` already did (see [LR01-13](Lambda_Issue_Ledger.md#lr01-13)): 0 of 10, and the std
suite passes 105 of 105 in three runs.

<a id="lr01-16"></a>**LR01-16 · The `auto` tier's output changed from run to run · FIXED 2026-09-22**
Seventeen `test-lambda-baseline` scripts passed with `LAMBDA_TIER=jit` and with
`LAMBDA_TIER=interp`, three runs each, but failed nondeterministically under the
default `auto` tier. `editor/commands_basic` differed from its golden by 58, 15
and 51 lines in three runs, and `graphviz/parser` sometimes passed. The
affected scripts:
- `dom_edit_protocol`;
- eight `editor/*` scripts (`commands_basic`, `dom_adapter`,
  `drawing_block_integration`, `editor_api_basic`, `input_intent_basic`,
  `list_autoformat`, `multi_node_delete`, `paste_basic`);
- six `graph/*` scripts (`graphviz/formatter`, `ordering_groups`, `parser`,
  `route_classes`, `suite`, `structurizr/reference_semantics`);
- two `pdf/*` scripts (`phase1_multipage`, `phase8_invoice_fixtures`).

Other symptoms had the same cause:
- The MathLive markup gate passed 279–485 of 921 cases under `auto`.
- `graph/mermaid/scene_render` lost an edge in some runs.
- `gc_shape_any_lane.ls` reported `nodes: 2` under forced GC, where forced GC
  only shifted the timing.

The pre-P1 binary already failed this way.

**Root cause.** A satellite image reads member names through its own suffix of
the module slab's property-key table: `lambda_module_name_id_at(state, base +
i)`. The base was an immediate taken while compiling,
`lambda_module_state_property_key_count()` on the pool worker. That
thread-local read saw no runtime and returned 0. Publication
(`lambda_module_state_prepare_layout`) then treated any count other than the
base as "suffix already linked" and appended nothing. So the first image to
publish owned the table, and every later image resolved its names through the
first one's keys. In the reduced repro, `ensure` read `st.nodes` as `st.id`
(`null`) and rebuilt the list. An image that did not use any other image's
keys stayed correct, which is why publication timing picked the failures.
Each tier alone was correct because only `auto` publishes worker images.

**Fix** (D8.1.1v12, D8.5.1v7):
- The base is placed at publication and recorded in the layout's new
  `property_key_base` cell.
- Generated satellite code loads it from its own `_sat_N_layout`, so the
  worker reads no receiving-runtime state.
- Publication links by content: a slab that already holds the image's key IDs
  at the recorded base keeps them; otherwise the suffix is appended and its
  base recorded.

**Results.** All 17 scripts and mermaid pass 3 of 3 under `auto`. MathLive
passes 921/921 on `auto` in three runs, and its `baseline.txt` is raised from
206 to 921 cases. A new test hook, `LAMBDA_SATELLITE_SYNC=1`, publishes at the
promoting call. With it, `test/lambda/satellite_property_keys.ls` and
`gc_shape_any_lane.ls` fail deterministically on the old code and pass on the
fix, at `LAMBDA_JIT_THRESHOLD` 1 and 5
(`LambdaTierParityTests.SatellitePublicationKeepsPropertyKeys`). After the fix, `test-lambda-baseline` keeps
only failures that predate it, the UI baseline suite passes 119/119, and the
UI DOM suite passes 127/127. That includes `codemirror_type`,
`pkg_context_menu`, `pkg_focus_policy` and `pkg_keyboard_activation`, which
failed in the earlier colour-work run and had been filed as pre-existing.


## 2. Parsing & AST construction (LR_02)

<a id="lr02-4"></a>**LR02-4 · `AstLoopNode` / `AstNamedNode` layout divergence · RESOLVED 2026-09-05**
`AstLoopNode` deliberately retains its own layout. Its secondary `(key, value)`
binding now uses `AST_NODE_FOR_INDEX`, a named-node-only kind, and the primary
loop binding registers its explicit spelling without an `AstNamedNode` cast.
Capture analysis traverses `AstLoopNode` directly, including join edges. This
keeps the canonical binding identity required by **D8.2.4** typed rather than
depending on coincidental field offsets. `for_at_pairs.ls` and
`for_join_s3b_test.ls` pass on MIR Direct, including forced-GC mode.


<a id="lr02-5"></a>**LR02-5 · `match`-arm `~` references were missed · RESOLVED 2026-08-25**
`has_current_item_ref` (`build_ast.cpp`) walked a match node's scrutinee and
then iterated the arm list with an **empty loop body**, falling through to
`false`. The loop was provably dead code.

**Observable failure:** a pipe never established the current-item context an arm
needed, so

```lambda
xs |> match (1) { case int: (~) * 10
                  default: 0 }        // was: error   now: [10, 10, 10]
```

evaluated to `error`. Only this shape broke — an arm whose enclosing `match`
carries no `~` in its *scrutinee*. `xs |> match (~) { … }` always worked,
because the scrutinee walk detected the reference.

**The arm PATTERN is deliberately not walked.** `doc/Lambda_Expr_Stam.md:961`
rules that `~` inside an arm body is the **matched value**, and a `that`
constraint's `~` is the match subject as well — both rebind, so neither can
consume an enclosing pipe's item. This mirrors the `HANDLER_EXPR` case directly
above, which already models exactly that shadowing.

*Worth recording for the next reader:* the correct result of the repro is
`[10, 10, 10]`, not `[10, 20, 30]`. The scrutinee is the constant `1`, so the
arm's `~` is `1` for every piped item. Reading `~` as the pipe item is the
natural first guess and it is wrong; the docs settle it.

Covered by `test/lambda/match_arm_current_item.ls` on both tiers — five shapes
including the constraint and no-pipe controls, and verified to fail
(`subject_is_const: error`) when the walk is emptied again.

*Reversed 2026-09-25 by S10.1.7v2:* the ruling scopes the current item to its
body, so an arm's `~`, spelled or a bare field name read as `~.name`, is never
free in an enclosing pipe body. A handler's value arm was already treated that
way. `has_current_item_ref` now walks only a match's scrutinee, so
`xs |> match (1) { case int: (~) * 10 }` is whole-value application of a
non-callable value ([LR02-29](Lambda_Issue_Ledger.md#lr02-29)). Counting arm
bodies would also have let an implicit read flip the pipe's mode by binding
state, which S10.1.7v2 forbids. In the fixture, `subject_is_const` became
`outer_restored`, `xs |> (match (1) { … }) + ~`, which maps to `[11, 12, 13]`.
See [LR02-28](#lr02-28).


<a id="lr02-9"></a>**LR02-9 · Binary `&` / `!` type operators rejected in annotation position · RESOLVED 2026-08-25**
Intersection and exclusion evaluated correctly as patterns but were rejected by
a declaration or parameter annotation, with a diagnostic that named the binding
rather than the contract. Both halves are fixed.

**Three defects, not one.** The chain broke in three places, and each had to be
found from the one before:
1. `static_boundary_relation` (`build_ast.cpp`) recognised a binary **target**
   only for `OPERATOR_UNION`; `&`/`!` fell through to the generic tail and were
   rejected outright. This was the actual capability gap.
2. The type-pattern parser lowered a type-level `&` to **`OPERATOR_OR`**
   (`parse_type_pattern.cpp`, with a comment calling it "odd" but reproducing
   it), so even after (1) the annotation carried an operator the boundary
   checker does not treat as a set operation. Normalised to
   `OPERATOR_INTERSECT`, matching expression space and the sibling site in the
   same file; consumers accept `OPERATOR_OR` as the historical spelling.
3. `lambda_type_format_name` rendered only `|`, so an `int & string` contract
   printed as the bare word `type` — the diagnostic half of this entry.

Also widened `promote_type_union_expr` so a type-set operator between two type
values builds a first-class binary type in expression position too.

**Semantics are unchanged and now agree across positions** — `1` is admitted by
`number & int` and by `int ! string`, and rejected by `int & string`, exactly as
`is` reports. The rejection of `let a: int & string = 1` is *correct*: nothing
satisfies that intersection. Diagnostics now read
`cannot initialize 'a' of type int & string with int` and
`argument 1 expected int & string, got int`.

Covered by `test/lambda/type_set_operators.ls` (pattern, alias, inline
annotation and parameter positions, both tiers) and
`test/std/negative/type_set_operator_mismatch.ls` +
`NegativeScriptTest.TypeSetOperatorContractIsNamed`. Closes the implementation
half of **SO9** and the `&`/`!`-unimplemented warning in the string-pattern
design record.


<a id="lr02-14"></a>**LR02-14 · Keyword-as-name handling is a patchwork; S16.10 rules it · RESOLVED 2026-08-27**
Ruled 2026-08-27 as **S16.10** (spec v16.0.0; deliberation and probe table in
`Lambda_Design_Syntax.md` §7.24): keywords never name bindings — the whole
lexer keyword table, E201 at the declaration site, no quoted escape — while
map keys, element tags, attribute names, and `.`-member steps admit keywords.
Divergences to fix:

1. `import edit: …` parses and **every use** fails (`expected a type
   pattern` — the `edit` declaration keyword captures the statement);
   `import 'edit': …` parses and creates an **unreachable binding**
   (`'edit'.x` is silently null). Both must become E201 at the import line.
2. `let if = 1` parses; every use fails (`expected an expression`).
3. **`let type = 1` parses and `type` then silently reads the base type** —
   a silent wrong answer, the priority defect of the cluster.
4. `<if a:1, "x">` is rejected (`expected an element tag`) but is legal
   under S16.10.2 — the tag position must accept keyword words.
5. E201 covers only `last` and must extend to the whole table, in the C
   parser and the Tree-sitter reference grammar alike.

Migration: ~55 keyword-named bindings in `test/` + `lambda/` (offset 12,
group 9, state 8, to 5, by 4, …; breakdown in §7.24); 0 keyword import
aliases.

*Reference-grammar half, 2026-09-24.* Item 5 had landed in C only. The grammar
still took any keyword wherever its parse state had no action for it, so
`let if = 1` and `let a = let b = 2` (as `let a = let`) parsed, and it refused
a data name whose keyword could also start a statement (`{while: 1}`,
`<div if: 1>`). It now declares a tree-sitter `reserved` set (CLI 0.25.10,
ABI 15) holding every capture-real word of Appendix K.1. A reserved word lexes
as its keyword wherever an identifier is expected, so a binding position
rejects it, and `_keyword_name` admits it back in the data-name positions C's
`token_is_key` reads. That retired three per-keyword patches: `_misplaced_let`
with its never-emitted `let_outside_list` external, `_tier3_kw`, and the
S16.6.6 `_expr_body_start` guard (LR02-R8). Corpus differential over 1934
files: 7 files newly rejected, all negative tests. C rejects five of them
(three with E201, two keyword import aliases at parse time). The other two
are runtime tests built on the unnamed `fn (x) { … }`, which C misparses into
a runtime error (LR02-21). 2 files are newly accepted (`keyword_data_names.ls`,
`validator/schema_xml_basic.ls`). The trees of every file both grammars accept
change only in node names, plus comments the retired guard used to swallow. The
work surfaced LR02-21 to LR02-23.


<a id="lr02-15"></a>**LR02-15 · Sys-func shadowing; S12.3.7 rules it user-first · RESOLVED 2026-08-27**
Probes 2026-08-27 (debug build): `fn sum(a) => 99` then `sum([1,2,3])`
compiles, executes on the interpreter tier, prints **no result**, and dies at
teardown (ASan dealloc failure); `fn len` / `fn min` shadows likewise;
unshadowed `sum(x) + len(x)` is fine. Ruled 2026-08-27 as **S12.3.7** (spec
v16.1.0; deliberation in `Lambda_Design_Syntax.md` §7.25): user-first,
module-lexical shadowing with a mandatory compile warning; `pub` export
extends to importers through the explicit import only; a non-callable shadow
is the not-callable error, never builtin fallback; keywords/base-type words
stay un-shadowable (S16.10.1). Implementation: one resolution point in
`build_ast` covering both tiers ("is this name module-bound?" before builtin
registry lookup), the shadow warning, and a regression test for the
crash shape.


<a id="lr02-16"></a>**LR02-16 · `lambda.*` namespace not implemented · RESOLVED 2026-09-08**
Ruled 2026-08-27 as **S17.2.1/S17.2.2** (semantics v16.2.0) and **D7.2.4**
(design v1.38.0); deliberation in `vibe/Lambda_Package.md` §1b. Implemented
the complete namespace migration:

1. `lambda.sys.*` resolves directly to the existing system-function registry,
   including the S12.3.7 shadow escape; `lambda.math` and `lambda.io` use the
   same built-in module rows as their bare aliases.
2. `lambda` is barred from binding declarations by the direct lexer’s
   reservation check, yielding E201 while member/data-name positions remain
   available.
3. Shipped packages moved from `lambda/package/` to their `lambda/` roots,
   with typesetting moved specifically to `lambda/doc/math/`; all live imports,
   bridge scripts, tests, and release packaging now use the canonical paths.
4. Regression coverage is in `test/lambda/lambda_namespace.ls` and
   `test/lambda/negative/semantic/lambda_namespace_root.ls`; focused probes
   cover the registry, built-in alias, document package, and reserved-root
   paths. The affected DOM package regressions pass 5/5; `test_lambda_gtest`
   passes 837/837, input passes 2104/2104, MathLive passes 921/921, and
   `make test-lambda-baseline` passes 5075/5075. The previously failing
   `test_js_gtest` case `dom_3d_transform_inline_rect` now passes; additionally,
   `make test262-baseline` passes 40261/40261 with zero regressions.

<a id="lr02-18"></a>**LR02-18 · The Tree-sitter reference grammar has no list literal (S2.5.1v2, S2.5.5v2) · FIXED 2026-09-24**
`grammar.js` `_parenthesized_expr` admits one expression, optionally after
`let` bindings, so `(1, 2)` parses as `ERROR` in the `lambda-cst` verifier's
grammar (checked with `tree-sitter parse`, 2026-09-22); `()` is rejected with
it. The C parser accepts both (`()` since P1 of
[List Fixes](<impl/Lambda_List_Fixes (done).md>)). Neither conformance script
(`test/ts_s16_conformance.sh`, `test/c_s16_conformance.sh`) has a list case,
so the divergence is unguarded. Fix belongs with the grammar work of P5
(`make generate-grammar`), with accept cases for `()`, `(a, b)` and
`(let x = 1, x, 2)` in both scripts.
*Fixed 2026-09-24 (grammar side; C unchanged):* a `list` node takes `()` and
two or more items, each a `let` or an expression in any order; `(x)` stays
the item. An arrow head is now a parameter list only (a GLR fork against the
group, as C's `arrow_head_candidate`), and the scanner no longer starts a
statement at a return type closed by `=>`, since `()` and `(a, b)` share the
arrow's state. That also fixed `(x) int => x` and `(x, y: int) int => x`
(rejected before) and `(1, 2) => 3` (accepted before). Pinned in both scripts'
"list literals (LR02-18)" and "arrow heads are parameter lists" sections.

<a id="lr02-19"></a>**LR02-19 · A let-group kept only its last item; a lone declaration was a value (S2.5.4, S2.5.5v2) · FIXED 2026-09-22**
`direct_let_group` (`build_ast.cpp`) kept one non-declaration item — the
last — so `(let x = 1, x, 2)` was `2` and `(1, let x = 2, x)` was `2`, on
both tiers and on HEAD; both evaluators already build a list from
declarations plus several items, so only the builder was wrong. A group
with a single declaration skipped the let-group path and evaluated the
declarator itself (`(let x = 5)` was `5`), and a block holding a single
declaration took that declaration's type, so `[{ let x = 5 }, 9]` took the
compact int lane and was `[0, 9]` on the interpreter. All three now follow
the ruling: every non-declaration item stays, a declaration-only group or
block is `null` and splices nothing in an item position. No script or
package in the corpus used a multi-item let-group or a lone-declaration
group (instrumented scan, 2026-09-22). Fixture:
`test/lambda/list_declarations.ls`.

<a id="lr02-20"></a>**LR02-20 · The C parser caps list literals, elements, calls and decompositions at 64 items (S2.5.1v2, S2.5.5v2, D8.1.2v3) · FIXED 2026-09-25 (found 2026-09-24)**
`lambda_parser.c` gathers the children of a flat reduction in fixed
proof-of-concept stack buffers, so it rejects valid source the reference
grammar accepts. `parse_group_or_arrow` (`children[64]`, :1127) fails a
65-item list literal `(0, 1, …, 64)` with E100 "too many grouped expressions
in parser POC"; `parse_element` (`children[64]`, :943) caps attributes plus
the content child (:970, :1010); `parser_parse_postfix_delimited`
(`children[65]`, :1625) caps call arguments and index dimensions (:1631); and
`parse_assignment_clause` (`LambdaToken names[64]`, :1467) caps decomposition
names (:1484). A 65-item array parses, because `parse_array` builds its items
as a `parser_list_append` chain. S2.5 sets no item limit, so under D8.1.2v3
the C side is wrong; `lambda-cst` reports such a list as `missing` (grammar
accepts, RD rejects). It surfaced once LR02-18 gave the grammar list
literals. Recorded rather than fixed (USER, 2026-09-24). Sibling, unruled:
`parse_crud_statement` caps comma-joined `put`/`del` edits at
`LAMBDA_CRUD_MAX_CLAUSES = 32` (:2207), and no PTH60v3 text sets a limit —
confirm it is unintended before lifting it.
*Fix notes (2026-09-24 survey).* Consumers are unbounded: a reduction passes
`children` by pointer and count to a synchronous sink, `syntax_sink_reduce`
(`build_ast.cpp`), whose GROUP/ELEMENT/POSTFIX/LET handlers walk
`child_count` (the reduction tape that copied them is retired, LC3.9).
A call over 16 arguments still meets the semantic `ERR_FUNCTION_ARGUMENT_LIMIT`
(rest parameters exempt), whose `binder_env`/`resolved` arrays are
bounds-checked. A growable buffer with inline storage keeps every reduction
byte-identical; switching to `parser_list_append` chains would change the
GROUP/ELEMENT/CALL shapes that `build_ast` and the `child_count` assertions in
`test/test_lambda_parser_poc_gtest.cpp` rely on. The parser links only libc
(`lambda-cst` and the parser POC gtest build the parser sources alone), and a
heap buffer must not be shared with a `parser_probe` copy. `parse_postfix`'s
`children[65]` (:1674) only ever uses slot 0. Pin the fix with mirrored accept
cases (a 65-item list literal, 65 call arguments) in both S16 conformance
scripts.

*Fixed 2026-09-25:* the four fixed buffers are now `ParserGrowBuffer`s (`lambda_parser.c`). Each has 64 (or 65) inline slots and spills to the heap past them, so every reduction keeps the same children in the same order and the sink sees any count. A buffer is a local of one parse call, released by a thin wrapper around the old body (`parse_element_into`, `parse_group_or_arrow_into`, `parse_assignment_clause_into`, `parser_parse_postfix_delimited_into`), so no early return leaks it and no `parser_probe` copy shares it. The parser links only libc, so the spill uses `malloc`/`realloc`/`free` under `RAWALLOC_OK`. The "in parser POC" limit diagnostics and "too many decomposition names" are gone; a failed spill reports "out of memory while parsing". `parse_postfix` keeps its own buffer, sized to the three children its member, query and handler forms use rather than 65. A call over 16 arguments still meets the source-argument limit after parsing (E230, D6.2.2v2). The `put`/`del` cap of 32 (`LAMBDA_CRUD_MAX_CLAUSES`) is unchanged, since no PTH60v3 text rules on it. Fixtures: `test/lambda/parser_item_limits.ls` (70 list items, attributes and decomposition names, all tiers), `LambdaRdParserPoc.ReducesMoreThanSixtyFourChildren`, and mirrored accept cases in both S16 scripts (a 65-item list, 65 call arguments, 70 attributes, 70 names; 350/350 C, 337/337 Tree-sitter).

<a id="lr02-24"></a>**LR02-24 · The C parser reads `x is 1 to 5` as `(x is 1) to 5` (S11.1.3, S11.1.6v2) · FIXED 2026-09-25 (found 2026-09-24)**
Both front ends accept `let r = x is 1 to 5`, but they read it differently.
- **Reference grammar:** `is` takes a `_type_pattern` on its right (`grammar.js:144`), which reads `1 to 5` as a `range_type` (`:1351`). So `r` is a membership test.
- **C:** builds `(x is 1) to 5`, a range whose start is a bool. The script compiles with no diagnostic and fails at run time ("Script execution failed"; in an array literal the item is `[error]`).

The cause is the `is` arm of the Pratt loop, which reads its right side with the bare `parse_type_slot` (`lambda_parser.c:1848`). `parse_type_slot_mode` has no `to` case, so the slot ends after `1`. `to` binds tighter than `is` (`LAMBDA_BP_SET` 50 against `LAMBDA_BP_MEMBERSHIP` 40), so the loop then folds it over the finished `is` node.

Annotations already handle the range. `parse_annotation_type_slot_value_mode` reads a trailing `to` into `LAMBDA_REDUCTION_FLAG_ANNOTATION_RANGE` (:796). So `type R = 1 to 5`, `let y: 1 to 5 = 3` and `case 1 to 5:` all work. `x is (1 to 5)` is `true`, and `x in 1 to 5` is unaffected.

The rulings put C in the wrong. S11.1.6v2 makes `is` a boundary-type position, and S11.1.3 applies the range type's membership rule there. The user precedence table (`doc/Lambda_Expr_Stam.md`, "Operator Precedence") also binds `to` (10) tighter than `is` (11). An S16 accept case cannot pin the fix, since C already accepts the text. It needs a `test/lambda` golden instead: `3 is 1 to 5` is `true`, and `9 is 1 to 5` is `false`.

*Fixed 2026-09-25:* `parse_type_slot_mode` now reads two literals joined by `to` as one atom, as the grammar makes `range_type` a `primary_type`. A literal here is a non-null one (`LAMBDA_TOK_INTEGER` through `LAMBDA_TOK_NAMED_VALUE`, the grammar's `_non_null_literal`). So `x is 1 to 5` tests membership, `x is 1 to 5 and y` stops at `and`, and a union of ranges stays one type. `to` continues across a line break (S16.2.2v2). A bound that is not a literal (`x is 1 to n`) makes no range type in either parser, so that form still reads `(x is 1) to n`. Literal ranges in annotations and match arms now take the same atom instead of the annotation path's `to` arm; an AST dump of all 1,952 tracked `.ls` files changed only one census line (`match_expr.ls`). Fixture `test/lambda/is_range_type.ls` (all tiers). Found on the way: [LR03-18](#lr03-18), where `is` never matches a range inside a union.

<a id="lr02-26"></a>**LR02-26 · The C parser ends a type at a line-start `?` (S16.2.2v2, S16.2.1) · FIXED 2026-09-25 (found 2026-09-24)**
S16.2.2v2 puts `?` in the continue-only set, so after a complete expression a line-start `?` continues it. The reference grammar applies this in types: its scanner never opens a statement at `?` (`classify_start`, `scanner.c:296`). So `type T = int` ⏎ `?` is `type T = int?`.

C rejects every such form:

| Source | C diagnostic |
|---|---|
| `type T = int` ⏎ `?` | E100 "expected an expression" at the `?` |
| `let x: int` ⏎ `? = null` | "expected '=' after let binding" |
| `fn f(a: int` ⏎ `?) { a }` | "expected ')' after parameters" |

The last rejection also breaks S16.2.1, which binds the next line inside an unclosed bracket.

The cause is in `parse_type_slot_mode` (`lambda_parser.c:683`), which continues across a line break only for `| & !`. Its `nesting` counter counts only brackets opened inside the slot, not an enclosing parameter list. Inside a type's own brackets, C does continue (both accept `type T = [int` ⏎ `?]`). The ruling puts C in the wrong. Neither S16 script has a line-start `?` in a type; add mirrored accept cases for the three forms.

*Fixed 2026-09-25:* the type slot's line-break rule now continues at `?` as well as at `| & !`, the continue-only tokens a type can take (S16.2.2v2), so all three forms parse. The parameter-list form no longer depends on the slot's own bracket count. A line-start `+`, `*` or `[` still ends the type, since each can begin a statement (S16.2.3v3); the reference grammar likewise rejects a line-start dual-role token inside a bracket, as in `(1` ⏎ `+ 2)`. Fixture `test/lambda/type_line_start_optional.ls` (all tiers), and mirrored accept cases for the three forms in both S16 scripts.

<a id="lr02-28"></a>**LR02-28 · An implicit read under a nested `~` binder read that binder's `~`, not the body's subject (S10.1.7v2) · RESOLVED 2026-09-25 by ruling (found 2026-09-25)**
`resolve_identifier` (`build_ast.cpp`) lowers an implicit field read to `~.name`, so it reads whichever `~` is innermost at run time. Three constructs rebind `~` inside a `that` body: a `match` arm (S11.2.1), a handler's value arm `e ^ { … } ~ { … }` (S7.6.1v4), and a constrained arm `case T that (…)`. Under one of them a bare name read that binder's value, so `{status: "open", total: 5} that (match status { case "open": total > 0 default: true })` read `"open".total` and was `null`. Methods were unaffected, because their declared fields bind to the receiver, not to `~`.

S10.1.7, as first written, said such a body reads "a field of that subject", which pointed at the proviso's subject. It also glossed the rule as "reads `~.age`", which pointed at the innermost `~`. The entry asked which reading holds.

*Ruled 2026-09-25 (USER):* the three binders align with the proviso. `~` binds the current item and may be omitted or spelled. The current item is scoped to the body, and outside the body the outer binding is back. So the innermost current item wins, which is what the lowering already did under a nested binder, and the example above is `null` by design. What changed is that these bodies now read implicit fields everywhere, not only inside a `that`: `match {age: 20} { case map: age > 18 }` reads `~.age`. S10.1.7 had already reached master, so the extension is S10.1.7v2 (spec 40.0.0), which also closed SO49 (a type constraint reads implicit fields in any position).

*Implemented 2026-09-25:*
- **Implicit reads on:** the resolver turns them on for a match arm body, a handler's value arm and every `T that cond` body.
- **Implicit reads off:** a match pattern, and a write target (`x = …`, `put`/`del`). Without the write-target rule, `nope = 5` in an arm stored into the matched value instead of reporting that `nope` has no binding.
- **Resolution order:** an `import math` constant resolves before the implicit read, and so does a module or namespace prefix spelled as a member's object. Without the prefix rule, `m.sqrt(~)` in an arm (`import m: math`) became a call on `~.m`.
- **Free-`~` test:** `has_current_item_ref` stops at match arms and at a nested `|>`/`|:`/`that` body. This reverses [LR02-5](#lr02-5), and see [LR02-29](Lambda_Issue_Ledger.md#lr02-29).

Fixture `test/lambda/implicit_current_item.ls` (both tiers, and the T0 subset).

<a id="lr03-12"></a>**LR03-12 · Occurrence/array type families not implemented (S11.1.1v3, S11.1.6v2, S16.8.6v3) · FIXED 2026-09-23**
The parser reads `T[n]`, `T[n+]`, `T[n, m]` as occurrence counts
(`parse_type_pattern.cpp` `apply_occurrence`, `grammar.js` `occurrence_count`)
where S11.1.1v3 makes `T[n]` an array of exactly n and S11.1.6v2 spells counts
`T{n}`/`T{n,m}`/`T{n,}` (string islands included: `\(d{3})`); `T{n,m}` does
not parse. Boundary semantics: `null is int*` is false (ruled true),
`[] is int?` is true (ruled false), `[1,2] is [int*]` trips the bare-`[T]`
lint. Migration: every `[n]`/`[n+]`/`[n, m]` occurrence in the tree and
`doc/Lambda_Type.md` §Type Occurrences. Design record:
[`Lambda_Type_Pattern.md` §1.3](Lambda_Type_Pattern.md).
*Fixed 2026-09-23 (P5):* the families are split by bracket in the C parser,
the reference grammar and the islands; an occurrence admits what a run is at a
boundary and is a run of the sequence's items in a slot (matched by
backtracking over the counts it can take); the array family admits only
arrays and checks a counted length; `<:` relates the two, and `list`/`range`
are the specialized array kinds. The retired spellings are rejected with a
diagnostic naming their replacement. Two implementation notes: a `{` count
binds tight and never applies to a return type (a spaced or return-position
brace is a body), and the old `max_count != 0` "was it set" sentinel had to
go, since `int[0]` is a legitimate zero-length array. Record:
[plan §14](<impl/Lambda_List_Fixes (done).md>).

<a id="lr03-11"></a>**LR03-11 · Sized-int and literal-union contracts admit wrong values · FIXED 2026-09-25**
`fn f(x: u8) { x }; f(-1)` returns `255` and `fn g(x: 1 | 2) { x }; g(3)`
returns `3`, on both tiers. `let x: i8 = 300` is `44` on the interpreter; the
JIT rejects it, but with the internal name `expected num_sized, got int 300`.
A sized boundary wraps where **S11.4.5** requires value-aware admission and
**S11.4.1v3** forbids a wrong value (see also **S4.2.4**); an integer
literal-union contract is not checked at all (a string literal union such as
`"a" | "b"` is).

*Fixed 2026-09-25:* three causes, all ruled by S11.4.5, S11.4.1v3 and S11.2.1.
- **Sized ints.** T0's `interp_coerce_declared_numeric` sent every `i8`…`u32` and `u64` binding contract through the wrapping conversion (`coerce_num_sized`, `coerce_uint64`). Binding boundaries now take `lambda_type_check`'s value-aware admission, as the JIT already did (D8.3.5: admission is not a cast); only an explicit `u8(v)` wraps. The JIT's native `u32` boundary rejected with a bare `ITEM_ERROR`; its cold arm now runs the shared check, so the rejection carries its E201.
- **Literal types.** A numeric literal contract was admitted by its carrier, and the static relation proved any literal contract by TypeId, so the JIT also dropped the check on `x: "a"`. `lambda_literal_contract_value` (`transpile_shared.cpp`) recovers a literal contract's value (never from the shared `LIT_*` markers). Admission, `lambda_type_matches` (by `==`, as a literal match arm is), the validator's primitive branch and `static_boundary_relation` (deferred, never proven by TypeId) use it.
- **Diagnostics.** `type_numeric_contract_name` names sized types (`expected u8`, not `num_sized`) in contract and validator messages, and `lambda_type_format_contract_name` prints a literal contract's value on the expected side (`expected 1 | 2`, `expected "a"`).

Fixtures: `negative/runtime/sized_admission_{param,declaration,u32_lane,u64}.ls` and `literal_admission_{union,string_param}.ls` (`ExpectRejectedOnEveryTier`); `sized_admission_values.ls` and `type_literal_admission.ls` in `kTune27TierParity`. `tune21_u32_decl_lane.mir-check` now counts the one cold-arm call instead of forbidding it. Found on the way: [LR03-15](Lambda_Issue_Ledger.md#lr03-15), [LR03-16](Lambda_Issue_Ledger.md#lr03-16).

<a id="lr03-14"></a>**LR03-14 · A range-typed parameter rejects every integer, and the tiers split on a range argument (S11.1.3, S1.6) · FIXED 2026-09-25**
A parameter declared with a range type, such as `fn f(x: 1 to 5) { x }`, is wrong on both tiers. An alias (`type R = 1 to 5`, `fn f(x: R)`) behaves the same:

| Call | Interpreter (and the default `auto` tier) | JIT |
|---|---|---|
| `f(3)` | rejected at compile time: `error[E207]: argument 1 expected range, got int` | same |
| `f(1 to 5)` | passes the static check, then fails at run time: "type check at argument 1 of _f_0 failed: expected range, got range" | admitted: `f` returns the range, and `[type(r), r is error]` is `[range, false]` |

S11.1.3 applies the range type's membership rule "in annotations, match arms, and value expressions". Under it, `f(3)` must be admitted and `f(1 to 5)` rejected, since a range is not an integer member. Every other boundary follows the rule: `let y: 1 to 5 = 3` is admitted and `= 9` is rejected, on both tiers; `3 is R` is `true`; and `case 1 to 5:` matches. The parameter boundary is the only one that doesn't. The static check treats the parameter as the `range` container kind, and so does the JIT's argument check. The interpreter's run-time check does reject the range argument, but it never sees an integer, because the static check has already refused it. So no call succeeds on the interpreter, and on the JIT only the wrong one does.

The static rejection comes from `lambda_ast_validate_call_arguments` (`build_ast.cpp`), whose `lambda_static_boundary_relation` (`build_ast.cpp:1708`) rejects `int` against the parameter's range type. The root cause of that, and of the JIT's admission of a range, is not yet located.

The diagnostics add confusion. Each one names the membership type `range`, the same word as the container kind, so even the correct `let` failure reads "expected range, got int 9". No test or package declares a range-typed parameter.

*Fixed 2026-09-25, with [LR03-18](#lr03-18), which has the cause:* a range type wore the range value's tag. On every tier, `f(3)` and `f(3.0)` are now admitted, `f(9)` fails at run time with "expected 1 to 5, got int 9", and `f(1 to 5)` is a compile error ("argument 1 expected 1 to 5, got range"). The static relation now treats a range contract as it treats a literal one (S11.2.1): an argument whose carrier fits the range's domain is left to the run-time check, and any other is rejected. Diagnostics name a range by its bounds: `expected 1 to 5`, `expected "a" to "e"`.

<a id="lr03-18"></a>**LR03-18 · `is` never matches a range inside a union type (S11.1.3) · FIXED 2026-09-25**
```
type R = 1 to 5 | 10
let a = [3 is R, 10 is R, 7 is R]    // [false, true, false]
let b = 3 is 1 to 5 | 10 to 20       // false
```
`3 is R` should be `true`: S11.1.3 applies a range type's membership rule in annotations, match arms and value expressions alike. The other two positions agree with it. `let v: R = 3` is admitted, and `match 3 { case 1 to 5 | 10: … }` takes the arm, because a match arm splits a union and tests each member. A range alone is right too: `3 is 1 to 5` is `true`. So the fault is in `is` against the union type, where the range member never matches. Both tiers give the same result, and so did the binary from before LR02-24's fix, through a type alias.

*Fixed 2026-09-25.* The fault was not in `is` or in unions. `parse_type_pattern.cpp` built a range type with `LMD_TYPE_RANGE`, the tag of a range value, where D3.1.1v4 puts it under the shared `LMD_TYPE_TYPE` tag, told apart by its kind. Six sites special-cased the pair (`fn_is`, `lambda_type_matches`, the `let` static exemption, alias wrapping, a declared `let`'s AST type, the JIT's `let` carrier). Everything that dispatched on the tag read "an int between the bounds" as "a range", on both tiers:
- **Schema validator.** It had no range case ("Unsupported type for validation: 16"), so `is` failed whenever the range sat inside a union, map or array type (`{a: 3} is {a: 1 to 5}` and `[3, 4] is (1 to 5)[]` were `false`), and `lambda.exe validate` rejected range-typed fields.
- **Map layout.** A range-typed field was laid out as a pointer to a range. Passing `{a: 3}` to `fn f(p: {a: 1 to 5})` segfaulted: admission rebuilt the map in the contract's layout, and the validator read the int 3 as a `Container*` (`map_field_to_item`). `let p: {a: 1 to 5} = {a: 3}` was a static E201, and a nominal `type Gauge { level: 1 to 5 }` read `null`.
- **Static checks and the JIT's argument lane.** Both read a range-typed parameter as a range ([LR03-14](#lr03-14)); a `(1 to 5)[]` parameter rejected `[3, 3]`.
- **Subtyping.** `<:` compared tags, so `R <: int` was `false` and every range was below every other (`(1 to 9) <: (1 to 5)` was `true`).

The fix builds the range type with `alloc_type_kind(…, TYPE_KIND_RANGE, …)`. Two helpers in `lambda-data.hpp` (`lambda_type_is_range`, `lambda_range_type_domain`) and one membership test in `lambda-eval.cpp` (`lambda_range_type_contains`, over `lambda_range_type_bounds`) serve every consumer:
- `fn_is` and `lambda_type_matches` test the kind.
- The validator has a range case (`validate_against_range_type`). The retag makes it mandatory, since the TypeType fallback would read the lower bound as a nested `Type*`.
- `static_boundary_relation` treats a range contract as it treats a literal one (S11.2.1). A range source is never PROVEN, since its members keep their own carriers.
- `<:` relates ranges by their members (S11.1.4v2), and `contract_semantics_equal` compares bounds; the tag alone equated `1 to 9` with `1 to 5` and would have let a map shape skip admission.
- The contract formatter names a range by its bounds.

A member keeps its own carrier: `3.0 is 1 to 5` is `true`. So a range contract has no native lane. Fields and parameters hold the boxed Item, as unions do; `lambda_canonical_rep` returns the Item rep, where the new tag would have claimed a Type pointer; a `let` keeps its initializer's carrier, as before. The `let`-only static exemption is gone, and the other special cases test the kind.

Fixtures: `range_type_membership.ls`, pinned in `kTune27TierParity`; `negative/runtime/range_admission_{param,range_value,field,char}.ls` (`ExpectRejectedOnEveryTier`) and `negative/semantic/range_argument_static.ls`. All 971 goldens pass with `LAMBDA_TIER=jit` and with `interp`, and compiling all 1,955 tracked `.ls` files reports the same errors before and after.

*Residue:* `<:` tries each arm of a union whole, so `1 to 5 <: (1 to 3 | 4 to 5)` is `false` ([LR03-21](Lambda_Issue_Ledger.md#lr03-21)). Found on the way, all older than this fix: [LR03-19](Lambda_Issue_Ledger.md#lr03-19), [LR03-20](#lr03-20), [LR07-30](Lambda_Issue_Ledger.md#lr07-30), and two more symptoms of [LR03-16](Lambda_Issue_Ledger.md#lr03-16).

<a id="lr03-20"></a>**LR03-20 · Object construction does not check its field contracts (S11.4.10) · FIXED 2026-09-25 (found 2026-09-25, while fixing LR03-18)**
`type Obj { a: int }` then `<Obj a: "x">` is admitted on both tiers and prints `<Obj a: 4317266544>`, the string's pointer read from the int field. A literal or range field admits any value: `type Obj { a: 1 | 2 }` and `type Obj { a: 1 to 5 }` both accept `<Obj a: 9>`. S11.4.10 verifies a declared type when the value crosses it, and lists the nominal binding of an element among the crossings. A map-type alias checks its fields: with `type P = {a: 1 to 5}`, `let p: P = {a: 9}` fails with E201. Reproduces on the binary from before the [LR03-18](#lr03-18) fix.

*Fixed 2026-09-25.* Construction wrote each value straight into its field's lane, on both tiers: `set_field_value` trusts its input, and `build_ast` typed the literal without looking at its fields. Each field now meets its contract under the three outcomes of S11.4.1v3:
- **Statically rejected:** a compile error. `<Obj a: "x">` reads "field 'a' of object 'Obj' expects int, but got string", and so does `null` in a required field. A required field with neither a value nor a default is E205, "object 'Obj' is missing required field 'a'" (`check_object_literal_fields`, `build_ast.cpp`).
- **Proven:** no check. An int still widens into a float field.
- **Deferred:** marked in the literal's `deferred_fields` and admitted at construction by `object_literal_admit_fields` (`lambda-data-runtime.cpp`) through `lambda_type_check`, before the value reaches the fill. `<Obj a: 9>` against `1 to 5` fails with "type check at field 'a' of Obj failed: expected 1 to 5, got int 9", and an exactly integral `3.0` becomes int 3 (S11.4.5). A spread's fields are always deferred. The interpreter admits before it allocates the object; the JIT calls `object_fill_checked`, which roots the values first.

S11.4.10 has a failure surface as an error value at the binding site. That is S7.7's contagion, which the spec's status table still lists as pending, and the JIT raises instead of it at every boundary. Typing the literal `T | error` instead would put E208 on every function that builds an object from an unproven value, which a call with a deferred parameter check does not. So a failure leaves as a failed declaration does today: the enclosing function returns the error, through the edge a failed map spread takes (`emit_return_if_item_error`, `interp_signal`), and its caller sees an error value (`make_grade(9) is error` is `true` in the fixture). An object that is constructed is therefore always admitted, and typed readers may trust its layout (D3.2.6).

Found on the way: a derived object type dropped its base fields' defaults (`resolver_object_copy_base`), so with `type Dog : Animal { … }`, `<Dog name: "Rex">` stored null in an inherited `legs: int = 4` on both tiers. The copy now keeps the default. Tracing the fixture also found [LR03-19](Lambda_Issue_Ledger.md#lr03-19)'s cause.

Fixtures: `object_field_admission.ls`, pinned in `kTune27TierParity`; `negative/runtime/object_field_{range,dynamic,spread}.ls` (`ExpectRejectedOnEveryTier`) and `negative/semantic/object_field_{static,missing}.ls`. All 974 goldens pass with `LAMBDA_TIER=jit` and with `interp`, and compiling every tracked `.ls` file raises the new diagnostics nowhere else.

<a id="lr03-22"></a>**LR03-22 · A constrained type's base was tested by its TypeId, or not at all (S11.2.1, S11.3.1v2) · FIXED 2026-09-25 (found 2026-09-25)**
`x is T` for a named `T = B that p`, and a `case B that p:` arm, compared the value's TypeId with the base's, on both tiers. A union, occurrence or array base carries the shared type tag, so it refused every value: with `type U = int | string that true`, `5 is U` was `false`, and `int?`, `int[]`, `any` and `number` bases did the same, as did a `float` base for `5`. A map, element or nominal base took any container of its kind, so `{a: "x"}` passed `{a: int} that true` and a plain map passed a `Point` base. The generic `fn_is`, reached by a first-class type value and a named match arm, returned `true` for a union, occurrence or array base without testing anything, and compared TypeIds otherwise.

*Fixed 2026-09-25.* The base admits as `x is <base>` does, through `fn_is` on the constrained type's own value. `fn_is` peels every `that` layer (`lambda_constrained_type_base`) and rebuilds the operand `x is <base>` receives (`is_operand_type_value`): the published singleton for a base-type word, the type itself for an extended kind, and a TypeType around a structural type. The JIT's `emit_constrained_type_test` and T0's `interp_constrained_type_matches` call it before the predicates, one helper per tier for `is` and match arms alike, where each tier had kept two copies of the TypeId check. The generic path stays base-only, as S11.4.6 has it. Fixture `constrained_type_base.ls` §1–§5 and §7, pinned in `kTune27TierParity`.

<a id="lr03-23"></a>**LR03-23 · A match arm naming a constrained type skipped its predicate, and an alias chain lost its inner ones (S11.2.1) · FIXED 2026-09-25 (found 2026-09-25)**
With `type Pos = int that ~ > 0`, `-5 is Pos` was `false` but `match -5 { case Pos: … }` took the arm: only an inline `case int that …` ran its predicate, and a named arm reached the generic `fn_is`. With `type U = int | string that false`, `case U:` matched `true`, `5` and `[1]`. `type P2 = Pos` lost the predicate under `is` as well, since only a declaration whose initializer was itself a constrained node resolved, and `type Small = Pos that ~ < 10` was always `false`, its base being a constrained type, whose TypeId is the type tag.

*Fixed 2026-09-25.* `ast_constrained_type` (`ast.hpp`) resolves the constrained type a position names statically, inline or through any alias, and `ast_constrained_type_predicates` lists the predicates on its alias chain, innermost first. `is` and both tiers' match arms use them, so `case Pos:` tests exactly as `x is Pos` does. T0's frame plan costs a named arm as it costs the inline form (`plan_constrained_type_need`); with one slot the frame overflowed. Fixture `constrained_type_base.ls` §6.

<a id="lr03-24"></a>**LR03-24 · T0 answered a constraint predicate outside its allow-list with `false`, where the JIT evaluated it (S11.4.11, S1.6) · FIXED 2026-09-26 (found 2026-09-25)**
T0 ran a `that` body only when `interp_predicate_supported` (`interp_plan.cpp`) admitted every node in it: literals, `~` and `~key`, a set of operators, and an allow-list of 22 pure system functions, within a 1,024-step budget. A predicate that read a `let` binding, called a user function, built a container or used a pipe failed without an error. The JIT compiled the same body in full. With `let lim = 3; type Big = int that ~ > lim`, `5 is Big` was `false` on T0 and `true` on the JIT. Under `auto`, `fn check(x) => x is Big` called twelve times answered `false` four times, then `true` once promoted. Separately, nothing checked a constraint body's colour: `type Bad = int that eff(~) > 3`, with `eff` a `pn`, compiled; the JIT ran the effect inside `5 is Bad`, and T0 answered `false`.

*Fixed 2026-09-26, by the user's ruling S11.4.11 (TE-20).* A predicate is an `fn` body over the scope it is written in, evaluated in full on every tier:
- **Colour.** The colour walk enters every constraint body, as `fn` context wherever it is written, a `pn` body included: each constrained type this module resolved, from the type list, and each object type's own constraints (`colour_walk_constrained_types`, `build_ast.cpp`). A statically `pn` callee is E224 and a dynamic one gets colour-guard bits.
- **T0.** `interp_eval_constrained_predicate` evaluates the body as the proviso's is. `EvalMode::PREDICATE`, its allow-list, its guard and the `LAMBDA_PREDICATE_FUEL` budget are gone (AI17v2).
- **Scope.** Once T0 evaluated such bodies, three places read the wrong names, and the JIT was wrong in the first. (1) A closure naming a local constrained type did not capture what the predicate reads: the JIT logged "undefined variable" and failed the test. Capture analysis now counts those reads (`capture_constraint_reads`). (2) An imported predicate must read its own module, which is [LR03-25](#lr03-25). (3) The names a predicate binds itself (a `for` variable, a group `let`) took slots of the declaring frame, out of the window in a closure's. They now take `BINDING_STORAGE_PREDICATE` slots of a window that each evaluation reserves in the evaluating frame (`plan_predicate_windows`, `interp_plan.cpp`), so a call that re-enters the predicate gets its own. Module-slab slots, tried first, broke on re-entry.

Fixtures: `constrained_type_predicate.ls`, pinned in `kTune27TierParity`, and `negative/semantic/predicate_calls_pn.ls` (`SemanticError_PredicateIsFnContext`). `make test-lambda-baseline` 5928/5929; every golden 981/982 with `LAMBDA_TIER=jit` and with `interp`; compiling all 2,027 tracked `.ls` files gives the same first errors before and after. The one failure in each run, `proc_markup_mutation`, fails identically on the binary from before the fix when run from a worktree.

*Residue:* the JIT read an imported predicate's names in the importer ([LR03-27](#lr03-27)); a predicate that named its own type crashed compilation on both tiers ([LR03-28](#lr03-28)). Both were fixed later the same day, by compiling each predicate as a function; that also retired the predicate windows described above.

<a id="lr03-25"></a>**LR03-25 · T0 read an imported constrained type's predicate constants from the importing module (S11.4.11, S1.6) · FIXED 2026-09-26 (found 2026-09-25)**
A predicate's AST belongs to the module that declares the type, but `eval_literal` resolved a literal's `const_index` against the running frame's module (`interp_const_at(f->module, …)`). With `pub type Named = string that ~ != "admin"` imported by a module whose first constant is `"bob"`, T0 answered `["admin" is Named, "bob" is Named]` as `[true, false]` and the JIT as `[false, true]`.

*Fixed 2026-09-26 with [LR03-24](#lr03-24).* `TypeConstrained::module` records the module that resolved each layer, and T0 switches the frame to that module's execution instance for the layer's evaluation (`interp_constrained_module`, `interp.cpp`), so constants, bindings and functions all resolve in the declaring module. A cache template maps to this Runtime's instance. A same-frame fault recovery point restores the switched module (`interp_eval_local_fault_operand`). Fixture `constrained_type_predicate.ls` §8, with the helper module `mod_constrained_types.ls`.

<a id="lr03-30"></a>**LR03-30 · A one-literal type alias was not a type value: `type T = 1` printed an address (S11.2.1, S10.1.1v2) · FIXED 2026-09-26 (found 2026-09-26, while implementing S11.1.7)**
With `type T = 1`, `[T, type(T), 1 is T, 2 is T]` was `[4404907056, int, false, false]` on both tiers, and `type F = 2.5` printed `2.122001866e-314`, a pointer's bits read as a number. `direct_finalize_type_alias` (`build_ast.cpp`) makes a literal alias a type value by wrapping its payload in a TypeType registered in the type list, but it did so only for string and symbol literals. A numeric alias kept the bare literal Type as its value type, so both tiers published that Type's pointer in the int or float lane. A symbol alias was a type value but admitted nothing (`'sym' is Y` was `false`): its literal payload is a `Symbol` (`parse_type_pattern.cpp`), and `literal_type_matches_item` (`lambda-eval.cpp`) read it as a `String`, whose chars sit at another offset. A literal union (`type P = 1 | 2`) was never affected, since the union is registered as a type and the validator matches its arms.

*Fixed 2026-09-26.* Every bare literal payload is wrapped (`definition->is_literal && definition->type_id != LMD_TYPE_TYPE`; the TypeType singletons carry `is_literal` too and are already type values), and `literal_type_matches_item` reads a text literal through the Item that `lambda_literal_contract_value` builds, so a Symbol payload is read as a Symbol. Found and fixed with it: `type(x)` of a type value built a fresh compact prefix tagged `type` rather than returning the shared `TYPE_TYPE`, so once S11.1.7 made the compact meta types compare by identity, `type(int) == type` was `false`; the fresh prefix was also unsafe for any unwrapper, which would read it as a TypeType. `fn_type` now returns `TYPE_TYPE`, as it returns `list` and `integer`. Fixture `test/lambda/type_literal_alias.ls`, on all three tiers (`kTune27TierParity`); baseline 5948/5948. Found on the way and still open: a bool literal in type position carries no value ([LR03-31](Lambda_Issue_Ledger.md#lr03-31)).

<a id="lr03-31"></a>**LR03-31 · A bool literal in type position carried no value: `false is (true | 1)` was `true` (S11.2.1) · FIXED 2026-09-26 (found 2026-09-26, while fixing LR03-30)**
The type-pattern parser gave `true` and `false` the shared marker `&LIT_BOOL`, which has no payload: the value emitter re-read the node's source span, which in a type pattern is the whole type slot's. `lambda_literal_contract_value` found no value in the marker, so `is` and the validator fell back to the bool tag and a bool literal type admitted both bools: with `type B = true`, `false is B` was `true`, and `false is (true | 1)` was `true` on both tiers. Inside a larger pattern the emitted value drifted too: `case [true, false]` read both slots from the span's first byte, `[`.

*Fixed 2026-09-26.* Two value-carrying literal types, `LIT_BOOL_TRUE` and `LIT_BOOL_FALSE` (`lambda-data.cpp`), are what `true` and `false` are in type position; `static_literal_item_from_type` and `lambda_literal_contract_value` read their value, `literal_type_matches_item` admits a non-numeric, non-text literal by its own value, and the JIT emits a bool literal from its type rather than its span. They are also the runtime literal types of bool operands (S10.1.1v2). A rejected bool or float literal contract now names its value (`expected true, got bool false`). Found on the way: [LR07-38](#lr07-38). Fixtures `test/lambda/type_literal_alias.ls` (LR03-31 section, three tiers) and `negative/runtime/literal_admission_bool_declaration.ls`.

<a id="lr03-28"></a>**LR03-28 · A constrained type whose predicate names the type itself crashed compilation (S11.4.11) · FIXED 2026-09-26 (found 2026-09-26, while fixing LR03-24)**
`type Rec = int that (~ <= 0 or (~ - 1) is Rec)` segfaulted on both tiers before anything ran. Both expanded a named constrained type's predicate where `is` named it: T0's frame plan sized the scratch of `is Rec` by the predicate (`plan_constrained_type_need` into `plan_need`) and the JIT inlined it (`emit_constrained_type_test`), so a self-reference recursed without bound. No bound would have helped: a recursion whose depth depends on the value cannot fit one frame's planned scratch or one inline expansion.

*Fixed 2026-09-26, by the mechanism TE-20 named (S11.4.11 unchanged; AI17v3).* A `that` body is an `fn` body, so it is compiled as one. The resolver opens a function of its own around the body, `that(~)` (`resolver_predicate_begin`, `build_ast.cpp`), whose one parameter is the candidate, whose locals are the names the body binds and whose captures are the outer locals it reads. The function sits in the tree under its constrained type, so both tiers plan, index and emit it as any function, and it binds its parameter as `~` at entry (`AstFuncNode::is_that_predicate`). Its return contract admits an error, since an error answer fails the test; the implicit `fn` contract would reject a body whose value may be an error, such as `int that checked(~)`, as E208 (fixture `constrained_type_predicate.ls` §11). `is` and a match arm call it once per layer: T0 through a closure made where the type is named (`interp_eval_constrained_predicate`), the JIT through the function's boxed `_b` entry with its captures read there (`emit_constrained_predicate_call`). Each level is an activation of its own, so the recursion is as deep as an `fn`'s: 5,000 levels on both tiers, and deeper overflows with E308 as a recursive `fn` does. Gone with the inline expansion: T0's predicate windows (`BINDING_STORAGE_PREDICATE`, `plan_predicate_windows`, `InterpFrame::predicate_window`) and the frame-module switch that fault recovery had to undo. A satellite does not carry the predicate function, so a function with a constrained `is` stays on T0, as one with a constrained match arm already did. The candidate now starts a fresh context, `~~` null and the root the candidate itself, where the inline body had read the enclosing context of the `is` site; no fixture or package read either. Fixture `constrained_type_recursive.ls` (self, alias chain and match arm, a local type from a closure, recursion over structure, an error at one level, `~~`), identical on all three tiers.

<a id="lr03-27"></a>**LR03-27 · The JIT read an imported constrained type's predicate names in the importer (S11.4.11, S1.6) · FIXED 2026-09-26 (found 2026-09-26, while fixing LR03-24)**
Both tiers expanded a predicate where `is` named its type. T0 switched to the declaring module for the evaluation; the JIT emitted the body into the importer's MIR function, where the declaring module's names are not bound. With `let lim = 3` and `pub type Eq = int that ~ == lim` imported, `3 is Eq` was `true` on T0 and `false` on the JIT, which logged "mir: undefined variable 'lim'". A predicate calling one of the declaring module's private functions (`pub type Dbl = int that dbl(~) > 6`) left the importer unlinkable ("Import of undefined item _dbl_…").

*Fixed 2026-09-26 with [LR03-28](#lr03-28).* The predicate is compiled once, as a function of its declaring module, and the importer calls it. The declaring module exports each layer's `_that_b_<offset>` entry beside its `pub` functions, whatever the type's own visibility (`register_module_pub_fns`), and the importer reaches it under that module's unit prefix through the `fn_call_boxed_1_into` trampoline, as an imported function call does (`emit_constrained_predicate_call`, `transpile-mir.cpp`; `runtime_script_instance` maps a cached template to the running instance for both tiers). The body reads its own module's names and links its private functions on the JIT as on T0. Fixture `constrained_type_predicate.ls` §10 with the helper `mod_constrained_types.ls`; on the tree before the fix the JIT did not link the fixture.

## 4. Numbers, decimal & datetime (LR_04)

<a id="lr04-1"></a>**LR04-1 · "Unlimited" decimal is a 200-digit cap · RESOLVED 2026-08-28**
Literal, string, and arena ingestion now parse coefficients exactly, and
decimal `+`, `-`, and `*` use a local maximum-precision context so exact
results can grow beyond 200 digits. `g_unlimited_ctx.prec = 200`
(`lambda/core/lambda-decimal.cpp:46`) remains only the extended context for
documented inexact operations such as division and power; it is no longer used
to cap source literals or exact arithmetic under **S4.6.1**/S4.6.2. Regression:
`decimal_tiers` covers a 350-digit literal and a 53-digit fixed-tier product;
the decimal baseline passes.


<a id="lr04-3"></a>**LR04-3 · Trapping `mpd_get_ssize` can SIGFPE · RESOLVED 2026-09-05**
Decimal narrowing now uses `mpd_qget_ssize` and maps an invalid conversion to
the existing `INT64_ERROR` sentinel. Thus an out-of-range decimal cannot enter
libmpdec's trapping path; the native boundary remains total as required by
**S4.1.2**. Regression:
`LambdaDecimal.QuietInt64ExtractionRejectsOverflowAndInvalidComparison` covers
`9223372036854775808` without a signal.


<a id="lr04-4"></a>**LR04-4 · `decimal_cmp` swallows conversion failure as equality · RESOLVED 2026-09-05**
`decimal_cmp_items` now returns success separately from its order result. A
failed operand conversion is invalid ordering (and false for JS strict
equality), never equality; validator pattern matching also rejects it. This
preserves the poison/non-equality rule in **S4.2.3**. Regression:
`LambdaDecimal.QuietInt64ExtractionRejectsOverflowAndInvalidComparison` covers
an invalid Decimal operand.


## 5. Strings, symbols & vectors (LR_05)

<a id="lr05-1"></a>**LR05-1 · `ndim` cap of 32 is unchecked in the helpers · RESOLVED (D1.9)**
`LAMBDA_ARRAY_NUM_MAX_NDIM` is the single rank cap. Construction, GC promotion,
and equality use it; `lambda/runtime/lambda-vector.cpp` now validates descriptor
rank at the shared shape/stride decode boundary before writing a caller's
fixed-rank buffer. Every vector, structural, reduction, mask, and image caller
converts a rejected descriptor to `ItemError`; direct native reduction returns
`NaN` after logging because its ABI is `double`. The regression injects an
out-of-range descriptor and verifies shape and matrix operations fail without
decoding it. This implements D1.9's malformed-input fail-closed rule.


<a id="lr05-2"></a>**LR05-2 · Not full UCA collation · RESOLVED (not a defect; ruled by S6.2.2)**
The former expectation was wrong: Lambda's normative total order is bytewise
UTF-8, with no locale collation or accent ordering. The utf8proc casefold path
is used only for markup tag/attribute matching. The stale comment at
`lambda/core/utf_string.cpp:57` should be corrected, but implementing UCA would
contradict **S6.2.2** rather than fix Lambda's operators.


<a id="lr05-5"></a>**LR05-5 · `fn_label` bypasses the runtime allocator with raw `malloc`/`free` · RESOLVED 2026-08-28**
The flood-fill stack was allocated with raw `malloc` and released with `free`,
so the operation bypassed the checked `memtrack` allocation contract and its
failure-injection path. The success path was leak-free, but the temporary
workspace was outside the runtime's ownership and failure accounting.

The stack now uses the existing `mem_alloc`/`mem_free` pair with
`MEM_CAT_TEMP`. This implements **D4.2.1v3** and lets allocation failure return
through the existing `ItemError` path, as required by **D4.2.2v2**. No new
data structure or design ruling was added.

Regression: `RuntimeShapeTransition.LabelStackAllocationFailureReturnsError`
arms `memtrack_fault_inject(0)` and verifies that `fn_label` reports the
workspace allocation failure. The complete representation suite passes 29/29;
`make test-lambda-baseline` passes 3977/3977.


<a id="lr05-9"></a>**LR05-9 · `++` on numeric arrays of different element types reinterprets bits · FIXED 2026-09-23**
`[1,2] ++ [3.5]` is `[1, 2, inf]`, `[1.5] ++ [2]` is `[1.5, 1e-323]`, and
`[1i8,2i8] ++ [300]` is `[1, 2, 44]`, on both tiers. `fn_join`'s same-type
shortcut (`lambda-eval.cpp:527`) tests only that both sides are
`LMD_TYPE_ARRAY_NUM`, then builds the result with the **left** element type
and copies the right payload's bytes under it. Contradicts **S5.3.1** (a
numeric array is representation only); the `++` operand table itself has no
ruling.
*Fixed 2026-09-23 (P4):* the shortcut now also requires both lanes to have the
same element type; a mixed pair falls through to the generic item path, so
`[1, 2] ++ [3.5]` is `[1, 2, 3.5]` and `[1i8, 2i8] ++ [300]` is `[1, 2, 300]`.
Record: [plan §13](<impl/Lambda_List_Fixes (done).md>).

<a id="lr05-10"></a>**LR05-10 · Sequence kind (list vs array) is not preserved anywhere (S2.5.6, S2.5.7, D2.6.5v3) · OPEN (found 2026-09-22)**
A 562-probe survey (`temp/spec_survey/listarray/`, both tiers) found the
result kind of every sequence operation decided by how the input was built,
not by its kind. Root causes: two list flags — `is_content` set by `list_end`
and by take/drop/slice/zip/reverse/`split`/`find`/the vector ops,
`is_spreadable` set by `array_spreadable` (`for`), `item_spread`, the child
queries, and `unique` (`lambda/lambda.h:1015`); the consumers disagree
(`array_push`/`list_push` splice only `is_content`, `array_push_spread` both,
`array_push_spread_all` every array — `collection_runtime.cpp:190`, `:385`,
`:486`; `lambda-data-runtime.cpp:1815`, `:1824`); array literals decide
spreading from **syntax** — a for-style list spreads only when a sibling is
written as `for` or `*` (`transpile-mir.cpp:20509`, `interp.cpp:2239`) — so
`let r = for (x in [1,2]) x; [r, 9]` is `[[1,2],9]`; and each function picks
its own result flag: `sort` → array (`lambda-vector.cpp:101`), `reverse`
keys on `!is_spreadable` (`:2409`), `unique` copies `is_spreadable` (`:2696`),
take/drop/slice/`[i to j]` make a **list** of a plain array or range
(`:2761`–`:2911`), `zip` always a list (`:4963`), `fn_join` (`++`) always an
array (`lambda-eval.cpp:441`), pipes always an array
(`transpile-mir.cpp:30053`, `interp.cpp:2896`), `order by` an array
(`fn_sort_by_keys` `:2494`, but an empty ordered result stays a list), typed
`int[]` admission rebuilds the value and drops the flag
(`lambda-eval.cpp:9918`), `x is list` is hard-coded false (`:2198`).
For-expressions never collapse (empty is a hidden non-null empty container,
one item is `[v]`); `(for …)[int]` and `(1 to 3)[int]` return `[]`
(`child_query_collect` `:3773`). Design: one kind flag, one result-kind
helper (`array_transform_copy_cert` is the nearest hook, `lambda-vector.cpp:165`),
value-decided spreading, collapse at every list-producing site.
Implementation plan: [`vibe/impl/Lambda_List_Fixes (done).md`](<impl/Lambda_List_Fixes (done).md>)
(also covers LR05-11, LR05-12, LR12-28, LR03-12, LR05-9 and the S2.6 content gaps).
*Partially resolved 2026-09-22 (P1):* one kind bit, value-decided spreading,
position-decided finish (void vs null), for-expression collapse, no list
normalization, `()`, `type()`/`is list`; the per-function result kinds (P2)
remain. Making `array_push` splice every list (it had spliced only
`is_content` lists) exposed the positional sites that had relied on for-lists
not splicing: a for-expression passed as one argument was spliced into the
rest list (`count_args(for (x in [1, 2]) x)` was 2, the LR09-9 invariant).
Every positional site — rest and argument lists (JIT, dynamic-call adapter,
JS timer packs, concurrency args), group/join/order keys and rows, path keys,
query matches, clones, set operators, element-copying transforms, `zip`,
index-addressed vector results, and the hosted-Python list operations — now
uses the verbatim append `array_push_verbatim` (D2.6.5v3).
*P2 landed 2026-09-22:* every sequence operation keeps its input's kind
through one finish (`seq_finish_kind`, `seq_operands_are_lists`,
`lambda-data-runtime.cpp`): sort/reverse/unique/take/drop/slice, element-wise
arithmetic, masks, and unary functions, `zip`, the set operators, `fill`, the
mapping pipe and `that` (`pipe_end`, both tiers), and for-expression windows
(`for_window`, in place ahead of the one collapse); `split`/`find` build
arrays; typed admission keeps the bit. What remains of the umbrella is the
slot-store image ([LR12-28](#lr12-28), P3) and text ([LR05-11](#lr05-11), P4).
Record: [plan §11](<impl/Lambda_List_Fixes (done).md>).
*P3 landed 2026-09-23:* slot stores take the array image
([LR12-28](#lr12-28) fixed) and insertions splice a list (`a[i] = list`,
`push`, typed arrays included).
*P4 landed 2026-09-23:* text walks as a sequence and rebuilds its own kind,
`++` follows S10.6.1's table, and `*` no longer marks its operand
([LR05-11](#lr05-11), [LR05-12](#lr05-12), [LR05-9](#lr05-9) fixed). The
umbrella's remaining part is the type families of P5
([LR03-12](#lr03-12)).

<a id="lr05-11"></a>**LR05-11 · Text sequence operations, `that`/pipe on scalars, and `++` do not follow S2.5.8, S10.1.2v2, S10.1.5v2, S10.6.1 · FIXED 2026-09-23**
`reverse`/`sort`/`unique` return a string unchanged (`lambda-vector.cpp:2396`,
`:2429`, "strings are singular"); `in` on strings is a substring test
(`lambda-eval.cpp:3814`, `strstr`) where S8.1.1/S2.5.8 rule character
membership; `that` and the mapping pipe over a string yield characters as a
spreading list rather than a string; `"s"[-1]` is `""` (S7.2.1 says `null`).
`5 that ~ > 3` is `[5]` (ruled `5`); `[1,2] that ~ > 9` is `null` (ruled
`[]`). `++`: `"a" ++ [1]` stringifies to `"a[1]"` and `[1,2] ++ 3` is an
error, where S10.6.1 rules `["a", 1]` and `[1,2,3]`; `null` is already the
identity. `keys`/`values`/`names` are not built in (S8.4.1v2 — deliberate).
*Partially resolved 2026-09-22 (P2):* `that` and the mapping pipe follow
S10.1.2v2/S10.1.5v2 for list, array, range, map, element, scalar, and null
sources (`5 that ~ > 3` is `5`, `[1,2] that ~ > 9` is `[]`, a list filtered
to nothing is `null`). The text operations, `in`, `"s"[-1]`, and `++` remain
(P4).
*Fixed 2026-09-23 (P4):* a string or symbol is walked by code point and a
binary by byte, so `reverse`/`sort`/`unique`/`take`/`drop`/`slice` rebuild the
source's kind from its characters; a filter over text always gives that kind
(`""` when empty) and a mapping gives it only when every result item belongs to
it (`"abc" |> upper(~)` is `"ABC"`, `"abc" |> ord(~)` is `[97, 98, 99]`) — the
checker types those results accordingly, without which the JIT folded
`("abc" that p) == ""` to false while the interpreter answered true. `in` is
code-point membership and `contains` keeps the substring test; an out-of-range
text subscript is `null` (S7.2.1). `++` follows S10.6.1's table: a sequence
operand decides the result before any text arm (`[1] ++ "ab"` is `[1, "ab"]`,
`[1, 2] ++ 3` is `[1, 2, 3]`), kind by S2.5.7, `null` the identity, maps and
elements rejected. `keys`/`values`/`names` stay out of scope (S8.4.1v2 —
deliberate). Record: [plan §13](<impl/Lambda_List_Fixes (done).md>).

<a id="lr05-12"></a>**LR05-12 · `*` spread mutates its operand and skips ranges (S12.3.5v2) · FIXED 2026-09-23**
`item_spread` (`lambda-data-runtime.cpp:2397`) sets the spread flag on its
operand, so `[*a, 3]` permanently turns `a` into a list; on the JIT the operand
may be a pooled constant literal (`transpile-mir.cpp:8148`), so
`fn g() => [1, "y"]` returns a list on every later call — JIT gives 3 items
where the interpreter gives 2 (repro
`temp/spec_survey/listarray/t/repro_spread_const.ls`). `[*(1 to 3), 9]` does
not spread the range; `[*null, 1]` keeps the null where `*null` splices
nothing. S1.6 violation on the JIT half.
*Fixed 2026-09-23 (P4):* `item_spread` is deleted. `*x` builds the list of x's
items — a sequence's items with a range materialized, nothing for null, any
other value (text included) as one item — and finishes by position like every
other list producer, so it splices where it lands, collapses at one item or
none, and `[*xs]` packages any value as an array. The operand is untouched.
Record: [plan §13](<impl/Lambda_List_Fixes (done).md>).

<a id="lr05-13"></a>**LR05-13 · Query results are lists of any length (S2.5.5v2; kind unruled) · FIXED 2026-09-23**
`fn_query` and `fn_child_query` (`lambda-eval.cpp:3728`, `:3822`) return a
container with the list bit set whatever it holds, so `e[element]` with one
match is a one-item list (`len` 1, spreads as an item) and with none an empty
list, where S2.5.5v2 says a list has at least two items. No ruling names the
kind of `e[T]` / `e?T`: read as a selection (S2.5.7) the result follows its
source — an array for an element, map, array, or range, a collapsing list for
a list — but query results placed in element content would then land as one
array item instead of splicing. Needs a ruling before the query functions
change. Repro `temp/p2/probe_query.ls`. Both tiers.
*Ruled and fixed 2026-09-23:* S8.2.4 — a type subscript is an accessor, so
`e[T]` and `e?T` yield the run `T*`: `null`, the match itself, or a list.
`fn_query`/`fn_child_query` finish through the list collapse and the checker
types the result open. The content-splicing worry above is answered by the
run: two or more matches splice by their kind bit, one lands as itself, none
as `null`, and `*e?T` packages any of them. Argument in
[Expr_Query §4.1](Lambda_Expr_Query.md). Fixture `test/lambda/query_kind.ls`.

<a id="lr05-14"></a>**LR05-14 · `str_rfind_byte` can return a position past the real last match · FIXED 2026-09-24**
`str_rfind_byte` (`lib/str.c:266`) scans backwards eight bytes at a time and returns the highest byte flagged by `_swar_has_byte` (`:279`).
- **Cause:** through borrow propagation, that test can also flag the byte just above a real match when that byte equals `c ^ 0x01`. The caveat is already noted in `str_count_byte` (`:404`). So a backward search can return the position just after the true last match.
- **Forward search is unaffected:** `str_find_byte` takes the lowest flagged byte, which is always a real match.
- **Callers:** every one-byte reverse search reaches it through `str_rfind` (`:321`):
  - Lambda `last_index_of` (`lambda/runtime/lambda-eval.cpp:6524`);
  - LambdaJS `String.prototype.lastIndexOf` (`js_string_find_position` in `lambda/js/js_runtime.cpp`);
  - Node-compatible `Buffer.lastIndexOf` (`lambda/module/node_core/node_buffer.cpp:1584`).
- **Reproduced 2026-09-24 on both tiers:** `last_index_of("dir/.hidden", "/")` is 4 and `last_index_of("abcdefgh", "b")` is 2, where 3 and 1 are right. LambdaJS `lastIndexOf` gives the same 4 and 2; Node gives 3 and 1.
- **Why tests missed it:** in `test/lib/test_str_gtest.cpp:218`–`221`, every match checked falls in the scalar tail.
- **Fix:** use an exact zero-byte mask for the backward scan, `~(((x & 0x7F…) + 0x7F…) | x | 0x7F…)`, and add both reproducers as tests.

*Fixed 2026-09-24 (string tuning P0):* the backward scan reads the highest flag, so `str_rfind_byte` now uses `_swar_has_byte_exact` (`lib/str.c`); each byte's sum stays below 0x100 and cannot flag a neighbour. Forward scans keep `_swar_has_byte`. Tests: `RFindByteSwarNeighbour` and `RFindByteMatchesNaive` (2,000 random buffers of every length 0–39 over an alphabet built around `c` and `c ^ 1`) in `test/lib/test_str_gtest.cpp`; golden cases 65–66 in `test/lambda/string_funcs.ls`; two lines in `test/js/string_methods.js`. Record: [string tuning P0](<impl/Lambda_Impl_String_Func_Tuning.md>).

<a id="lr05-15"></a>**LR05-15 · Indexing a non-ASCII symbol splits a character · FIXED 2026-09-24**
S2.5.8 has indexing and every sequence operation see a symbol as its code points.
- **Cause:** `item_at` (`lambda/runtime/lambda-data-runtime.cpp`, the `LMD_TYPE_STRING`/`LMD_TYPE_SYMBOL` case) starts from `is_ascii = true` and corrects it only for strings, which carry the flag. A symbol therefore always takes the byte-indexed fast path.
- **Reproduced 2026-09-24 on both tiers:** `'café'[3]` is the one-byte symbol `'\xC3'`, where `'é'` is right. By contrast, `len('café')` is 4 and `"café"[3]` is `"é"`.
- **Knock-on:** `reverse`, `sort` and the other text sequence operations read characters through `item_at` (via `vector_text_items`, `lambda/runtime/lambda-vector.cpp:304`). So `reverse('café')` is `'\xC3fac'` and `sort('bé')` is `'b\xC3'`: the byte `0xA9` is lost, and neither result is valid UTF-8.
- **Fix:** the UTF-8 path below the fast path already handles symbols correctly. Establish ASCII-ness for a symbol before choosing the path, either with `str_is_ascii` over its bytes or with an `is_ascii` bit on `Symbol`.

*Fixed 2026-09-24 (string tuning P0):* `item_at` decides a symbol's ASCII-ness from its bytes (`str_is_ascii`) instead of assuming it, so a non-ASCII symbol takes the UTF-8 path. On both tiers `'café'[3]` is `'é'`, `reverse('café')` is `'éfac'` and `sort('bé')` is `'bé'`. Golden case in the S2.5.8 fixture `test/lambda/text_sequence.ls`. Record: [string tuning P0](<impl/Lambda_Impl_String_Func_Tuning.md>).

## 6. C transpiler — legacy C2MIR (LR_06)

**All nine issues are archived below as [LR06-R1…R9](#lr06-r1r9).** The C2MIR backend no longer exists in the tree.

## 7. MIR Direct transpiler & JIT (LR_07)

<a id="lr07-1"></a>**LR07-1 · Numeric semantic result and physical representation were coupled · RESOLVED 2026-09-14**
Every Lambda AST family now enters `transpile_expr_value()` and publishes a
`MirValue` carrying the full `Type*` contract and its actual `ValueRep`. The
legacy raw-register expression shim is absent. Consumers request their needed
carrier through `em_require_rep()` and read a lowered value's carrier only
through `mir_value_carrier_type(MirValue)`; they do not recreate it from the
AST or from `MIR_reg_type()`.

The final audit replaced post-lowering carrier re-derivations in machine-count,
loop binding/filter/condition, multidimensional-index, bitwise, pipe, path, declarator,
and edit-index lowering. `mir_expr_carrier_type()` remains a pre-lowering
planner only, using AST/lowering facts to select an emission path; it never
observes an emitted MIR register. This enforces the four-authority split in
**D2.4.1–D2.4.3**: semantics in `Type*`, planned representation in lowering,
emitted representation/provenance in `MirValue`, and physical register class
only in named physical helpers.

Regression evidence: `ValueRepresentationTest` covers canonical contracts and
fail-closed carrier transitions; `index_value_rep.ls`,
`bitwise_lane_preservation.ls`, `proc_assignment_error_carrier.ls`, and
`proc_var_type_widen.ls` pass under eager JIT. The compiler audit finds no
`transpile_expr_reg_legacy` or `legacy_expr_value` symbol and no semantic
`MIR_reg_type()` query in Lambda expression lowering.

<a id="lr07-4"></a>**LR07-4 · Type widening is truncate-or-box · RESOLVED 2026-09-14**
`transpile_assign_stam` now preserves both the declared destination contract
and the producer's physical carrier. A FLOAT assigned to a declared `int`
crosses `emit_checked_boundary` before any lane conversion, so a non-integral
value returns E201 instead of reaching an incompatible move or a lossy cast.
An inferred `var` that is written with a wider scalar in a control region is
pre-widened at its declaration, ensuring every path observes one boxed carrier.

The remaining boxed-to-native assignment arm now tests `ItemError` before
unboxing. An `any` producer that yields an error therefore exits on the error
channel; it can never be decoded as `0`, `0.0`, or `false` in a native local.
This implements the destination-admission rule in **S7.8.1** and keeps the
carrier/value distinction required by **D2.4.1–D2.4.3**. The old division-zero
example was stale: **S4.5.1–S4.5.3** define computed zero divisors as numeric
poison, not `ItemError`.

`MIR_D2I` remains only in the guarded float-index normalization path; an
out-of-domain magnitude maps to a non-indexable value that every bounds path
rejects, so it is not an assignment-widening conversion.

Regression: `test/lambda/proc/proc_assignment_error_carrier.ls` covers an
`any`-returned `ItemError` assigned to an inferred native `var value = 7`, an inferred loop
widening to `1.5`, and rejection of `1.5` at a declared `int` assignment.
It produces `[true, 1.5, true]` under both `LAMBDA_TIER=jit` and the default
tier. `proc_var_type_widen.ls` also passes under eager JIT.

<a id="lr07-5"></a>**LR07-5 · AST “effective type” carrier guessing · RESOLVED 2026-09-14**
The Lambda MIR Direct `get_effective_type` helper and its cited source anchor
are gone; the former line now belongs to unrelated import-prototype emission.
Every emitted expression instead publishes a `MirValue` with its semantic
contract and actual `ValueRep`, and consumers convert only through
`em_require_rep()` under **D2.4.1–D2.4.3**. This removes the original failure
mode: interpreting a boxed `Item` as a native scalar merely because the AST
retained a precise type.

`mir_expr_carrier_type` remains solely a pre-lowering planning oracle. Its
unproven, fallible, control-flow, and mixed-representation cases fail closed to
the boxed Item carrier, while emitted values preserve the exact producer fact.
The audit found no reproducible non-identifier stale-type boxing failure. Any
future planner coverage gap belongs to the broader LR07-14 representation audit,
not this retired helper defect.

<a id="lr07-10"></a>**LR07-10 · Out-of-bounds index semantics differed by type · RESOLVED 2026-09-14**
`MIR_INDEX_OOB_FLOAT_ZERO` is removed. `emit_checked_index_load` now emits the
reserved nullable-float lane for every native-double out-of-bounds path, even
if a future policy accidentally omits the explicit float-null classification.
The Item boundary translates that lane to `null`; `0.0` is no longer an OOB
fallback. This implements total invalid reads under **S7.1.1v3** while retaining
the semantic/carrier split of **D2.4.1–D2.4.3**.

Regression: `proc_nullable_float_lane_read.ls` and
`proc_nullable_lane_comparisons.ls` pass under eager JIT, alongside the
general OOB-read coverage in `oob_read_null.ls`.

<a id="lr07-r7"></a>**LR07-R7 · Precise-root classification trusted dishonest static types · RESOLVED 2026-09-13**
The producer and consumer halves now share the value-side TypeId
classification: `mir_value_type_id` handles the overloaded `LMD_TYPE_TYPE`,
and `lambda_gc_value_class` fails closed when the semantic contract is
unresolved. This preserves the precise RootFrame/Rooted ownership required by
**D2.4.1–D2.4.3** and **D5.4.2** without restoring conservative native-stack
scanning.

`LAMBDA_ROOT_WITNESS=2` covers named locals and expression temporaries across
may-GC calls. The current whole-corpus sweep reports zero violations, while the
negative control reproduces the pre-fix violations; focused fixtures also pass
with forced GC and freed-object poisoning. The collector-side duplicate is
archived as [LR08-R3](#lr08-r3).

<a id="lr07-15"></a>**LR07-15 · Object methods read the receiver as zero on the eager JIT tier · RESOLVED 2026-09-03**
An SI3v2 tier-divergence: the same script yields different results under
`LAMBDA_TIER=jit` than under `interp`/`auto`. Implicit receiver-field reads
inside an object method body evaluate to 0 on the eager whole-module MIR path.
Probe (commit `ababcb674`, **before** any 2026-09-03 change — verified by
stashing): `test/lambda/object.ls` with `type Counter { value: int, fn
double() => value * 2, fn add(n: int) => value + n }` and `let c = <Counter
value: 5>` gives `c.double()` = **10** and `c.add(3)` = **8** on `interp`, but
**0** and **3** on `jit` — the `3` shows `value` itself reading 0, not the
multiply failing.

Companion symptom, same root: a `pn` method's mutation is lost. `pn bump() {
value = value + 1 }` on `<Counter value: 5>` leaves `c.value` = 5 under `jit`
and 6 under `interp` (`temp/probe_pn_call.ls`).

Why it was not caught: the baseline runs the default AUTO selector, which routes
these scripts to T0, so `object.ls` passed at 4079/4079 while the JIT path was
wrong. Any corpus tier-parity sweep must set `LAMBDA_TIER` explicitly.

**Root cause — one missing back-pointer.** `binding_node_set_entry`
(`build_ast.cpp:2180`) wrote the `NameEntry` back onto its declaring node for
`AST_NODE_VARIABLE_DECLARATOR` and `AST_NODE_PARAM` only. An object type's field
scope-helper is an `AST_NODE_KEY_EXPR` (`direct_object_add_field` and the
base-inheritance copy at `:6910`/`:6986`), so `field_ref->entry` stayed NULL and
the `shape->binding = field_ref->entry` beside it stored NULL — even though
`ShapeEntry::binding`'s own comment (`lambda-data.hpp:316`) says object-method
field lowering depends on it.

That NULL was invisible to T0, which resolves an object-field read by *name*
against `method_self` (`interp_read_binding`), and fatal to MIR, which matches
variables by *binding identity, not spelling* (`mir_var_for_ident`,
`transpile-mir.cpp:2209`). The method prologue loaded each field from `self` and
called `publish_var_binding(mt, field_name, se->binding)` with NULL, so the
locals were registered under no binding; every implicit read then fell through
`transpile_ident_value` to its "undefined variable" arm. The write half failed
the same way: the epilogue's write-back (`:25701`) looks the local up with
`mir_var_for_binding(field->binding)` and found nothing, so a `pn` method's
mutation was dropped.

**Fix:** admit `AST_NODE_KEY_EXPR` in `binding_node_set_entry`. One arm, both
halves — reads and the `pn` write-back — on both tiers. Fixtures:
`test/lambda/object_method_receiver.ls` (read, inherited fields, float
unboxing) and `test/lambda/proc/object_method_write.ls` (write-back); both are
byte-identical under `LAMBDA_TIER=interp` and `=jit`, as is `object.ls`.
Baseline 4082/4082.

*Measurement note:* the tier selector is the `LAMBDA_TIER` environment
variable. `./lambda.exe jit run f.ls` is **not** tier selection — `jit` consumes
`run` as the script name and the file never executes (`nodes=0`, prints
`null`). Two wrong conclusions in this investigation came from that form.


<a id="lr09-8"></a>**LR09-8 · `len(element)` violated the S8.3.1 length law · RESOLVED 2026-09-03 (USER ruling)**

S8.3.1v2 states the law — `len(x)` is the number of iterations `for (i in x)` performs — and gives `len(<e a:1, b:2, "t">)` = **3** as its own example. The element arm of `fn_len` returned the child count alone, so that expression answered **1** while `[for (x in e) x]` yielded three members. Ruled closed by the user: element length is attribute count plus content-item count, for structural and nominal elements alike.

Fixed in the ELEMENT arm of `fn_len` and in `fn_len_e`, the JIT's specialization for a statically-element argument — both now `map_attr_count((Map*)elmt) + elmt->length`, so the two tiers cannot drift apart. `len_iter_law.ls` no longer records a divergence; it pins the law. The verified walk order is attribute VALUES first, then content items: `for (x in <div id:"a", cls:"b", <p "x"> <q "y">>)` yields `["a", "b", <p "x">, <q "y">]` and `len` is 4.

The fallout is real and is tracked separately as [LR09-9](#lr09-9): the change moves 44 corpus goldens, of which only 6 are the bare length number.


<a id="lr09-9"></a>**LR09-9 · The `len(e)`-bound child walk, and the `content(e)` accessor that replaces it · RESOLVED 2026-09-04 (USER ruling)**

Closing LR09-8 removed the accident that made `for (i in 0 to len(e) - 1) e[i]` a correct child walk. An IntKey subscript reaches only children (S8.2.1v4) while `len` now also counts attributes, so the loop overran and `e[i]` read `null` past the last child. It was **not** merely inefficient: the phantom nulls are indistinguishable from real children, and three shapes of silent corruption showed up — a schema validator reporting each null as *"Scalar content is not permitted directly under \<graph>"*, a rebuilt content list gaining trailing nulls, and a `group by` aggregate turning `total: 15` into `null`.

**Ruled: `content(e)`**, a system function returning the element's content sequence. `len(content(e))` is the child count and `content(e)[i]` the child index walk, so the arithmetic disappears rather than being re-spelled. Rejected alternatives: `e.content` (dot resolves the key domain first under S8.2.2v2, so it would silently return a user attribute named `content` — and `content` is a live child/attr name across the graph schema) and `size(e)` (a second length-ish name, reintroducing exactly the confusion LR09-8 removed, and no way to index).

**It is a read-only VIEW, not a copy** (USER): the returned Array shadow-copies the element's content meta fields — items pointer and length — and never copies the item slots, so a per-node walk stays allocation-free. Borrowing reuses the container view contract ArrayNum already had: `is_view` set, `is_mutable_view` clear, and `extra` holding an `ArrayNumShape` whose `base` is the owning element. Write-through is deliberately deferred; `fn_array_set` refuses a read-only view.

**Three defects the view surfaced, each worth remembering.** (1) The view must be **rooted across the descriptor allocation** — that allocation can collect, and with conservative stack scanning retired a view held only in the C frame is invisible, so it was reclaimed mid-construction and its slot handed to the next array; `content(e)` then returned an unrelated later array. (2) The descriptor is nursery data and must be **promoted** in the compact pass, or `extra` dangles after the zone reset. (3) An element's items buffer **moves**, so the view is excluded from owned-data compaction and instead rebound from its base — forcing the base's promotion first, since the sweep order is arbitrary. All three only appear under `LAMBDA_GC_FORCE_EVERY`.

**Four runtime consumers were real bugs, not migrations** — every place that pairs a count with an IntKey read, since an IntKey reaches content only (S8.2.1v4) while `len` now also counts attributes. Found by test failure: the **mapping pipe**, which sized its traversal with `fn_len`, so `g |> ~["amount"]` gained a null row per attribute and poisoned `sum` — its own comment already said elements pipe over content. Found afterwards by audit, with NO test covering them: **`last`** (`e[last]` read `null` instead of the final child, on both tiers) and the **set operators** `fn_union`/`fn_intersect`/`fn_exclude` plus the mixed-type array concat (`e | f` leaked a trailing `null`).

All five now call one shared `extern "C" int64_t fn_seq_count(Item)` — the count of positions a positional traversal visits, which is content length for an element and `fn_len` otherwise — so the rule has exactly one definition and cannot drift between the tiers. `slice`/`drop`/`take_last` need no change: `vector_length` returns -1 for an element, so those refused elements before this ruling and still do.

*The audit is the lesson.* The pipe surfaced as a golden diff; `last` and the set operators did not, because no fixture exercised them on an element. Grepping for `fn_len` callers that feed an index was what found them, and that is the check to repeat if the length law ever moves again.

That gap is now closed by `test/lambda/element_content_axes.ls`, which pins both axes together — `len` as attributes-plus-content equal to the iteration count, `content()` as the child sequence, an IntKey reading `null` past the last child, `last`, the mapping pipe, the three set operators, the degenerate shapes (bare, attributes-only, content-only), a nominal element, and a `group by` element where the key attribute is counted by `len` but not by `len(content(g))`. It was verified to FAIL, not merely to pass: reverting `fn_seq_count` to `fn_len` makes `e[last]` collapse to null and the pipe grow two phantom rows, which is exactly the silent breakage that shipped unnoticed.

**Migrated call sites** (`content()` everywhere): `graph/model.ls` `element_children`/`child_items`, `graph/transform/content.ls`, `graph/transform/html.ls`, `editor/mod_edit_schema.ls` `children_array`, and `math/optimize.ls` — where `can_merge` tested `len(a) != 1` meaning *exactly one child*, so a single class attribute silently disabled all span merging. Fixtures using the idiom to express a child walk were migrated the same way rather than re-baselined; only 6 goldens changed, all bare length numbers.

**Still open, and worth a ruling of its own:** a `group by … into g` binds an element whose attributes are the group key, so `len(g)` now counts the key alongside the members and member count must be spelled `len(content(g))`. That is correct under S8.3.1v2 but is an ergonomic wart on the group-by surface.

<a id="lr07-16"></a>**LR07-16 · Dynamic calls ignore argument names · FIXED 2026-09-25**
`fn f(a, b) => a - b; let g = f` then `g(b: 1, a: 5)` returns `-4` on both
tiers where the direct call `f(b: 1, a: 5)` returns `4`: a call through a value
binds named arguments positionally, silently. `ast_resolve_call_args` needs the
callee's declaration, which a dynamic call does not have. Either reject named
arguments on a dynamic callee or resolve them against the runtime signature
(`Function::fn_type`).

*Fixed 2026-09-25:* S12.3.2 and D6.2.2v2 already rule it: a dynamic call with named arguments is rejected. `resolve_call_body` (`build_ast.cpp`) now reports E212 at the first named argument when the callee is neither a system function, a resolved object method, a direct function (`ast_direct_call_function`) nor a type conversion. Every shape that had bound positionally — a `let`-bound function, an arrow, a `fn` parameter, a map-field function, an immediately invoked arrow — is rejected on both tiers; direct and module-aliased calls still bind by name. A compile scan of all 1,934 tracked `.ls` files found no caller relying on the old binding, and `doc/Lambda_Func.md` now states the rule. Fixture `test/lambda/negative/semantic/named_arg_dynamic_call.ls` (`NegativeScriptTest.NamedArgumentsNeedStaticallyKnownCallee`). A statically resolved object method still binds named arguments by position: [LR07-19](Lambda_Issue_Ledger.md#lr07-19).

<a id="lr07-17"></a>**LR07-17 · JIT: an imported `pub let` holding an int literal reads `0` · FIXED 2026-09-25**
A module with `pub let A = 10` and `pub let B = 1 + 2`, imported with
`import .m`, gives `[A, B]` = `[0, 3]` on the JIT and `[10, 3]` on the
interpreter. Computed values, floats, strings, and arrays import correctly, and
`A` reads correctly inside the module's own functions. Probable site:
`load_module_var_slots` (`transpile-mir.cpp:7733`) — the literal never reaches
its slot. The survey also reports, unverified here, that a module whose
annotated init fails is still importable on the JIT (reads `0`, exit 0) while
the interpreter aborts, contradicting **D7.2.2**. Violates **S1.6**.

*Fixed 2026-09-25:* the fault was the importer's const-fold pass, not `load_module_var_slots`. An imported name points at the provider's own declaration, and `interp_const_binding_decl` (`interp.cpp`) accepted it, so `interp_const_init_value` read the provider's literal span out of the *importer's* source text (a comment line added above the import changed the value read) or looked its fold handle up in the importer's constant pool; a large module could read past the importer's buffer. Int, negative, hex, bool and `x: int = 5` literals were affected; floats, strings and computed values were not. An imported name is now never a const binding, so the read goes through the module slot, as `B` already did. The secondary claim reproduced as a separate defect: `run_script_mir` (`transpile-mir.cpp`) discarded each imported module's init result, so a module whose init ended in an ordinary error (`pub let C: int = g("s")`) still ran its importer on the JIT, exit 0. It now publishes the error and stops, as T0's `interp_execute` does (D7.2.2). The existing `test/lambda/import_vars.ls` had been wrong on the JIT all along (`version` read 0); goldens run on `auto`, which starts in T0. Fixtures: `import_const_exports.ls` (with `mod_const_exports.ls`) and `import_vars.ls` in `kTune27TierParity`; `negative/import_init_error_driver.ls` (`NegativeScriptTest.ImportInitErrorBlocksExecution`, all three tiers). Found on the way: [LR07-20](Lambda_Issue_Ledger.md#lr07-20).

<a id="lr07-21"></a>**LR07-21 · JIT: a `for` over `[true, false]` takes the true branch for `false` · FIXED 2026-09-25**
```
pn main() {
  var hits = []
  for (b in [true, false]) {
    if (b) { push(hits, "t") } else { push(hits, "f") }
  }
  print(hits)
}
```
T0 prints `["t", "f"]`; the JIT prints `["t", "t"]`: a wrong value with no error (S1.6). Reported by the LR12-14 investigation, reproduced 2026-09-25; root cause not located.

*Fixed 2026-09-25:* the loop variable was declared `bool` while its register still held the boxed element. `iter_val_at` returns an Item, and the for-in binder (`transpile-mir.cpp`) unboxed only int, int64, uint64, float and string. A variable's reads take their carrier from its TypeId (`mir_value_from_var_entry`), so a `bool` variable was read as bool's 0/1 lane, and `bf` tested the whole tagged word, which is nonzero for `false`. S3.1 puts `false` in the falsy set. `if`, `not`, `where`, group-by keys, join sources (both sides, left joins included), indexed pairs and nested loops all read every element as true; the index read `xs[1]` was right. Boxing consumers (`print(b)`, `b and x`) escaped because the bool boxer keeps only the low byte, and pointer-typed elements escaped because their consumers accept a tagged or a raw pointer. The same hand-kept list was duplicated in the join, group-by and `open` alias binder, which also read the raw `type_id` instead of `mir_value_type_id`, so an occurrence contract (`T?`) there was bound as the meta-type (the LR07-7 hazard). Both copies are now one binder, `mir_bind_item_var`. It unboxes every type in `is_native_param_type_id`, the parameter binder's set with bool included, into the carrier `lambda_canonical_rep_for_type_id` names. A JIT-tier sweep of the 999 goldened fixtures against the pre-fix binary found none that passed before and fails now. Fixture: `test/lambda/proc/for_in_bool_truthiness.ls`, pinned in `kTune27TierParity`; it fails on the pre-fix JIT and passes on `auto`, which starts in T0 and hid the defect.

<a id="lr07-23"></a>**LR07-23 · JIT: a method's `_b` wrapper re-boxed its Item result · FIXED 2026-09-25 (found the same day)**
`type Counter { value: int, fn double() => value * 2 }` with `<Counter value: 5>.double()` is `10` on T0 and `inf` on the JIT. Six goldens segfaulted on the JIT (`object_method_receiver`, `proc/map_object_robustness`, `proc/object_direct_access`, `proc/object_method_write`, `proc/object_mutation`, `proc/proc_object_counter`) and six returned wrong values (`object_method_value`, `object_nominal`, `import_pub_types`, `proc/object_open_instance`, `proc/proc_keyword_object_members`, `proc/typed_param_direct_access`). Found by running every golden with `LAMBDA_TIER=jit`; goldens run on `auto`, which starts in T0.

*Fixed 2026-09-25:* the forward-declare prepass pre-registered native call facts with `is_method = false` ("in prepass, method_owner is not set"), so a method whose return type infers as a scalar got a native-return NativeFuncInfo. `transpile_func_def` never gives a method a native body, because methods dispatch through their `_b` entry. The body therefore returned a checked Item, while `emit_boxed_abi_wrapper`, built from the stale NativeFuncInfo, read that Item as a raw int lane and tagged it again. The forward-declare walk now sets `mt->method_owner` for object methods, as the define walk already did (both through `mir_object_type_owner`), and pre-registration asks `mt->method_owner != nullptr`, as final lowering does (S1.6). `object_method_receiver.ls`, whose header says tier parity is its point, had been identical on both tiers when LR07-15 was fixed and regressed unseen; it is now pinned in `kTune27TierParity`. The skip-listed `object`, `object_inherit`, `object_update` and `map_object_robustness` now pass on all three tiers as well.

<a id="lr07-24"></a>**LR07-24 · JIT: a widened bool array's read folded the new value to `false` · FIXED 2026-09-25 (found the same day)**
`var mixed = fill(3, true); mixed[2] = "mixed"; mixed[2]` is `"mixed"` on T0 and `false` on the JIT (`proc/proc_fill.ls`, `proc/proc_fill_bool_lane.ls`).

*Fixed 2026-09-25:* the index read of an array whose inferred element type is bool (`nested_bool` in `emit_generic_array_index_value`) guards the packed ELEM_BOOL layout and falls back to `item_at`. T21-1c made that read publish a native bool, folding the slow arm's Item to its low bit; the store had already turned the array into a generic one, so the slow arm held the string. The read is now a native bool only when every arm yields one (`mir_array_bool_elements_proven`): a declared rank-one bool lane, or a binding witness, which is installed only when no store can retag the lane (for bool, `mir_store_may_change_elem_type` rejects every value not proven bool). Otherwise it uses `MIR_INDEX_RESULT_BOXED_BOOL`: the fast arm's byte is boxed, and the slow arm's Item is kept. Inference must never change a result (D3.3.1v2, D3.3.3v3). The declared `bool[]` fast paths that T21-1c's gates measured (`sieve2`, `primes2`) are unchanged. The same flaw in other consumers is [LR07-28](Lambda_Issue_Ledger.md#lr07-28).

<a id="lr07-25"></a>**LR07-25 · JIT: a repeated literal key read its first entry · FIXED 2026-09-25 (found the same day)**
`{a: 1, a: 2, b: 3}.a` is `2` on T0 and `1` on the JIT (`map_duplicate_key_lookup.ls`, four rows).

*Fixed 2026-09-25:* a literal shape keeps a repeated key's entries in source order. The runtime reader (`_map_get_keyed`) and the checker's member oracle take the last entry, but the JIT's static field planner used `find_shape_field_by_name`, which returns the first. `find_shape_read_field_by_name` (`transpile_shared.cpp`) now returns the entry a read resolves, and `mir_plan_static_field` uses it (S1.6). `find_shape_field_by_name` still returns the entry `fn_map_set` writes, because the guarded store's fast arm must write the slot its `fn_map_set` fallback writes. LambdaJS shares the planner, but its literals deduplicate keys, so its output is unchanged. The write side's own inconsistency, on both tiers, is [LR03-17](Lambda_Issue_Ledger.md#lr03-17).

<a id="lr07-26"></a>**LR07-26 · JIT: a string-pattern `case` compared with `==` · FIXED 2026-09-25 (found the same day)**
With `type digits = \(d+)`, `match "123" { case digits: "number" default: "other" }` is `"number"` on T0 and `"other"` on the JIT (`match_string_pattern.ls`, seven rows; `symbol_pattern.ls`, two).

*Fixed 2026-09-25:* `emit_single_pattern_test` uses `fn_is` for a type-valued pattern and `fn_eq` otherwise. `mir_is_type_value_node` counted a name as a type only when it named a type declaration or an object type, so a pattern name's carrier fell through to its string lane, and the arm compared the string with the pattern. T0 decides by the pattern's runtime value. The rule for which definitions denote a type (a `type` statement, an object type, a type-definition declarator, a string or symbol pattern) is now `ast_definition_denotes_type` in `ast-core.hpp`, shared by the checker's `ast_is_explicit_type_value` and by `mir_is_type_value_node` (S1.6, rule 13).

<a id="lr07-27"></a>**LR07-27 · JIT: a direct store wrote a raw int64 over an `i64?` field · FIXED 2026-09-25 (found the same day)**
With `type Row = {value: i64?}`, `var t: Row = {value: 7i64}; t.value = null; t.value = 8i64; t.value` is `8` on T0 and `error` on the JIT (`proc/proc_nullable_native_i64_map.ls`).

*Fixed 2026-09-25:* a persistent `i64?` field is a TypedItem, the wide-optional layout (D2.5.2v3, D2.6.4v3), but `shape_entry_storage_type_id` reports its value domain, int64. The typed-record direct store in member assignment wrote `it2l(null)`, then a raw 8, over the TypedItem, and the next read found an invalid tag. The typed direct read already refused such lanes. `mir_direct_field_lane_supported` now states the one rule both directions follow: a nullable native lane is accessed directly only for containers (null is the zero pointer) and `int` (`INT_LANE_NULL`). `bool?`, `float?`, `i64?` and `u64?` stores take the checked setter. Representation follows the full contract, never the TypeId (D2.5.1).

<a id="lr07-31"></a>**LR07-31 · `~key` in a single-subject body read a stale register, and a value arm hid a nested pipe's `~` (S10.1.3, S1.6) · FIXED 2026-09-25 (found 2026-09-25)**
A match arm, a constraint predicate, a handler's value arm and a method bind `~` to one subject, so `~key` is null there; T0 had it so for arms and constraints. The JIT left `pipe_index_reg` as it found it (`mir_bind_subject`, `transpile_match`, the method prologue). Inside a pipe, `~key` read the pipe's index, so `[5, 6] |> match ~ { case int: ~key }` was `[0, 1]`. Outside any pipe the function did not compile ("undeclared reg 0"), for `match 5 { case int: ~key }` and for a method reading `~key`. In a handler's value arm the JIT read the pipe's index or `0`, and T0 dereferenced the arm context's missing key slot and segfaulted. With no walk at all (top level, a view's model) the JIT read `0` where T0 read null. The JIT also bound the value arm's `~` with a flag that outranked the pipe context, so a pipe nested in the arm read the handled value as its own `~`: `[1, 2] ^ {0} ~ { ~ |> ~ * 10 }` was `[[10, 20], [10, 20]]`.

*Fixed 2026-09-25.* Each of those JIT bindings sets `~key` to null, and so does `~key` outside any binding. The value arm binds its `~` through the pipe context, saved and restored around the arm, and the `in_handler_value` flag is gone. T0 reads a missing key slot as null, and its value arm now roots at the operand, as the JIT's does, so a proviso nested in the arm agrees on its root. Fixture `current_key_subject.ls`, pinned in `kTune27TierParity`.


<a id="lr07-32"></a>**LR07-32 · JIT: an `any`-typed subscript key took the typed-array int fast path · FIXED 2026-09-26 (found 2026-09-25)**
With `a = [10, 20, 30, 40, 50]`, a key whose static type is `any` was decoded as an int whatever it held: `let r = 1 to 3; a[r]` and `fn at(x) => a[x]; at(1 to 3)` read `10`, `at("k")` read `10`, a mask held in a variable read `0`, and `at(1.5)` read `20` once an int call to `at` existed. The interpreter dispatches on the key's value and was right in every case.

*Fixed 2026-09-26:* `emit_index_result_value` (`transpile-mir.cpp`) sends every key whose carrier is not an integer to `fn_index`, and the string path no longer admits `any`; the fast paths it guarded (`FAST PATH 1d`, `FAST PATH 3`, the `any` branch of `emit_array_num_index_load`, the `|| idx_tid == LMD_TYPE_ANY` arms) became unreachable and were deleted. The carrier oracle `mir_expr_carrier_type` must route exactly as the emitter does, so its INDEX_EXPR arm claims the element lane only for a position key (`idx_is_position`: an int carrier, a native int expression, or a semantic integer); a range key's slice had been published under the element's carrier and unboxed as `0` (`fn g(x: int[]) => x[1 to 2]`). Semantics: S8.2.1v4, S7.1.1v3, S8.2.4v3. Fixture `test/lambda/subscript_selection.ls` (LR07-32 line), pinned on all three tiers in `kTune27TierParity` and in the forced-GC sweep.

<a id="lr07-33"></a>**LR07-33 · The checker typed a slice or multi-key read as the element or row, so a map field stored `0` or `null` · FIXED 2026-09-26 (found 2026-09-25; both tiers)**
`{k: a[r]}` with `r` a bound range stored `0` on the interpreter, and `{v: m[1, 2]}` stored `null` on both tiers ("value type int does not fit nullable native map lane").

*Fixed 2026-09-26:* `direct_field_result_type` (`build_ast.cpp`) types an index read as the element only when every key names a position (`ast_index_key_is_position`: a number, or a nullable one) and steps one nested array type per key, so `m[i, j]` is the element and a partial `t[i, j]` the row; any other key is `any`. `declared_compound_destination_type` returns no contract for a subscript statically known to select (`ast_index_keys_select`: several keys, a range, an array, a type), and the read site uses a declared contract only for a position key. Pitfall met on the way: `unwrap_primary_node` returns NULL for a literal key (a PRIMARY that carries its value), which silently dropped `v[3]`'s declared `float?` and boxed the out-of-range sentinel as `nan`; the classifiers use `ast_unwrap_primary_to_leaf`. SI14, S8.2.4v3. Fixture: the LR07-33 line of `subscript_selection.ls`.

<a id="lr07-34"></a>**LR07-34 · `fn_int64_index` was emitted by the N-D subscript lowering but not registered · FIXED 2026-09-26 (found 2026-09-25)**
Any N-D subscript with a non-native key — `m[1, j]` with `j = 1.0`, `fn f(i) => m[1, i]`, `m[0 to 1, 1]` — failed to link the module ("import of undefined item fn_int64_index").

*Fixed 2026-09-26:* the helper is in `sys_func_registry.c`. Two neighbours of the same path were fixed with it. The interpreter decoded each N-D key with `it2l`, so a range or `1.5` named some position; `interp_eval_ndim_indices` now uses `fn_int64_index`, whose `INT64_MIN` reads as absent. And the JIT passed an unboxed non-array payload to `array_num_at_nd`, which reads the N-D flag from byte 2 of the Container header — for a `String`, the third byte of its length, so a string of 64 KiB or more would be walked as an N-D array; both tiers now call the checked `fn_index_nd` (`lambda-eval.cpp`), which is `null` unless the target is a typed array (the interpreter had returned an error, where S7.1.1v3 requires `null`). Fixture: the LR07-34 line of `subscript_selection.ls` (`m[1, j]`, `m[1, 1.5]`, `m[0 to 1, 1]`, an untyped key, `"abc"[0, 1]`).

<a id="lr07-35"></a>**LR07-35 · A partial N-D subscript returned `null` where the design record says a leading-axis view · FIXED 2026-09-26 (found 2026-09-25; both tiers)**
`t[1, 3]` on a 2×4×6 array was `null`; `t[1][3]` was the row.

*Fixed 2026-09-26* per [`Lambda_Typed_Array2.md`](Lambda_Typed_Array2.md), the normative record where no `S#` covers the point (Doc_Convention §2): `array_num_at_nd` steps one leading axis per key with `array_num_get` when there are fewer keys than axes, so `t[1, 3]` equals `t[1][3]`; more keys than axes, or an out-of-range key, stay `null`, and the full-rank read stays allocation-free. The forced-GC sweep caught the first version: the JIT passes the keys in a `heap_data_calloc` buffer, which the first view allocation collected before the next key was read, so the keys are now copied to the stack on entry. Fixture: the LR07-35 line of `subscript_selection.ls`, in the forced-GC sweep.

<a id="lr07-36"></a>**LR07-36 · JIT: `last` in a nested subscript resolved against the outer container, and a computed container was evaluated twice · FIXED 2026-09-26 (found 2026-09-26)**
`a[1 to 3][last]` was `null` on the JIT (40 on the interpreter); `src()[last]` called `src` twice; and `a[last] = v` failed to compile ("`last` used outside subscript") unless a read of `a` had been lowered before it, whose container it then borrowed.

*Fixed 2026-09-26:* `mt->last_index_object` is scoped by `MirLastIndexScope`, the JIT's counterpart of `InterpLastIndexGuard`, in `emit_index_result_value` and in the index-assignment lowering (S7.2.2). A container is evaluated once and handed to `last` through `mt->last_index_item_reg`: the multi-key path now evaluates its container before its keys, as the interpreter does, and a single key that uses `last` (`mir_keys_use_own_last`, which skips a nested subscript's own key) on a container that is not a plain name reads through `fn_index` with the container evaluated first. Fixtures: the LR07-36 line of `subscript_selection.ls`; `test/lambda/proc/subscript_last_scope.ls` (writes, `m[last, 0]`, single evaluation), both on all three tiers and in the forced-GC sweep.

<a id="lr07-37"></a>**LR07-37 · An inline `T | null` in expression position was a set union, so `e[(int | null)]` selected nothing · FIXED 2026-09-26 (found the same day)**
`[1, null, 2][(int | null)]` was `null` on the interpreter and `1` on the JIT (the JIT half was LR07-32), because `int | null` evaluated to `[]`: `promote_type_union_expr` makes a static type only of two explicit types, so the operation fell to `fn_union`, whose walk over a scalar's content finds nothing (S8.3.1v3). `1 | 2` and `1 | 1` were `[]` for the same reason, `[1] | 2` was `[1]`, and `int?` cannot be spelled in an expression, where `?` is the query operator.

*Fixed 2026-09-26 by S10.1.1v2 (USER):* `|`, `&` and `!` are type operators only — `fn_union`/`fn_intersect`/`fn_exclude` (`lambda-eval.cpp`) always build a type, a scalar operand reading as its literal type, and never collapse (`1 | 1` is a type, `1 & 2` the empty type). Set algebra on containers moved to `unique(a, b, ...)`, `intersect(a, b, ...)` and `except(a, b)` (`lambda-vector.cpp`). `[1, null, 2][(int | null)]` is `(1, 2)` on both tiers. Fixture `test/lambda/type_set_operators_expr.ls`, all three tiers. A same-day first version that collapsed decided type operations and kept container set operations was replaced before release; its argument is in [Formal2 C6.4](Lambda_Semantics_Formal2.md).
<a id="lr07-38"></a>**LR07-38 · JIT: a one-value numeric literal contract took its native lane as proof, so `let m: 1 = 2` bound 2 (S11.2.1, S11.4.1v3) · FIXED 2026-09-26 (found 2026-09-26, while fixing LR03-31)**
The interpreter rejected `let m: 1 = 2`, `let f: 2.5 = 1.5` and `g(2)` against `fn g(x: 1)`; the JIT bound 2, 1.5 and 2. Two shortcuts treated a contract on the int or float carrier as proved by a native lane without asking whether it was a literal: `mir_boundary_is_redundant` (arguments, returns, and declarations it answers for) and the `native_scalar_declaration` path of a `let`, whose lane check tests the carrier only. A union of literals (`1 | 2`), a string literal and a bool literal were enforced on both tiers, since none of them has a native numeric lane.

*Fixed 2026-09-26.* Both shortcuts exclude a literal or const contract, which then takes the general boundary and its value check (the neighbouring `optional_int_lane_declaration` already excluded literals). Fixtures `negative/runtime/literal_admission_int_declaration.ls`, `literal_admission_float_declaration.ls` and `literal_admission_int_param.ls`, each rejected on every tier (`ExpectRejectedOnEveryTier`).

## 8. Memory management & GC (LR_08)

<a id="lr08-r3"></a>**LR08-R3 · JIT rooting hinged on dishonest static types · RESOLVED 2026-09-13**
The collector-side face is resolved with the same fail-closed
`lambda_gc_value_class` classification and the runtime root witness archived as
[LR07-R7](#lr07-r7). Level-2 witness coverage checks both candidate bindings and
non-candidate expression temporaries against actual GC-managed pointer lanes;
the current corpus sweep reports zero violations. The implementation remains
precise `RootFrame`/`Rooted` ownership, as required by **D2.4.1–D2.4.3** and
**D5.4.2**, never conservative stack scanning.

<a id="lr08-4"></a>**LR08-4 · Wide scalar ownership at Lambda escaping stores · RESOLVED 2026-09-14**
The Lambda-core audit is complete: native Arrays and packed Map/Shape fields
now use their destination-owned layouts; generic containers, closure
environments, VMap, module storage, concurrency/task state, and async paths
route escaping scalar Items through their owning store/rehome helpers. Concat
rebases through `array_set`, and a wide intermediate held across `wait` remains
correct under forced collection. This satisfies **D2.5.2v3**, **D2.6.1v3**,
**D2.6.4v3**, and **D5.2.2v3** for the Lambda runtime.

The audit found two JavaScript-native carrier defects after the Lambda-core
paths were closed: Error standard own fields and JS collection entry storage.
They are tracked separately as [JS03-L1](JS_Issue_Ledger.md#js03-l1); they do
not leave a remaining Lambda-core runtime path under this record.

<a id="lr08-r12"></a>**LR08-R12 · Generator/async suspension states capped at 64 · RESOLVED (2026-09-08)**
`JsMirTranspiler::gen_state_labels` was `MIR_label_t[64]`, and two clamps
matched it — `if (yield_count > 63) yield_count = 63;` and the identical line
for `await_count`. Both truncated silently: a 100-yield generator summed only
its first 62 values (1891 instead of 4950), and a 150-await async function was
wrong the same way. Fixed by exact-sizing the label array from the pre-counted
state count, checking that capacity in `jm_next_resume_state` instead of a
literal 64, and deleting both clamps. Same sweep produced LR09-30 above.


<a id="lr08-r11"></a>**LR08-R11 · Native realm construction is not GC-safe · RESOLVED (2026-09-08)**
The JS realm's native module builders were written against an implicit
"no collection happens here" assumption. Under
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1` this fails in three distinct
ways, all violating **D5.4.2** (a value under construction is live and needs an
exact root); D2.1.7 pins the heap as non-moving, so these are liveness bugs, not
address-stability bugs.

1. **Root ranges registered one Item too high.** Five `JsNamespaceState`-derived
   caches (`stream`, `http`, `https`, `net`, `fs`) registered their precise root
   range at the first *derived* field rather than the inherited
   `namespace_object`, which the base lays out first. Each range therefore left
   its namespace object unrooted *and* scanned one Item past the end of the
   struct. `require("stream")` returned a namespace with **zero** properties
   under forced GC. Fixed 2026-09-08 in `js_runtime_state.cpp`; the catalog now
   starts every such range at `namespace_object`, and a one-time runtime check
   (`js-root-range:` in the log) pins the start/count pair for all seven
   namespace states. These states are not standard-layout, so `offsetof` on them
   is ill-formed and the guard cannot be a `static_assert`.

2. **Factories that build an object on a bare C local.** `js_new_object()`
   followed by a run of allocating property installs, with the only reference in
   a C automatic. The object is reachable from nowhere until the last store, so
   a collection mid-construction reclaims it and the finished object comes back
   missing methods, or a later store writes into reclaimed memory and crashes.
   Fixed: the four `node_crypto` factories, both `node_path` parse factories and
   `path.win32` (this one crashed `require("path")` outright), `node_os`
   `networkInterfaces`/`userInfo`, the `stream` base constructor and its
   prototype, and `http.STATUS_CODES`.

3. **Two allocating arguments in one store.** `set(obj, make_string(k),
   make_string(v))` — argument evaluation order is unspecified, so whichever
   operand is built first is an unrooted temporary while its sibling allocates.
   The canonical rooted publisher `js_install_native_*`
   (`js_runtime_function.cpp`) already carries this rule as a comment citing
   D5.2/D6.2.2v2, but hand-rolled `*_set_method` clones bypassed it. Fixed:
   `stream_set_method`, `assert_set_method`/`assert_set_fresh_method`/
   `assert_set_method_item`, `js_path_set_method`, `dns_set_constant`,
   `js_message_port_data_clone_error`, and the `js_net` address-property and
   `node_events` unhandled-error stores. The 15 Jube-module `*_set_method`
   clones were already correct.

**Second pass (2026-09-08) closed it.** All 26 built-in modules now report
byte-identical key sets with and without
`LAMBDA_GC_FORCE_EVERY=1 LAMBDA_GC_POISON_FREED=1`, and
`make test-jube-node-core-dynamic` — whose forced-GC arm produced 7 of 35
registry lines on pristine `HEAD` and 11 after the first pass — passes.

A fourth shape appeared in this pass, and it is the most dangerous of the four:

4. **A cache slot published before its root exists, or with no root at all.**
   `js_get_internal_stream_state_namespace` assigned the fresh object into a
   `JsStreamState` slot without first calling `stream_ensure_roots()`, so the
   range backing that slot was not yet registered. The object was reclaimed and
   its storage reused by the two native functions installed immediately after,
   which is why the module registry reported this namespace as a **function**
   rather than an object. `js_get_internal_stream_add_abort_signal_namespace`
   was worse: it cached into a function-local `static Item`, which a precise
   collector never scans at all. That namespace now has a real slot in
   `JsStreamState` (range 44 → 45, guard's last field updated accordingly), and
   both getters plus `js_get_internal_stream_end_of_stream_namespace` register
   the range before publishing. The other 13 function-local `static Item`
   namespace caches in `js_runtime.cpp` were audited and all call
   `heap_register_gc_root`, so this was the only unrooted one.

Also fixed in this pass: `tls` rootCertificates key (freed across the
certificate-bundle build) and the bundled-pem push; `http` `METHODS` array and
`globalAgent` (both unreachable across their own construction); the three `dns`
server-array builders (`dns_load_system_servers`, `dns_array_copy`,
`dns_validated_servers_copy`, whose arrays were bare locals across push loops —
this is why `dns.__dns_servers__` was absent while `getServers()` still worked);
and `zlib` `constants`, which was created and then left unrooted while the
sibling `codes` object allocated, so its root slot received a reclaimed pointer.

**Rule of thumb the four shapes reduce to:** publish into a registered root
*before* the next allocation, never between two of them. The ~194-site shape
scan of `lambda/{js,module,dom}` remains a starting point for future audits, not
a defect list — most entries are reachable through an already-rooted owner.


<a id="lr08-1"></a>**LR08-1 · Decimal `mpd_t` leak (in-code TODO) · RESOLVED 2026-09-05**
Per **D4.3.3**, the GC delegates out-of-zone cleanup to the C++
`heap_gc_destroy_external_payload` bridge. For `LMD_TYPE_DECIMAL`, it calls
`decimal_payload_release`, which runs `mpd_del(&dec_val)` and clears the
embedded payload before sweep reclaims the wrapper. Teardown uses the same
bridge, so it has one idempotent ownership path rather than a Decimal-specific
second free.
`GCHeapTest.DecimalPayloadFinalizerReleasesMpdDuringSweep` verifies that a
dead Decimal's real `mpd_t` payload is released during collection, not only at
context teardown.


<a id="lr08-5"></a>**LR08-5 · Hard-coded struct byte offsets in tracing and compaction · RESOLVED 2026-09-05**
D2.6.6v2/D3.4.1: the C collector now consumes `LAMBDA_GC_OFF_*` constants
derived by `offsetof` from one canonical C ABI layout. Assertions bind that
layout to the C and C++ definitions of the container chain, `TypeMap`,
`ShapeEntry`, `TypedItem`, `ArrayNumShape`, `Function`, and `VMap`; trace,
compaction, and GC test fixtures no longer embed their own byte positions.
The retained separate `item_to_ptr` high-byte-zero platform assumption is not
an offset-layout issue.

<a id="lr08-6"></a>**LR08-6 · `SHAPE_POOL_MAX_CHAIN_LENGTH` = 64 returned a NULL shape · RESOLVED 2026-09-25 (verified 2026-09-26)**
A map or element with more than 64 fields got no pooled shape (`shape_pool.cpp`), with only a `log_warn` and a possible NULL dereference downstream. The shape pool was retired on 2026-09-25 (`c7aa73eb4`, D3.4.3v2): maps, parsed elements and MarkEditor rebuilds share whole types through the per-Input transition tree, which has no chain cap, and a declined tree gives the container a private chain of its own (`rebuild_private_chain`). A rebuild that cannot describe or lay out its fields returns `ItemError`. Verified 2026-09-26 when the entry was found still open: `MarkEditorTest.RebuildLaysOutManyFields` (201 fields) and `LaneStorageResolverTests.ShapeBuilderHasNoFieldLimit` (200), and a 10,001-key JSON map reads back whole; it also prints since [LR11-4](#lr11-4).


## 9. Runtime builtins (LR_09)

<a id="lr09-2"></a>**LR09-2 · `SysFuncInfo` lacked a data-driven native-argument convention · RESOLVED 2026-09-14**
The coarse call-wide `c_arg_conv` flag is retired. A fixed-arity registry row
may now provide a `SysFuncArgDesc` array, one required `ValueRep` per ABI
parameter; a null array explicitly retains the historical all-`Item` ABI.
`band`, `bor`, `bxor`, and `bnot` declare their compact integer-lane slots in
the table.

Generic fixed-arity direct-call lowering now evaluates each argument to a
`MirValue` and requests that row's carrier through `em_require_rep()` before
forming the MIR call prototype. Thus the producer's contract and representation
stay explicit until the ABI boundary, as required by **D2.4.1–D2.4.3**. The
bitwise code retains only semantic dispatch: dynamic/full-width cases use the
boxed classifier, while proven compact-int cases use the descriptor-selected
native carrier. Interpreter planning and dispatch read the same descriptors.

`SysFuncRegistry.NativeBitwiseArgumentsUsePerSlotDescriptors` covers the
native rows, a mixed Item/F64 descriptor, and the null-array Item default;
item-representation tests (33/33) and the focused JIT bitwise corpus (4/4)
pass. The full Lambda baseline completed 3315/3321: its six remaining failures
are two MIR budget ratchets and four benchmark cases, including two shift-only
cases that use the untouched all-`Item` ABI.

<a id="lr09-r30"></a>**LR09-R30 · Regex capture groups silently truncate at 256 · RESOLVED (2026-09-08)**
A regular expression with more than 255 capture groups reports the wrong result
and gives no diagnostic. Repro:

```js
const n = 300;
const re = new RegExp('(a)'.repeat(n));
const m = 'a'.repeat(n).match(re);
console.log(m.length - 1, m[n]);   // Lambda: 255 undefined   Node: 300 a
```

`$300` in a `replace` pattern likewise resolves to nothing. The cause is
`JS_REGEX_MAX_GROUPS` (256, `js_regex_wrapper.h:25`) with clamps of the form
`if (ngroups > JS_REGEX_MAX_GROUPS) ngroups = JS_REGEX_MAX_GROUPS;` at seven
sites across `js_runtime.cpp` and `js_regex_wrapper.cpp`. The constant sizes
about a dozen **stack** arrays (`re2::StringPiece matches[...]`,
`int starts[...]/ends[...]`, `RegexGroupInfo groups[...]`), so removing it means
either heap-allocating on the match path or sizing from the compiled pattern's
group count. ECMAScript sets no such limit and V8 allows 32,767.

**Fixed 2026-09-08.** `JS_REGEX_MAX_GROUPS` is gone. Match scratch is sized
from the compiled pattern's own group count through one `JsRegexScratch<T>`
helper (`js_regex_wrapper.h`) that keeps `JS_REGEX_INLINE_GROUPS` (32) slots
inline and heap-allocates only above that, so an ordinary pattern still
allocates nothing on the match path. Every clamp is deleted.

**There were three caps, not one, and the first fix only moved the boundary.**
After the match-scratch conversion a 300-group pattern matched correctly but a
*lookahead* over 128 groups still failed. Two more fixed limits stood behind it:

- `erased_original_group[256]` in the wrapper's assertion-rewrite pass, whose
  guards silently stopped the erased-group remap partway, producing a wrong
  rewritten pattern. Now sized from `original_group_count`.
- The backtracking matcher (`js_bt_regex.cpp`), which had `int cap_start[256]`,
  `cap_end[256]`, per-iteration `saved_s/saved_e[256]` and per-lookaround
  `sv_s/sv_e[256]`, plus an explicit `if (ng + 1 > 256) return 0; // fall back`.
  That return is reported to the caller as **no match**, so it was not a
  fallback at all — a large lookahead pattern silently failed. All four arrays
  are sized from the pattern and the refusal is deleted.

Verified against Node on match, `exec`, high-numbered `$n` replacement and
lookahead at 128/150/200/300 groups: byte-identical output. Regression test
`test/js/regex_many_capture_groups.{js,txt}` covers all six cases; JS gtest is
371 tests, up from 370.

No performance cost: the ordinary-pattern match path got *faster* in a
debug-build A/B (813 ms vs 1017 ms over 600k matches), which is consistent with
no longer placing 4 KB of `re2::StringPiece[256]` and 2 KB of `int[256]` on the
stack per match. Per rule 10 that debug figure is directional only; the point is
that it is not a regression.

Gates: test262 40261/40261 with 0 regressions, JS gtest 371/371, script gtest,
rooting core, MIR GC stress, lambda baseline 5078/5078 (the memtrack gate), node
slice identical to pristine.


<a id="lr09-6"></a>**LR09-6 · `set_runtime_error` message buffer cap · RESOLVED 2026-08-28**
`set_runtime_error` and `err_createf` formatted into fixed 1024-byte stack
buffers, silently truncating rich diagnostics. The common formatting path now
measures the required length and allocates the complete message through the
existing `memtrack` allocator before creating the error. This satisfies the
message-bearing error contract in **S7.4.4** and removes the duplicated
formatting path; no new data structure or design ruling was added.

The shared 64-frame native stack-trace default is recorded in
[LR10-R4](#lr10-r4).

Regression: `ErrorCreationTest.CreateFormattedErrorPreservesLongMessage`
verifies the full 1514-byte formatted message. The focused error suite passes
121/121 and `make test-lambda-baseline` passes 3978/3978.

---


## 10. Error handling (LR_10)

<a id="lr10-2"></a>**LR10-2 · Hard-coded 64 KB last-function span · RESOLVED 2026-09-05**
`build_debug_info_table` now gets MIR's actual next allocation address for the
final function's exclusive end, instead of inventing a 64 KiB bound. A missing
frontier falls back to an empty range rather than labeling unrelated native
code. Regression:
`LambdaJitDebugInfo.FinalFunctionRangeUsesJitAllocationFrontier` emits a final
function larger than 64 KiB and resolves an address beyond the old boundary.

<a id="lr10-7"></a>**LR10-7 · Error values do not own their `code` / `message` · FIXED 2026-09-25**
`let a = error("A"); let b = error("B"); [a.message, b.message]` is
`["B", "B"]` on both tiers: the `.code` and `.message` members read
`context->last_error`, not the value (`lambda-eval.cpp:5617`–`5630`), so every
error reports whichever error was constructed last. Contradicts **S7.4.4**
(an error carries its code, message, and source location). The survey also
found `error({code: 42, message: "m"})` reading back as `318` / `"Error"`.

*Fixed 2026-09-25:* an error Item already points at a GC-traced `LambdaError` payload; `fn_member`'s error branch (`lambda-eval.cpp`) read `context->last_error` instead. It now reads `it2err(item)` and falls back to `last_error` only for the payload-less `ItemError` sentinel. `fn_error` gained the parameter-map branch, `error({code, message})` (S7.4.4, `doc/Lambda_Error_Handling.md`); a field that is absent or of the wrong type keeps its default (318, `"Error"`). Three goldens had pinned the stale read: `conc/cancel_parked`, `cancel_before_run` and `cancel_nested_cleanup` printed `Error` for a cancelled task's `"task cancelled"`. Fixture `test/lambda/error_value_payload.ls` (both tiers and forced GC). The other S7.4.4 gaps found on the way are [LR10-10](Lambda_Issue_Ledger.md#lr10-10).

---


## 11. Mark data API (LR_11)

<a id="lr11-5"></a>**LR11-5 · `deep_copy` of `PATH` is shallow · RESOLVED 2026-09-05**
`path_clone` replays the immutable path spine into the destination pool, giving
every copied non-`sys` path independent names and links. Resolution and metadata
caches are deliberately not copied, so no source-owned payload survives. This
keeps the ownership chain precise under **D4.4.3**. Regression:
`MarkBuilderDeepCopyTest.CopyPathRehomesSpineAndDropsSourceCaches` destroys the
source pool before reading the copied path.

<a id="lr11-2"></a>**LR11-2 · `render_map` iterated while it mutated · FIXED 2026-09-26**
The retransform loop re-executed each dirty template inside `hashmap_iter`, and a re-executed body reaches `apply()` → `render_map_record()` → `hashmap_set()`. The map is Robin Hood hashed, so an insert can resize it or shift a bucket, and a dirty entry displaced behind the iterator was skipped: its view kept its stale result. Over fresh maps holding 2 to 120 dirty entries, each re-execution recording one or sixteen children, the old loop missed an entry in about one case in ten. Which case varies from run to run, since the key hashes an interned pointer.

*Fixed 2026-09-26.* `render_map_retransform_with_results` takes the dirty keys first, then looks each entry up afresh before re-executing it, so no iteration spans a re-execution. `render_map_retransform` is that function without results, where it had been a second copy of the loop. Regression `RenderMapRetransform.EveryDirtyEntryRunsOnceDespiteInserts` (`test_source_pos_bridge_gtest`) sweeps those cases: it failed in each of five runs before the fix and passed in eight after.

<a id="lr11-3"></a>**LR11-3 · MarkEditor decided a buffer's owner from `ui_mode_`, not from the buffer · FIXED 2026-09-26**
Inline `container_rebuild_with_new_shape` freed the old data buffer through the editor's pool unless `ui_mode_` was set, on the premise that a ui_mode buffer came from the JIT's result arena. Since the pool rewrite `pool_free` refuses a pointer it does not own, so a wrong flag no longer corrupts the heap as the entry feared, but the flag stayed a guess at provenance: a pool buffer edited in ui_mode was never freed. Child-buffer growth in `elmt_insert_child`, `elmt_insert_children` and `array_insert` guessed the same way: a buffer not in the editor's arena was assumed to be malloc's and `raw_realloc`ed, yet nothing mallocs item buffers — they belong to an Input's arena or pool, a result arena, or the GC heap — so an insert that grew such a buffer aborted in malloc. Those copy loops also ignored the owned wide-scalar tail at the end of the capacity (`list_relocate_owned_tail`): an insert could land on an int64 or float payload.

*Fixed 2026-09-26.* The owner decides. The old data buffer is freed exactly when the editor's pool owns it (`pool_owns`, new in `lib/mempool`). Children grow through `MarkEditor::reserve_children`, which counts the owned tail and calls the io layer's `list_grow_io`, promoted from `collection_io.cpp`'s `expand_list_io`: a fresh buffer from the editor's arena, the tail moved with the items that point into it, the old buffer left to its owner. Regressions in `test_mark_editor_gtest`, each failing before the fix: `InlineGrowthLeavesForeignItemsBuffer` (aborted in `raw_realloc`), `InlineInsertKeepsOwnedScalarTail` (read three corrupted int64 values) and `InlineRebuildFreesOnlyPoolOwnedData` (the ui_mode pool buffer stayed live).

<a id="lr11-4"></a>**LR11-4 · Hard-coded caps with mixed failure modes · FIXED 2026-09-26**
Four caps in one subsystem failed four ways, silent truncation among them. `SHAPE_BUILDER_MAX_FIELDS` (64) had gone with SCU10, which grows the drafts from the caller's arena, but a draft that failed to grow still left `shape_builder_import_shape` with a partial field list, and every caller ignored both its result and `shape_builder_add_field`'s, so an edit could rebuild a container without some of its fields. The printer bailed on any map over `MAX_FIELD_COUNT` (10,000) fields and printed the whole map as `{[invalid map_type length]}`: a 10,001-key JSON input read back whole and printed as that marker.

*Fixed 2026-09-26.* No cap fails silently. `shape_builder_import_shape` returns `false` when its drafts cannot grow, and a MarkEditor edit returns `ItemError` on that or a failed add. The printer's field-count cap is gone; a negative length is still refused as corrupt. The remaining caps fail visibly, as the §15.1 grow-or-error doctrine asks: `MAX_BATCH_UPDATES` (64) returns `ItemError`, `EDIT_SOURCE_PATH_MAX` (32) fails with "source path too deep", and `MAX_DEPTH` (2000) prints `[MAX_DEPTH_REACHED]` where it stops. Regressions `MarkEditorTest.ShapeBuilderImportReportsDraftFailure` and `test/lambda/wide_map_print.ls`.


## 12. Procedural runtime (LR_12)

<a id="lr12-11"></a>**LR12-11 · A callee that returns its parameter aliased the argument · FIXED 2026-09-17**
S9.1.2 / S9.1.3. `pn keep(p: Box) Box { return p }` then
`var r = keep(b); b.size = 9` printed `r.size == 9` on both tiers (found while
fixing LR12-10; present on the Result46 binary). The same held for a write
through `r`, for `fn` callees, for a returned child (`return h.items[0]`), a
conditional or `let`-aliased return, a forwarding wrapper, and a returned `var`
parameter. Every call result was treated as a fresh owner
(`ast_expr_produces_owned_container`), and `return` was the one retention site
inside a callee that set no share bit.

**Fix.** The AST pass decides, per parameter, whether the function result may
be the parameter or a part of it (`ast_function_result_may_alias_entry`). The
result may be:
- the parameter itself, or a member or index path from it (an element of a
  scalar array excepted);
- either arm of an `if` or `match`, or a block's last value;
- a local whose initializer or rebinding may alias the parameter (a loop or
  pattern variable counts conservatively);
- a call argument in a position the callee itself may return.

FUNCTION_END seeds `NameEntry::cow_param_returned`, and script finalize
completes it as a fixpoint (`lambda_ast_note_returned_params`), so forward and
recursive callees resolve. Both tiers then share-mark the result after a direct
call to such a callee (`ast_call_may_return_argument`; T0 in `eval_call`, MIR at
the end of `transpile_call`), for plain and `var` parameters alike. MIR also:
- keeps the share test on the argument roots and their children;
- keeps it on a binding of such a call result;
- skips the mark for a scalar result, and for a call in tail position, whose
  caller marks instead (the mark would also split RV6 pair forwarding).

An entry mark on the parameter (the CW29 placement) was tried first and
rejected: it shared whole containers whose getters return one element
(havlak2: +36k array copies, +38% time).

**Cost.** Code that re-binds a getter result and writes it back now copies the
element, because the element carries its sticky insertion mark. Before, MIR
wrote through the shared object in place, which is the defect itself.
havlak2 pays +34k small map copies. The outputs of all 157 benchmark scripts
are unchanged. Release timing against the post-T29-1 build, which also
predates LR12-10: havlak2 61.0 to 63.3 ms (+4%), splay2 +3%, cd2 +1%;
deltablue2, richards2 and prettier_ast2 are flat.

Regression `test/lambda/proc/call_result_alias.ls` (18 shapes; identical on
interp, jit, auto and default; forced-GC clean).

**Residue (OPEN):** a dynamic callee (a function value) is not analysed, so
`var r = f(b)` with `f = keep` still aliases.

<a id="lr12-12"></a>**LR12-12 · `push` did not capture the pushed value · FIXED 2026-09-17**
S9.3.1 names `push`/`splice` as insertion points that capture by value. On both
tiers (and the Result46 binary) `var x: Box = ...; push(bag, x); x.size = 5`
left `bag[0].size == 5`, for a local root and for a `var` parameter alike
(found while fixing LR12-10).

**Fix.** `push` now captures a value that already has an observer, by the same
rule as a literal element (`ast_expr_insertion_needs_capture`): MIR
`mir_emit_insertion_capture` in the bound-owner and place arms, T0 in the push
binding and place arms. A `var` parameter source is noted as marked so a later
re-borrow detaches it (`interp_note_var_param_marked`). A fresh local whose
only use in the function is one later push or compound store in its own
statement list is moved instead (`NameEntry::insertion_moves_value`,
`lambda_ast_note_insertion_moves`). Without that, deltablue2's constructors
(`var c = {...}; push(w.cons, c)`) left a sticky mark that copied every element
on its next write (+12k map copies, about +11% time). Regression
`test/lambda/proc/push_insertion_capture.ls` (identical on interp, jit, auto and
default; forced-GC clean).

**Residue (OPEN):** `push(f(), x)` into a temporary owner does not capture.
That only matters when `f` returns an argument through a dynamic callee (see
LR12-11).

<a id="lr12-13"></a>**LR12-13 · Index-then-field stores under a declared record array · FIXED 2026-09-17**
D3.2.4v3. Found while testing LR12-12, present on the Result46 binary.
`var bag: Box[] = [...]; bag[0].size = 6` failed on T0 with "typed nested
array assignment index is out of bounds": `lambda_array_path_set_checked`
walked only index keys. The JIT took the raw COW path store and admitted
nothing, so `bag[0].size = v` with `v = "x"` stored the string into the int
lane and read back a garbage integer.

**Fix.** The checked map path setters' contract-generic body is shared
(`runtime_container_path_set_checked[_inplace]`). The array setter delegates
any path with a non-index key to it, and its leaf-only admission accepts a
certified array root as well as a record (`runtime_value_rep_proves_contract`).
An `any` step is an open leaf, like `array`. MIR routes array-rooted member
paths through the same checked setter (`mir_emit_typed_array_path_store`, also
used by the index-only arm). Covered by the LR12-12 regression (including a
rejected dynamic value).

<a id="lr12-15"></a>**LR12-15 · An out-of-range nested store is logged, not raised · CLOSED 2026-09-18 — consolidated into [LR12-24](Lambda_Issue_Ledger.md#lr12-24)**
S7.1.3v2. `var m = [fill(2, 0), fill(2, 0)]; m[2][0] = 1; return 5` returns 5
on both tiers. The store logs its failure: on JIT, a `fn_array_set` null-pointer
message; on T0, "cow path mutation encountered a non-container child". The
procedure continues as if nothing happened. Same for a packed matrix. Repro:
`temp/t29/packed_probe.ls` (`oob_loop`, `oob_packed`).

Closed as a symptom: the four entries are one missing feature — TE-15's
containment and the defect system channel — recorded as [LR12-24](Lambda_Issue_Ledger.md#lr12-24).

<a id="lr12-16"></a>**LR12-16 · JIT drops a store error in a plain-parameter callee · CLOSED 2026-09-18 — consolidated into [LR12-24](Lambda_Issue_Ledger.md#lr12-24)**
S7.1.3v2, SI3v2. `pn store(a: int[], i: int, v: int) int { a[i] = v; return a[0] }`
called with an out-of-range `i` returns an error value on T0. On the JIT it
returns `a[0]`, after logging the same `fn_array_set` bounds error. The same
store in a callee with a local root raises on both tiers. Present on the
post-T29-3 binary. Repro: `temp/t29/oob_int.ls` (JIT prints `param_oob=false`,
T0 `true`).

Closed as a symptom: the four entries are one missing feature — TE-15's
containment and the defect system channel — recorded as [LR12-24](Lambda_Issue_Ledger.md#lr12-24).

<a id="lr12-17"></a>**LR12-17 · JIT COW facts ignored control flow; writes leaked into shared values · FIXED 2026-09-17**
S9.1.2 / D4.4.1. Found in Tune29 §20.3, present on the Result46 binary. MIR
Direct chooses a raw or a share-checked store from per-binding "may be
shared" facts, and updated them in emission order only. A detach or rebind in
one `if`/`match` arm cleared the fact for the path that skipped the arm. A
share made late in a loop body (`var d = c; push(snaps, d)`) did not reach
the store emitted earlier in the body. Eight shapes wrote into a value that
another binding still held; T0 was correct. One of them is havlak2's
`arr_set`.

**Fix.** `MirCowJoin` joins the facts at `if`/`match` merges, starting each
arm from the entry facts. A generalized loop pre-scan marks every outer
binding the body may share at loop entry, and `MirCowLoopJoin` joins the
facts at loop exits. Regressions: `test/lambda/proc/cow_flow_join.ls` and
`test/mir/lambda/tune29_handle_alias.ls` (identical on interp, jit and auto;
forced-GC clean).

<a id="lr12-18"></a>**LR12-18 · A native float return drops a raised boundary error · CLOSED 2026-09-18 — consolidated into [LR12-24](Lambda_Issue_Ledger.md#lr12-24)**
S7.1.3v2, SI3v2. `pn f(a: float[], i: int) float { var s: float = 1.0; s = s + a[i]; return s }`
with an out-of-range `i` returns an error on T0 (the program aborts with E201).
The JIT logs the same E201 and returns `nan`: the native float return lane has
no transport for the error the assignment boundary raised. Present on v46.
Same family as LR12-16. Repro: `temp/t30/h/err_prop.ls`. A boxed (`any`)
return propagates correctly.

Closed as a symptom: the four entries are one missing feature — TE-15's
containment and the defect system channel — recorded as [LR12-24](Lambda_Issue_Ledger.md#lr12-24).

<a id="lr12-19"></a>**LR12-19 · Literal-bounded dense loops read past a short array · FIXED 2026-09-18**
S7.1.3v2, D4.3.1. Found in Tune30 T30-1; present on v46. For
`while (i < 5) { … a[i] … }`, `mir_dense_loop_scan` never copied the literal
bound into its result, so the dense guard compared each array's length with
`-1`, which is always true. Every proven read then loaded past the end of a
shorter array instead of yielding null: `count_lit([1, 2])` counted 0 nulls
where T0 counts 2. Fixed by copying the literal. Once fixed, a second defect
made the guard always false: for a literal bound it emitted
`mulo 5, 5; bo`, MIR folds the product into a move, and the `bo` read a
stale flag. The square is now computed at compile time. Regression
`test/lambda/proc/dense_loop_short_array.ls`.

<a id="lr12-20"></a>**LR12-20 · A dense-guard proof leaked into its fallback arm · FIXED 2026-09-18**
S7.1.3v2. `mir_expr_may_be_null` treated a read that is provable under the
dense guard as never null, even outside the guard-true arm. A versioned
tree's merged result (or a guarded load) can still be the null a short array
yields, so `s = s + a[i]` in a loop skipped the declared binding's rejection
and produced `nan` where T0 raises E201. The proof is now used only while the
arm assumes the guard. Regression: same fixture.

<a id="lr12-21"></a>**LR12-21 · Index arithmetic over lane sentinels wrapped into a valid index · FIXED 2026-09-18**
S7.1.3v2, S4.1.2. `mir_emit_native_index_expr` gave a leaf no validity check,
so a sentinel lane value (`INT_LANE_INF` = `INT64_MAX`, `INT_LANE_NEG_INF` =
`INT64_MIN+1`, `INT_LANE_NAN`, the null lane) entered the index sum as a plain
integer: `a[x + y]` with `x = inf, y = -inf` wrapped to `a[0]` and
`-inf + -inf` to `a[2]`, where T0 yields null. The band test sat only on the
result, which a wrap satisfies. Each leaf that `mir_int_lane_operand_proven_in_band`
does not prove now carries the exact three-instruction test
`(v >> 53) + 1 <=u 1`; a sum of in-band leaves cannot wrap, so the result's
band test is gone and the poison is `(value | mask) >>u shift` (`INT64_MAX`,
which every bounds check rejects, including one holding a nonnegative index
proof that skips its `< 0` test). Present since v46. Regression:
`test/lambda/proc/index_sentinel_sum.ls`.

<a id="lr12-22"></a>**LR12-22 · A raised E201 inside a JIT function yields a value instead of propagating · CLOSED 2026-09-18 — consolidated into [LR12-24](Lambda_Issue_Ledger.md#lr12-24)**
S4.1.2, S7.1.3v2. `pn f(data: int[], n: int, stride: int) int` whose body does
`acc = acc + data[i] + j` with `i` out of range raises E201 on both tiers, but
T0 abandons the caller's statement while the JIT returns `inf` and the caller
prints it. Same family as [LR12-18](#lr12-18) (a native return lane has no
error channel), seen here on a declared int lane. Probe:
`temp/t30/h/tier_divergence_probe.ls` (`wide=` and `sentinel=` lines print on
the JIT only). Pre-existing: reproduces on the Tune29 binary.

Closed as a symptom: the four entries are one missing feature — TE-15's
containment and the defect system channel — recorded as [LR12-24](Lambda_Issue_Ledger.md#lr12-24).

<a id="lr12-23"></a>**LR12-23 · The interpreter's compact-int loop re-ran a partially applied iteration · FIXED 2026-09-18**
S4.1.2. Reported as a saturation disagreement — `steps = steps + m` with `m`
doubling gave `9007199254740991` on the JIT (the exact sum `2^53 - 1`, a legal
int) and `inf` on T0 — but the cause was worse than saturation.
`interp_fast_int_exec` commits each assignment as it executes it, and a value
that leaves the compact band abandons the fast path *mid-body*; the ordinary
evaluator then re-ran the whole iteration, so every statement that had already
committed ran a second time. `while (i < n) { c = c + 1; m = m * K; i = i + 1 }`
returned **4** for `n = 3`: a wrong answer with no saturation in sight. One
iteration is now atomic — `interp_fast_int_collect_targets` records the
register slots the body can write and a bail restores them, so the ordinary
evaluator resumes from the state the iteration started with. Regression:
`test/lambda/proc/loop_fast_path_bail.ls` (pre-fix T0: `bail=4`, `double=inf`,
`guarded=25`).

The second instance noted against this entry — `int(r * (r + 1) div 2)` with a
saturating `r`, where T0 abandons the statement and the JIT prints `inf` — is
*not* this bug. It is the error-propagation family of
[LR12-22](#lr12-22)/[LR12-18](#lr12-18) and stays open there.

<a id="lr12-26"></a>**LR12-26 · An `fn` may call a statically-known `pn` · RESOLVED 2026-09-18 (S12.1.1v2, C20.7)**
Resolved by the designer's ruling that a script's top level is functional: the check now
runs in every `fn` context, and the three reliance sites below were migrated.
Original entry:
S12.1.1 says `fn` cannot call `pn`, and the ruling is unmarked, but no check
exists for a direct call: `fn bad(x) => logsq(x)` with `pn logsq` compiles and
runs on both tiers. Only `call()` (`validate_effect_polymorphic_call`) and pn
object methods were checked. The dynamic half (a `pn` reached through a value)
was closed 2026-09-18 with S12.1.4v3(6), and the static rule now holds inside
`function` bodies (C20-3) via the colour walk in `lambda_ast_finalize_script`
(`colour_walk_call`, `build_ast.cpp`). Extending that one check to every `fn`
context breaks three reliance sites: `lambda/package/dom/edit_history.ls`
(`fn clear_history`/`fn replay_retained` call `pn session.set_history*`),
`test/lambda/proc/type_binder_proc_raw.ls` (module-level calls to `pn`s), and
`test/mir/lambda/tune26_nested_tco_native_result`. Blocked on a ruling for the
module top level's colour, which no S#/D# point states (see C20.6).

<a id="lr12-28"></a>**LR12-28 · A list stored in a field or `fill` keeps its list-ness (S2.5.6, S2.5.7) · FIXED 2026-09-23**
`<e a: (1, 2)>` stores the attribute as a list, so reading `e.a` into an array
literal spreads it (`[…, e.a]` gains two items); S2.5.6 rules that a
single-value slot of a persistent container stores the array image `[1, 2]`.
`fill(2, (1, 2))` stores the list as one element twice (`len` 2) where S2.5.7
rules the list `(1, 2, 1, 2)` (`len` 4). Both tiers.
*Partially resolved 2026-09-22 (P2):* `fill` follows its item —
`fill(2, (1, 2))` is `(1, 2, 1, 2)` and `fill(0, (1, 2))` is `null`.
*Fixed 2026-09-23 (P3):* every single-value slot stores the array image — map,
element, and object literals, member writes (`fn_map_set`, the checked and
COW setters, `vmap_set`), and the MIR's direct field stores, which convert
through `slot_image` whenever the value's static type may hold a list — and
the source list keeps its kind. Record: [plan §12](<impl/Lambda_List_Fixes (done).md>).

<a id="lr12-29"></a>**LR12-29 · The interpreter ran `on` handler bodies as functional blocks (S12.1.3, S2.5.3) · FIXED 2026-09-22**
An `on` handler is a `pn` (S12.1.3), so its body yields its last value
(S2.5.3). MIR compiles every handler with `in_proc` set, but the interpreter
ran handler bodies in a frame with no `fn` node, so `eval_content` built the
body as a list. That was invisible while blocks normalized like content. Once
P1 of the list fixes made blocks keep `null`, the `<input>` keydown handler
in `dom/form.ls` returned `(null, verdict)` — its `if (…) { return … }`
without an `else` contributed the `null` — and the truthy list read as
"handled", so the document-level paste shortcut never ran (paste into a text
field was a no-op; `rsc_scale_context_menu_matrix`, `dom_pkg_paste_ime`).
Handler frames now carry `proc_handler`, which `eval_content` treats like a
`pn` frame.

<a id="lr12-30"></a>**LR12-30 · An `fn` can call a built-in procedure (S12.1.1v2) · FIXED 2026-09-22**
`fn f() => print("x")`, `output(…)`, `cmd(…)` and `today()` all compiled and
ran from `fn` context, including the module top level. The colour walk
(`colour_walk_call`, `build_ast.cpp`) skipped every system function, although
these rows are `is_proc` in `sys_func_defs`. The walk now reads `is_proc` for
every system-function callee, built-in rows and host-module `pn(...)` Jube
rows (D7.4.6, ES48) alike, and reports E224. A name with both colours
(`call`) has already resolved to its `fn` row by then. Reliance sites migrated
with the fix:

| Reliance on built-in procedures in `fn` context | Sites | Migration |
|---|---|---|
| Documentation examples (`check_doc_blocks.py`), mostly top-level `print` | 21 | effects moved into a `pn` (`pn main()`, or a named `pn`); three functional showcase scripts in `Lambda_Reference.md` now end with their value instead of printing it |
| Test scripts (`input_md_simple`, `input_rst`, `datetime_funcs`), none gated | 19 | rewritten as `pn main()` |

A compile-only scan of every tracked `.ls` outside `negative/` (1,708 files,
the 168 package modules through import drivers) finds no remaining site.
Still open around the fix: `now` and `today` are unimplemented (`func_ptr`
NULL; any call fails with "import of undefined item pn_today"), while the
0-argument `datetime()` and `justnow()` read the same clock but are registered
`fn`, and the `log_*` family is `fn` although it writes the log. Which of these
are effects is unruled.


## 13. Schema validator (LR_13)

<a id="lr13-1"></a>**LR13-1 · Suggestions are built but never surfaced · RESOLVED 2026-09-05**
Validation now populates the existing correction generator before errors are
reported when `show_suggestions` is enabled; errors constructed outside a
`SchemaValidator` lazily take the same reporting path. Missing/unexpected
schema-field errors can now call the existing field-ranking helper when the
containing map type is available. The option explicitly suppresses both
population and reporting. Regression:
`LambdaValidator.TypeMismatchSuggestionsAreAttachedAndReported` verifies the
hint appears and that disabling the option omits it.


<a id="lr13-10"></a>**LR13-10 · Element content is checked by pattern-item count, never matched against the pattern · FIXED 2026-09-25 (found 2026-09-25)**
`validate_against_element_type` checks an element's content only by comparing its child count with the type's `TypeElmt::content_length` (`validate.cpp:826` in the fast verdict, `:871` on the reporting path). For a type pattern that field counts the items of the content pattern (`parse_type_pattern.cpp:1510`), so an occurrence stands for exactly one child. The children themselves are never validated: neither their types nor their own content patterns are checked. `is` reaches the same check through `fn_is` → `schema_validator_validate_type` (`lambda-eval.cpp:2248`). Reproduced on both tiers with a build of `293b7a175`; none of the code involved has changed since.
- With `a = <ul <li "a"> <li "b"> <li "c">>`, `a is <ul; <li>*>` is `false`, but `a is <ul; <p>, <p>, <p>>` is `true`.
- `<ul> is <ul; <li>?>` is `false`: an empty run still needs one child.
- `validate` of an XML file holding three `<li>` fails against `<document; <li>*>` and `<document; <li>+>` ("Element content length mismatch: expected 1, got 3"), while a file holding three `<p>` passes `<document; <li>, <li>, <li>>`. XML input wraps its top-level elements in `document`.
- The [Validator Guide](../doc/Lambda_Validator_Guide.md)'s own `Page` schema relies on runs (`<meta …>*`, `<h1>+`, `<p>*`). Under this check a run counts as one child, and the nested patterns are never reached.

**Why it went unnoticed:** the validator GTests build `TypeElmt`s by hand with `content_length` set to the exact child count they want (`test_validator_features_gtest.cpp`, `test_ast_validator_gtest.cpp`); no Lambda test puts an occurrence inside element content; and the validator targets run outside the baseline (the validator's [LR13-9](Lambda_Issue_Ledger.md#lr13-9), filed as LR12-1). The S2.1.3 and D2.6.6 implementation footnotes (Appendix A of each formal spec) and that LR13-9 entry all record the count check as implemented, without this caveat.

*Fixed 2026-09-25 ([Impl_Element_Type_Sharing P0](<impl/Lambda_Impl_Element_Type_Sharing.md>)):* S11.1.6v3 ruled element content a sequence-pattern slot. The content section now resolves to `TypeElmt::content_list`, a typed `TypeList` filled as a bracket pattern is, and `validate_against_element_type` matches the children with `array_pattern_runs_match` in the fast verdict and with the shared `validate_sequence_pattern` on the reporting path. A second defect under the same symptom: the per-slot shortcut `array_pattern_simple_type_matches` reduced a structural slot (`<p>`, `{y: int}`, `[int, int]`) to a TypeId test, so `[<li>]` matched `[<p>]` as well; only bare kinds take it now. Every reproducer above now answers correctly on both tiers. Tests: `test/lambda/element_content_pattern.ls`; the validator GTests use content patterns instead of counts.

<a id="lr13-11"></a>**LR13-11 · The validator refused every element of a constrained element type (S11.4.6) · FIXED 2026-09-25 (found 2026-09-25)**
The validator enforces a constrained type's base, TE-10's interim. Reached directly, a `TypeConstrained` is read as a TypeType, whose `type` field its `base` overlays. Wrapped in a TypeType, as an element of `Pos[]` or a field of `{v: Pos[]}` is, it stayed a constrained type after `unwrap_type`, and a TypeId compare refused every element: `[1, 2] is Pos[]` was `false` while `let xs: Pos[] = [1, 2]` was admitted.

*Fixed 2026-09-25.* `unwrap_type` (`validator_internal.hpp`) peels constrained layers to the base. The predicate is still not evaluated there (SO9), so `[-1] is Pos[]` is `true`. Fixture `constrained_type_base.ls` §8.

<a id="lr13-12"></a>**LR13-12 · The validator refused a bare datetime, binary or decimal type, read a sized type's size as a type kind, and let `date` admit any datetime (S10.1.1v2, S11.1.1v3) · FIXED 2026-09-26 (found 2026-09-26, auditing S10.1.1v2)**
An expression builds a union's arms bare (`int | datetime` in an expression holds the bare `TYPE_DTIME`), where type syntax wraps them, and `validate_against_type` dispatched only seven bare scalar kinds; the rest fell into "Unsupported type for validation" and admitted nothing, so `t'2025-01-01' is (int | datetime)` built in an expression was `false`, and so were a date against `date | 1` and a decimal against `decimal | 1`. Separately, a sized numeric type stores its size in `kind`, and the base-type walk tested `kind` before the tag, so `i16` read as an occurrence and `i32` as a union: `5i16 is (i16 | string)` and `7i32 is (i32 | string)` were `false` in type syntax too. And the base-type walk matched `date` and `time` by tag, so `t'10:30' is (date | int)` was `true`.

*Fixed 2026-09-26.* A base type's admission once its wrapper is off is one function, `validate_against_base_payload`, which the wrapped path and every bare scalar kind share (a bare literal still compares its value); its construct tests read `kind` only on `LMD_TYPE_TYPE`; `date` and `time` test the value's precision as `fn_is` does. Fixture `type_set_operators_expr.ls` ("a type arm admits as it does in type syntax").

## 13.1 Verification-pass records (LR_03 and ledger hygiene)

<a id="lr03-3"></a>**LR03-3 · MIR-JIT workarounds embedded in the value model · RESOLVED 2026-09-14**
The obsolete `_store_i64`, `_store_f64`, and `push_d_safe` helpers and their JIT
registry imports are deleted. `_barg` is also deleted: it formerly accepted raw
words, fractional floats, null, errors, and arbitrary tagged values as bitwise
integers. The interpreter now always uses the Item-aware bitwise helpers, which
reject noninteger operands. MIR emits a raw word instruction only when both the
numeric classifier and `MirValue` carrier prove a native `int`; guarded or boxed
results route through the shared Item helper instead. This preserves **S4.1.2**
and the representation authority split in **D2.4.1–D2.4.3**.

Regression: `bitwise_invalid_operands.ls` verifies that fractional operands
produce `error()` under eager JIT; `transpile_bitwise.ls`,
`bitwise_lane_preservation.ls`, `sized_numeric_bitwise_go.ls`,
`sized_numeric_ushr.ls`, and `proc_bitwise_int64.ls` preserve valid compact,
sized, and wide-lane behavior.

<a id="lr03-6"></a>**LR03-6 · JS accessor-pair tag overloading · RESOLVED 2026-09-14**
`JsAccessorPair` no longer occupies a fake `LMD_TYPE_FUNC` value lane.
`JsAccessorCell` has the distinct `GC_TYPE_JS_ACCESSOR` collector tag, and an
accessor `ShapeEntry` is a map-private virtual descriptor with
`byte_offset == -1` and a direct cell pointer. The collector follows the
`Map → TypeMap → ShapeEntry → cell` edge; producers use an exact temporary
object root only until that edge is published. Generic Map readers never
decode the cell as an `Item`. Descriptor conversion materializes a fresh
physical lane before accessor→data, and abandons a former data lane for
data→accessor, as required by **D3.4.8**.

Regression: `JsInterpreter.KeepsAccessorCellsVirtualAndAliveAcrossCollection`
forces a collection after accessor installation, and
`JsInterpreter.ConvertsAccessorDescriptorsBetweenVirtualAndDataStorage`
verifies both conversion directions and the `byte_offset` invariant.

<a id="lr03-10"></a>**LR03-10 · A type with no TypeId of its own resolves to the wrong singleton · RESOLVED 2026-09-03**
`lambda_type_node_singleton` (`runtime/ast.hpp`) turns a type-annotation AST
node into the runtime type value both tiers compare against. It arms a short
list of types that need a specific singleton, then falls back to
`base_type(node->type->type_id)` — a lookup keyed on the TAG. The arms exist
precisely because a few types have no tag of their own: `date`/`time` share
`LMD_TYPE_DTIME`, `list`/`number`/`integer` have no runtime tag at all, and the
sized numerics all share `LMD_TYPE_NUM_SIZED`.

Removing `LMD_TYPE_OBJECT` put `object` in exactly that class without adding
its arm. `TYPE_OBJECT` wears the map tag to route through the container
switches, so the fallback handed back the `map` singleton, and `{x: 1} is
object` answered **true** on both tiers while a nominal element answered false.
The helper is shared by T0 and MIR, so the two tiers agreed — with each other,
and not with the ruling. Fixed by adding the `object` arm alongside the others,
and by matching `&TYPE_OBJECT` by pointer identity in `fn_is` ahead of any tag
comparison.

Two things are worth carrying forward. The tag fallback is a **silent** wrong
answer, not a failure: a type that stops having its own tag needs its arm added
in the same change, and the existing arms are the checklist. And the diagnosis
cost more than the fix, because the natural suspicion was the lookup table —
`lookup_base_type_name` was returning the right singleton all along, and the
rewrite happened one layer later, at evaluation. Printing what `fn_is` actually
received, rather than what the table returned, is what closed it.


<a id="lr03-9"></a>**LR03-9 · The C mirror in `lambda.h` never matched `struct Container` · RESOLVED 2026-09-03**
`lambda.h` carries a C mirror of the container structs "for direct field access
optimization", with the comment *"Layout must match the C++ structs in
lambda.hpp exactly"*. It did not, and nothing checked it. The real `Container`
uses eight single-byte fields (`type_id`, `flags`, `array_flags`, `map_kind`,
`cow_state`, two ctor-mask bytes, `reserved_state`), each pinned by its own
`LAMBDA_STATIC_ASSERT`. Every mirror struct instead declared `uint16_t flags`
followed by padding, so alignment put `flags` at offset 2 and the first pointer
field at 16 rather than 8 — a divergence on every one of `Map`, `List`,
`ArrayNum` and `Element`.

It was invisible because nothing in C actually read those members: `gc_heap.c`,
the one C consumer, reaches fields by raw byte offset, and every other consumer
is C++ and sees `lambda.hpp`. So the mirror was dead weight that would have
produced wrong offsets the moment any C code used it by name.

Found by the D2.6.6v2 phase-1 work: adding the first-ever layout assertions to
the mirror failed the build immediately. Fixed by giving all four mirror structs
the exact eight-byte header, and the assertions now stand as the guard. The
general lesson is worth keeping: a hand-written mirror without an assertion is
only accidentally correct, and this one had been wrong for its whole life.


<a id="lr03-8"></a>**LR03-8 · A shape transition on an object drops its nominal record · RESOLVED 2026-09-03**
The shape-transition rebuild in `lambda-eval.cpp` (the `fn_map_set` path,
near the `LMD_TYPE_ELEMENT` branch that allocates a fresh `TypeElmt` and
carries `name`/`content_length`/`ns` across) has no object arm: an object
falls into the plain-`TypeMap` else-branch, which builds a shape with no
`type_name`, no `base`, and no method table. Found by the 2026-09-03 layout
survey as latent — no corpus script extends an object with a new field.

Under S2.1.4 it is a **defect, not an error path**: Lambda objects are open by
default (OB15 part 3), extension is an ordinary member addition, and every
shape reached from a declared shape must share the one nominal record (OB16),
so the value stays an instance of its type and its methods keep resolving.
**Resolved with D2.6.6v2 phase 2.** The nominal record is now a `TypeMap`
field, and both shape-rebuild sites copy it forward: the generic transition in
`fn_map_set`'s rebuild path, and `map_extend_open_shape`, which is the one that
actually produces a grown shape. The second mattered more than expected — until
open-instance extension landed the same day, no code path could reach a grown
nominal shape at all, so the defect was unobservable rather than absent.
Fixture `test/lambda/proc/object_open_instance.ls` pins exactly the check this
entry asked for: extend `<P x: 5>` with `p.z = 9` through a `var`, then confirm
`p is P`, `p.dbl()`, `len(p)`, round-trip printing, and that a grown instance
does NOT equal an ungrown one (S5.4.2v3 compares the full key set). Verified on
both tiers and under forced GC.


## 14. Sibling vibe ledgers (TS, Issues8)

<a id="ts-4"></a>**TS-4 · A named map type on a *local* is a COW value root, not a borrow · RESOLVED 2026-09-14**
The historical report predates the current COW model. A local map binding is a
snapshot by default, not an unconstrained alias; this is the required semantic
behaviour under **D4.4.1–D4.4.2**. The narrowly proven read-modify-write place
case receives a runtime-guarded borrow only when it satisfies **D4.4.4**;
otherwise the snapshot remains required.

The old correctness repro no longer reproduces: release `splay2` completes
with `splay: PASS (nodes=8000)`. Its rotations now rebuild and store back
subtrees rather than relying on mutable cursor aliases. The retained release
`raytrace3d2` also passes (`pixels=7200`, 11.98 ms internal timing). A
release-only one-million-iteration A/B probe of
`var tri: Triangle = scene.triangles[...]` measured typed reads at 18.8 ms and
17.2 ms, versus 40.8 ms and 46.7 ms untyped, with equal results. Thus the
reported 120 s versus 80 ms regression is absent on the current tree.

<a id="ts-8"></a>**TS-8 · No arity overloading for user definitions · RESOLVED (not a defect — ruled S12.3.6)**
`impl/Lambda_Issue_Type_Support (retired).md`. `pn f(a)` plus `pn f(a, b)` in
one scope gives `error[E209]: duplicate definition of 'f' in the same scope`.
**Ruled 2026-08-25 as intended behaviour** (`Lambda_Formal_Semantics.md`
S12.3.6, spec v15.2.0): a name binds to exactly one function, following
ECMAScript per S1.11.

The entry framed this as an asymmetry against the builtin registry, which *is*
keyed on `(name, arity)`. That keying is a **dispatch optimization** — it lets
an intrinsic select a specialized row without a runtime arity branch — not a
language rule, so builtins are not overloadable in source either and the
asymmetry is only apparent.

Nor is expressiveness lost: Lambda already covers the intent with **optional
parameters**. Verified — `pn f(a, b?)` accepts `f(1)` → `[1]` and `f(1, 2)` →
`[1, 2]`, the `fn` form behaves the same, and `g(1, 2, 3)` past the declared
slots is rejected. `pn f(a)` and `pn f(a, b)` are one `pn f(a, b?)`.

*Adjacent nit, since fixed 2026-08-25:* the over-arity diagnostic counted only
required parameters — `fn g(a, b?)` with three arguments reported "expects **1**
argument, got 3". It now reports the range, which matters more once S12.3.6
makes optional parameters the sanctioned alternative to overloading:

| Signature | Call | Message |
|---|---|---|
| `fn g(a, b?)` | `g(1,2,3)` | expects **1 to 2** arguments, got 3 |
| `fn g(a, b, c?)` | `g(1)` | expects **2 to 3** arguments, got 1 |
| `fn add(a, b)` | `add(1)` | expects 2 arguments, got 1 *(unchanged)* |
| `fn h(a)` | `h(1,2)` | expects 1 argument, got 2 *(unchanged)* |
| `fn v(a, ...)` | `v()` | expects 1 or more arguments, got 0 *(unchanged)* |

`build_ast.cpp` `lambda_ast_validate_call_arguments`; covered by
`test/std/negative/wrong_arg_count_optional.ls` +
`NegativeScriptTest.OptionalParamArityReportsARange`.


<a id="i8-mapkey"></a>**Issues8 · Double-quoted map keys rejected · RESOLVED (not a defect — doc was wrong)**
Every double-quoted map key fails: `{"key": 1}` gives
`error[E100]: expected an expression` at the `:`, while `{'key': 1}` and
`{key: 1}` both work. Ruled 2026-08-25: **the parser is correct** — a map key is
a *symbol*, written bare when it is a name and single-quoted otherwise; a
double-quoted string is not a key form. The Issues8 entry framed this as a
hyphen problem, but hyphens were never the issue: `{'other-key': 2}` already
works. The real defect was the documentation — `doc/Lambda_Data.md` presented
`{"string_key": 1, symbol_key: 2}` as a valid "mixed key types" example, and
that line did not parse. It now reads
`{'symbol-quoted': 1, name_key: 2}` under the comment *"Keys are symbols: quote
one when it is not a bare name"*, which does parse. The two `"..."` key hits
elsewhere in `doc/` are inside ```json fences and are correct as JSON.


<a id="i8-dqdiag"></a>**Issues8 · Double-quoted map key gives a generic diagnostic · RESOLVED 2026-08-25**
`{"key": 1}` reported `expected an expression` at the `:`. It now says:

> `a map key is a symbol, not a string: write a bare name like {key: 1}, or
> single-quote it when it is not a name like {'data-node-id': 1}`

**Why it was generic.** The brace resolver decides by interior (S16.4.1v2), and
a string is not a key — so `{"k": 1}` was read as a **block**, parsed `"k"` as
an expression statement, and failed on the following `:`. No amount of work in
`parse_map` could have helped, because control never reached it.

**Fix.** `braced_expression_is_map` now also routes `{ STRING : … }` to the map
parser, which rejects the key with the message above. That changes no accepted
program — `{"k": …}` has no valid reading as either a map or a block — and one
message covers every brace position, since `control_body_brace_is_map`
delegates to the same probe. Verified unchanged: `{key: 1}`, `{'a-b': 2}`,
`{a: 1, b: 2}`, `{}`, and the block forms `{ let x = 1; x }`,
`{ "just a string" }` → `"just a string"`, `{ 1 + 2 }` → `3`. Covered by
`test/std/negative/map_key_double_quoted.ls` +
`NegativeScriptTest.DoubleQuotedMapKeyNamesTheRule`.


<a id="issues0-9"></a>**Issues0 #9 · ShapePool keys on a hash without comparing field names · RESOLVED 2026-08-27**
`vibe/impl/Lambda_Issues0 (fixed).md` #9 — deferred there, and the archive's
`(fixed)` name hides it. `shape_pool.cpp:22` builds the pool key with
`HASHMAP_DEFINE_FIELD3_KEY(shape_entry, ShapePoolEntry, signature.hash,
signature.length, signature.byte_size)`. Two different shapes that collide on
hash **and** match on field count and byte size are treated as identical, so one
map's shape is reused for another and fields are read from the wrong offsets —
silent data corruption. Low probability, high blast radius.

The fix keeps the signature as a fast routing key and wires the existing
`shape_pool_shapes_equal` comparison into the hashmap identity check. Lookup
uses a stack-only probe, so repeated lookups do not consume arena storage; the
element name is retained as existing cache metadata so element signatures are
confirmed as well. This implements the structural identity required by
**D3.4.2** and exact name identity in **D3.4.4v2**.

Regression: `NamespaceTest.ShapePoolCollisionDoesNotAliasDifferentFieldNames`
uses the supported `NULL`-name normalization collision and verifies that
different shapes remain distinct while identical shapes are still reused.


<a id="lint-e1"></a>**Lint E1 · Unchecked allocation dereference in `build_ast.cpp`, now invisible to cppcheck · RESOLVED 2026-08-27**
`impl/Lambda_Issues4_Lint (retired).md` E1. The root cause was seven literal
source-copy sites treating the custom `mem_alloc` contract as non-fallible:
the four manual copies listed by the retired lint entry plus three
`strview_to_cstr` callers. `mem_alloc` returns NULL under the
`memtrack_fault_should_fail()` injection hook and on a failed `malloc`, so
each unchecked copy was reachable rather than theoretical.

The sites now share `ast_copy_source_text`, which checks the allocation,
records `ERR_OUT_OF_MEMORY` (`E309`) against the literal span, and returns
`TYPE_ERROR`/a failed static-literal probe before the buffer is read. This
follows the checked allocation contract in **D4.2.1v3** and the allocation
failure handoff in **D4.2.2v2**. The consolidation removes the duplicated
copy-and-terminate code; no new data structure or design ruling was added.

Worth recording separately: the migration from `malloc` to `mem_alloc`
**silenced the static analyser without fixing the code**. cppcheck originally
flagged this as `nullPointerArithmeticOutOfMemory`; a 2026-08-25 re-run reports
nothing here, because it does not model the custom allocator. The report's own
suggested remedy — use an allocator that cannot return NULL — was only half
applied.

Regression: `AstBuildAllocationTest.SizedLiteralCopyFailureDoesNotCrash`
arms `memtrack_fault_inject(0)` and verifies direct AST construction reports
the allocation error without crashing. The focused error suite passes 120/120
and `make test-lambda-baseline` passes 3976/3976.


<a id="i8-consoleesc"></a>**Issues8 · Console formatter does not escape quotes or backslashes inside collections · RESOLVED 2026-08-27**
Printing a collection rendered member strings through raw `%s`/`%.*s` paths in
`lambda/core/print.cpp`. That omitted the Lambda escapes for quotes,
backslashes, and control characters, producing ambiguous output such as
`["init: {"flowchart": …}", "back\slash"]`.

The fix consolidates the Item and legacy TypedItem string/symbol paths on one
length-based `print_quoted_text` helper. It emits `\"`, `\\`, `\n`, `\r`,
`\t`, `\b`, and `\f` for collection members while preserving the existing
standalone-string display contract, so a serialized string is not escaped a
second time. No new data structure or design ruling was added; the supported
escape forms follow the Lambda string grammar; the common forms are documented
in `doc/Lambda_Data.md`.

Regression: `NamespaceTest.PrintCollectionEscapesStringContents` verifies quote
and backslash escaping directly through `print_item`. The 42 affected golden
outputs were regenerated from the corrected printer. Focused namespace tests,
the direct Lambda suite (784/784), and `make test-lambda-baseline` pass
3976/3976.


<a id="i8-markcomp"></a>**Issues8 · One-line Mark child comprehensions fail at the closing delimiter · RESOLVED (not a defect — wrong spelling)**
The entry reported `<diagnostics; for (v in vs) v>` failing with `E100` where
"the valid multiline constructor" parsed, concluding that whitespace changes the
grammar. **Both halves of that are wrong.** `;` was never an element separator,
and the multiline form it presents as valid fails identically — verified
2026-08-25.

**S16.9.3** settles the spelling: `;` has exactly one role language-wide,
statement separation; `,` takes over inside elements, and the attribute/content
boundary comma is a **biconditional** — present exactly when the element has
both. `diagnostics` here is the *tag*, not an attribute, so the element is
content-only and takes no separator:

```lambda
<diagnostics for (v in vs) v>                 // correct — parses, <diagnostics 1 2>
<diagnostics kind: "x", for (v in vs) v>      // correct — both present, comma required
<diagnostics; for (v in vs) v>                // E100 — `;` is not an element separator
<diagnostics, for (v in vs) v>                // E100 — no attributes, so no comma
<diagnostics kind: "x" for (v in vs) v>       // E100 — both present, comma missing
```

Whitespace is irrelevant; the spelling was wrong in both layouts. The author most
likely carried `;` over from statement separation — the confusion S16.9.3 exists
to retire. Residue filed separately as [i8-semidiag](#i8-semidiag).


<a id="i8-semidiag"></a>**Issues8 · `;` inside an element gives a generic diagnostic · RESOLVED 2026-08-25**
`<diagnostics; for (v in vs) v>` reported only `expected an expression`, while
the two comma mistakes already named their rule. It now says:

> `';' cannot open element content; a tag is followed directly by its content,
> and ';' only separates one content item from the next`

**Scoped by what is actually legal.** `;` *is* valid between content items —
`<div "a"; "b">` and `<div k: 1, "a"; "b">` both parse — so the check fires only
at the content-start position, where no preceding item exists. The
attribute-bearing form `<div k: 1; "a">` was already covered by the
boundary-comma check and is untouched. `lambda_parser.c` `parse_element`;
covered by `test/std/negative/element_semicolon_opens_content.ls` +
`NegativeScriptTest.ElementSemicolonCannotOpenContent`.


<a id="i8-dynspread"></a>**Issues8 · Spreading a dynamically-constructed map yields a null-key nested map · RESOLVED 2026-09-05**
Any spread-bearing map now retains its keyed AST items for runtime construction;
the builder enumerates both shaped maps and VMaps in source order. VMap symbols
are re-entered through the name lookup seam, matching their String-key backing
store, rather than inserting null values. This follows **S16.8.9**'s runtime
shape rule and later-entry-wins ordering. Regression:
`test/lambda/map_spread_len.ls` spreads `map(["shape", "box"])` and produces
`["box", "a", 2]`.


<a id="i8-attrspread"></a>**Issues8 / Issues5 §23 · Element attribute spread lands the map as a child · RESOLVED 2026-09-05**
Elements use the same runtime keyed-literal path for attribute spreads, so a
dynamic map's fields are installed as attributes and its ordinary content stays
on the content face. This is the same **S16.8.9** source-order construction as
map spread. Regression: `test/lambda/map_spread_len.ls` produces
`["box", "a", ["new"]]` for the dynamic attribute-spread element.

---


## 15. Historical resolved and obsolete records
Kept for provenance: each of these appeared in an `LR_*` "Known Issues" section
and was verified fixed or removed on the date recorded below. Do not re-open
without re-verifying against current source.

### A.1 Compilation pipeline (LR_01)

<a id="lr01-r1"></a>**LR01-R1 · Parallel-compile CPU cap is advisory only · RESOLVED (removed)**
The parallel import-level compile path is gone: no `pthread_create`, `ncpus`, or
`cpu_cap` remains in `lambda/runtime/runner.cpp`, and `PROFILE_MAX_IMPORT_LEVELS`
went with it. The over-subscription hazard and the hardcoded 8 MB worker stack no
longer exist. (Per-script profiling caps survive — see
[LR01-5](<Lambda_Issue_Ledger.md#lr01-5>).)

<a id="lr01-r2"></a>**LR01-R2 · Precompile reversal coupling · RESOLVED (removed)**
`precompile_imports` no longer exists anywhere in `lambda/`. The fragile contract
between its slice reversal / index renumbering and `run_script_mir`'s
reverse-order import init is gone with it.

<a id="lr01-r3"></a>**LR01-R3 · `sys://` paths in maps/elements are never resolved · RESOLVED 2026-08-26**
`resolve_sys_paths_recursive` (`lambda/runtime/runner.cpp`) now walks map and
object fields through `map_shape_field_to_item`, and walks both element
attributes and children. This preserves the packed-shape ABI described by
`D3.4.1`; the old raw map-data walk was the source of the csv-related crash
that had suppressed this traversal. Resolved paths and their nested results
are recursively visited under the `S2.4.1v2` path contract. A nested map/element
probe now returns resolved values, and `make test-lambda-baseline` passes
3914/3914.

<a id="lr01-r4"></a>**LR01-R4 · Unescaped LaTeX bridge filename · RESOLVED 2026-08-26**
The LaTeX-to-HTML bridge now uses `lambda_string_literal_escape` and sizes its
script buffer from the escaped input instead of interpolating into a fixed
4096-byte array. This keeps source paths data rather than Lambda source, as
required by `S1.8`, and matches the PDF bridge's ownership and sizing pattern.
The normal smoke reaches the existing LaTeX package import-resolution failure;
the bridge construction itself is now source-safe and dynamically sized.

<a id="lr01-r5"></a>**LR01-R5 · `target_equal` compares hash-only · RESOLVED 2026-08-26**
`target_equal` retains the hash as a fast rejection, then compares target type,
scheme, and canonical URL/path content. Hashes are therefore not identity;
this follows `S2.4.2v4`. `Target_HashCollisionIsNotEqual` forces equal hashes
for two different URLs and passes in the namespace suite (38/38).

### A.2 Parsing & AST construction (LR_02)

<a id="lr02-r1"></a>**LR02-R1 · Unknown binary operator defaults to `OPERATOR_ADD` · RESOLVED**
`lambda_binary_operator_from_spelling` (`build_ast.cpp:3683`) now `return
false` on an unrecognized spelling (`:3717`), and the caller records a real
diagnostic — `record_semantic_error_span(tp, span, ERR_INVALID_OPERATION,
"unknown binary operator '%.*s'")` — and sets `node->type = &TYPE_ERROR`
(`:7382`–`7388`). Grammar/builder drift now fails loudly instead of silently
compiling as `+`.

<a id="lr02-r2"></a>**LR02-R2 · Numeric promotion relies on enum order · RESOLVED**
`std::max(left_type, right_type)` is gone from `build_ast.cpp` entirely.
Promotion now runs through the shared classifier in
`lambda/runtime/lambda-number.hpp` — `lambda_numeric_classify(family, kind_l,
kind_r)` over an explicit `LambdaNumericKind` enum
(`INT`, `INTEGER`, `FLOAT`, `DECIMAL`, `I8`…`U64`, `F16`, `F32`) — so reordering
`TypeId` no longer changes arithmetic results, and `float ∥ integer` and sized
lanes are representable.

<a id="lr02-r3"></a>**LR02-R3 · Decimal / `integer` result inference is incomplete · RESOLVED**
Superseded by the same classifier: `lambda_numeric_kind_from_type(Type*)` reads
the full `Type*` rather than reducing to `TypeId`, and `LAMBDA_NUM_INTEGER` and
`LAMBDA_NUM_DECIMAL` are distinct kinds
(`lambda-number.hpp:12`, `:14`). Arbitrary-precision integer results are no
longer conflated with ordinary decimal results.

<a id="lr02-r4"></a>**LR02-R4 · `raise` is not scope-checked · RESOLVED**
The `// TODO: Also allow in pure functions with error return type` is gone.
`build_raise_node_from_parts` (`build_ast.cpp:8011`) is scope-agnostic by
design; correctness is now enforced by the error-type machinery —
`TypeFunc::can_raise` (`:4633`), divergence classification (`:4099`–`4100`),
`validate_function_return_contract` (`:5166`, called `:8468`) and
`validate_explicit_return_boundaries`. This is the TE-16 `T^E` / `expr ^ { … }`
work landing; see [Type support enforcement design].

<a id="lr02-r5"></a>**LR02-R5 · `list` expressions forced to `&TYPE_ANY` · RESOLVED**
`direct_list_node` (`build_ast.cpp:7038`) now propagates a single item's own
type and only falls back to `set_type_any(tp, ANY_LIST)` for the general case
(`:7046`–`7047`). The adjacent `// Fix scope restoration` marker is gone;
declaration-bearing blocks take a separate, explicitly scoped path (`:7205`ff).

<a id="lr02-r6"></a>**LR02-R6 · Line-start fluent `.method(` rejected when the member name is a type keyword · RESOLVED 2026-08-24**
*Was LR02-11, found during the doc sweep; fixed the same day.*

**Symptom.** A fluent chain broken across lines was rejected whenever the member
name was a type keyword — `.map(`, `.int(`, `.string(`, `.float(`, `.array(`,
`.element(`, `.symbol(` — while `.len(`, `.sum(`, `.sort(`, `.filter(` and the
rest were accepted. The same expression on one line always worked.

**Root cause.** Two predicates that must agree had drifted apart. The member-name
parser `parse_path_segment` (`lambda/runtime/parser/lambda_parser.c`) accepts
`token_is_key(...)`, which includes `LAMBDA_TOK_BASE_TYPE`; the S16.2.4 line-start
carve-out in `parse_postfix` tested `parser->next.kind == LAMBDA_TOK_IDENTIFIER`
alone. A type keyword lexes as `LAMBDA_TOK_BASE_TYPE`, so the guard rejected
exactly the chains the member parser would have accepted.

**Fix.** The carve-out now calls `token_is_key(parser->next.kind)` — the same
shared set — so the guard admits precisely what the member parser admits.
`INTEGER`, `SLASH`, `PARENT` and `STAR_STAR` stay out because each keeps a
non-member reading at line start; `.5` therefore remains dual-role. A comment at
the fix point records the invariant so the two cannot silently desync again.

**Front-end divergence closed.** The Tree-sitter reference grammar already
accepted all these forms, so this was a C-parser-only defect and the two front
ends disagreed, against §4.4. They now agree.

**Verified.** Three ratcheting cases added to *both* harnesses
(`test/c_s16_conformance.sh`, `test/ts_s16_conformance.sh`): C 123→**126/126**,
Tree-sitter 118→**121/121**. Reverting the one-line fix fails exactly those three
and nothing else, so the ratchet bites. `make test-lambda-baseline`:
**3867/3867**.


<a id="lr02-r7"></a>**LR02-R7 · `pn ... =>` accepted by the C parser only; arrow-body errors rewritten into the element-ambiguity message · RESOLVED 2026-08-24**
Two defects closed by the S16.6.6/S16.6.7 ratification. (1) `parse_function_declaration` accepted `pn p() => expr` and `pn p() => { ... }` while the Tree-sitter reference grammar rejected both — a §4.4 front-end divergence with the C parser as the outlier; now rejected with `a procedure body is a statement block — write 'pn name() { ... }'`. Corpus cost: 1 doc site (`doc/Lambda_Procedural.md`), 0 tests. (2) The `runner.cpp` relation walk-back matched the `>` of `=>`, rewriting every arrow-body diagnostic into `'<' and '>' are ambiguous with element syntax` — `(x) => return x` produced that message instead of the parser's own; the walk-back now skips `=>` and `|>`. Harness: C 138/138, TS 128/128.

<a id="lr02-r8"></a>**LR02-R8 · Reference grammar lexed `return`/`break`/`continue` as identifiers in expression position · RESOLVED 2026-08-24**
*Was LR02-12, opened the same day while implementing S16.6.6 and closed the same day.*

**Symptom.** `if (c) return -1` parsed in the Tree-sitter reference grammar as a
**subtraction from a variable named `return`** — a silent misparse, strictly
worse than acceptance, and invisible to the compare lane because production
rejected it.

**Root cause.** Tree-sitter's lexer is context-aware and `word: $ => $.identifier`
enables keyword extraction: a keyword token is emitted only where it is
syntactically valid, otherwise the word falls back to `identifier`. In an
expression position `return_stam` is not valid but `identifier` is, so the
fallback fired. No grammar-only fix exists for this in tree-sitter 0.24 —
per-position reserved words arrived in 0.25's `reserved` sets, and the repo
pins 0.24.7.

**Fix.** A zero-width external `_expr_body_start`, withheld by the scanner when
the word at the cursor is `return`/`break`/`continue`, required at the four
S16.6.6 body positions (paren-form `if`/`for` body, `else` body, `case T:` arm,
`=>` arrow body — eight grammar sites). Withholding the token kills the
expression-body alternative, which is exactly the rejection required. The guard
is **scoped to those positions rather than to every identifier**, keeping the
§7.17 scanner blast radius small, and is **stateless** — a pure function of the
lookahead — so it carries none of that note's stale-carry hazard. The helper is
`inline:`d: as a real nonterminal it forced a reduce conflict against a trailing
binary operator (`=> x > y`).

**Verified.** C 140/140 and Tree-sitter 135/135 on identical case sets (the five
divergence cases moved back into the TS suite, plus controls for a
keyword-prefixed identifier `returnValue` and an arrow body with a binary tail).
Full 700-file `.ls` corpus cross-check: **zero movement** — the same 76
pre-existing failures before and after, measured by regenerating both ways.
`make test-lambda-baseline` 3868/3868.

*Superseded 2026-09-24.* The grammar moved to tree-sitter 0.25's `reserved`
sets (see LR02-14), which reserve `return`, `break` and `continue` everywhere
an identifier is expected. They have no action in an expression, so the four
body positions reject them without a guard, and `_expr_body_start` retired.
The guard had also swallowed any comment in front of an unbraced body into its
padding, so those comments now appear in the tree.

<a id="lr02-r9"></a>**LR02-R9 · `for (k, v at c)` bound both names to the key · RESOLVED 2026-08-24**
*Was LR02-8, found during the verification pass; closed once S8.1.3 settled what the form means.*

**Symptom.** `for (k, v at {a: 1, b: 2}) k ++ v` yielded `['aa', 'bb']` — the value
name aliased the key. A **silent wrong answer**: the shape was right, only the
binding wrong. The `where` variant was worse still — `where v > 2` compared the
key against a number, so the filter silently returned `[]`.

**Root cause.** `AstLoopNode.name` holds the LAST binding and `index_name` the
first, so in the paired form `name` is the value slot and `index_name` the key
slot. `at` set `key_only`, which redirects `name` to the key — correct for the
single-name `for (k at c)`, but in the paired form it overwrote the value slot
while `index_name` was independently getting the key.

**Fix.** Gate `key_only` on the absence of `index_name` (`build_ast.cpp`).
`key_filter` is deliberately untouched: that is what restricts the **member set**
to name keys, so the axis still means something — paired `at` on an element
yields attribute pairs only, and on an array yields nothing (an `IntKey` is not
a name, S8.2.2v2). Both execution tiers read the same flag, so one fix covers
MIR Direct and the interpreter.

**Ruling first, then fix.** The form was unspecified — S8.1.1 paired `at` with a
single name, S8.2.1v2 specified the paired form only for `in`, and SO12 recorded
the question as open. **S8.1.3** now rules axis and arity independent: the axis
picks which members are walked, the arity picks the projection. SO12 is closed.

**Verified.** The three worked examples in `doc/Lambda_Expr_Stam.md` had never
been run and all three were wrong; they now match. Regression test
`test/lambda/for_at_pairs.ls` + `.txt` pins all six shapes (paired/single `at`,
`where`, element attrs-vs-children, empty array). Baseline 3868/3868.

<a id="lr02-r10"></a>**LR02-R10 · Spread does not expand into a call's argument list · CLOSED 2026-08-25 (won't fix; `call()` supersedes)**
*Was LR02-10.* Ruled **container-only** as S12.3.5 rather than implemented.

**Why not.** Expansion needs call-site syntax and semantics of its own, costs
the static arity check S12.3.1 relies on (a spread's length is unknown until run
time), and silently diverts calls to the dynamic ABI — a same-source-shape perf
cliff. Demand was thin: 7 variadic functions and 24 `varg()` sites in the whole
test corpus, **0** in `lambda/` packages. And `varg()` returns an *array*, so
forwarding already worked for any callee taking a collection; the only shape
with no workaround was forwarding to a callee that is itself variadic.

**What replaced it.** `call(f, args)` (S12.3.4) — one registry row over the
existing `fn_call_into` dynamic ABI, versus three sites that would have had to
agree forever. Honestly dynamic, so no static guarantee is silently lost, and
strictly more general: it forwards to fixed-arity and variadic callees alike.
`fn outer(...) => call(inner, varg())` is the motivating case and works on both
tiers. S12.1.4 admits `call` as Lambda's first effect-polymorphic function,
the first partial answer to SO28.

**Follow-through.** Docs corrected in `Lambda_Expr_Stam.md` (the "not yet
implemented" spread note became the container-only ruling), `Lambda_Func.md`,
and `Lambda_Sys_Func.md` (new Dynamic Application section).
`test/std/core/functions/variadic_args.ls` — which never parsed, using a third
spelling `values...` — is repaired and now covers the forwarding case.
Regression test `test/lambda/call_dynamic_apply.ls` + `.txt`. Residue was tracked
as LR02-13 and is now resolved in [LR02-R13](#lr02-r13).

<a id="lr02-r13"></a>**LR02-R13 · `call()`'s runtime colour check selected the wrong registry row · RESOLVED 2026-08-26**
The `call` registry contains both `SYSFUNC_CALL` and `SYSPROC_CALL` with the
same name and arity. Lookup could return the procedure row even in a function
scope, while the effect-row resolver only corrected the function row; a
dynamically selected `pn` could then run from `fn`. The resolver now normalizes
either row to the enclosing `fn`/`pn` colour. This implements `S12.1.4` and
`S12.3.4` without changing closure construction. The tracked dynamic-procedure
regression passes, as does the full baseline.

<a id="lr02-r18"></a>**LR02-R18 · Bare `pn` method reference · RESOLVED 2026-09-05**
`AstFieldNode::is_proc_method_reference` marks a resolved dotted `pn` member;
the call builder clears that mark only when it consumes the member as the direct
callee. The shared final AST pass rejects every remaining mark with E224 before
either T0 or MIR lowering. This implements **S12.3.3v2** and **D2.6.7** without
changing the runtime member lane that valid `pn` calls need. Regression:
`NegativeScriptTest.SemanticError_ProcMethodCannotBeTakenAsValue`; the retained
positive member-value fixture passes on both JIT and T0.

### A.3 Value & type model (LR_03)

<a id="lr03-r4"></a><a id="lr10-5"></a>**LR03-R4 · `INT64_ERROR == INT64_MAX` collision · RESOLVED 2026-09-09**
`INT64_ERROR` is removed. `item_try_to_int64` and decimal `*_try_to_int64`
report success separately, so `9223372036854775807i64` remains a finite `i64`
while failed conversion returns `ItemError`. `fn_int64` now has a boxed `Item`
ABI, and MIR preserves that result until the existing error boundary; `it2l`
remains a legacy `IntLane` accessor and never represents a conversion failure.
This follows **S7.10.3**, **D2.4.3**, and the private-lane rule in **D2.2.2**.
Regression coverage: `test/lambda/int64.ls`, `ItemRepresentation.Int64AlwaysUsesPointerBackedPayload`,
`LambdaDecimal.QuietInt64ExtractionRejectsOverflowAndInvalidComparison`, and
`scalar_home_donation.mir-check`; `make test-lambda-baseline` passes 5087/5087.

<a id="lr03-r1"></a>**LR03-R1 · Two parallel type vocabularies · RESOLVED**
The `TypeSchema`/`SchemaTypeId` vocabulary in `schema_ast.hpp` was dead code and
has been removed, leaving `Type*` as the runtime's single type vocabulary. See
[LR13-R1](#lr13-r1).

<a id="lr03-r2"></a>**LR03-R2 · `vmap_from_array` dead branch · RESOLVED**
The duplicated `type_id != LMD_TYPE_ARRAY && type_id != LMD_TYPE_ARRAY` guard is
gone. `lambda/runtime/vmap.cpp:330` is now a single
`if (type_id != LMD_TYPE_ARRAY)`, with a comment (`:327`–`329`) explaining that
lists are `LMD_TYPE_ARRAY` at runtime and that `LMD_TYPE_ARRAY_NUM` is
*intentionally* rejected because its packed layout is unsuitable — so the second
clause was not a missing `ARRAY_NUM` case after all.

<a id="oi1-r1"></a><a id="lr03-r1-oi1"></a>**OI-1-R1 / LR03-R1 · value equality, strict structural equality, and VMap keys · RESOLVED 2026-09-08**
**S5.2.1v2** now makes Lambda numeric equality exact mathematical-value equality
across numeric ranks and makes decimal scale non-semantic. **S8.2.1v4** narrows
the public VMap key domain to `NameKey` and `IntKey`: string/symbol spellings
share a name key, while a finite exactly integral float or decimal canonicalizes
with integer ranks (`1`, `1.0`, `1n`, `1.0m`, and `1.00m` are one key).
Fractional or poison numeric keys fail checked construction/writes and have a
total `null` read; host raw backing stores retain their interop key relation.

`fn_eq_strict` now serves `item_deep_equal` for Radiant no-op elision. It keeps
numeric ranks and sequence families distinct and returns unequal at its depth
cap without publishing a runtime error. Public VMap insertion validates and
canonicalizes keys before mutation; the MIR Direct COW path preserves the prior
binding if a rejected write is handled. Regressions cover all admitted numeric
ranks and decimal scales, name-key normalization, invalid-key recovery, and
strict no-promotion equality. Verified by `make test-lambda-baseline`:
5,083/5,083 combined tests and 2,979/2,979 Lambda runtime tests; and by
`make test262-baseline`: 40,261/40,261 baseline tests with zero regressions.

### A.4 Strings, symbols & vectors (LR_05)

<a id="lr05-r1"></a>**LR05-R1 · `ArrayNum ==` is representation-sensitive · RESOLVED**
`array_num_eq` (`lambda/runtime/lambda-eval.cpp:1852`, called `:2220`) checks
N-D shape as structure, value-compares element-wise across differing element
types (avoiding double-promotion precision loss on high int64/uint64 bits),
compares float arrays element-wise (NaN-correct), and memcmps same-type compact
arrays with the per-type element width from `ELEM_TYPE_SIZE`. The historical
`sum(abs(a-b)) == 0` workaround is no longer needed.
⚠ Related caution from [Typed Array 4 implementation]: `ArrayNum ==` remains
*representation-sensitive at the benchmark level* — keep goldens in step.

<a id="lr05-r2"></a>**LR05-R2 · Two string orderings coexist · RESOLVED (stale at the operator level)**
Every language comparison is raw byte order and mutually consistent: `==`
(`fn_eq`), ordered `<`/`>` (`fn_lt_scalar`/`fn_gt_scalar` — `memcmp` plus length
tiebreak), and the sort-facing total order (`total_byte_cmp`). The utf8proc
casefold comparators are used only by the markup parser for case-insensitive
tag/attribute matching; the dead Item-level wrappers were removed in
[LR05-R3](#lr05-r3).

<a id="lr05-r3"></a>**LR05-R3 · Dead `*_comp_unicode` Item wrappers · RESOLVED 2026-09-05**
The five unused Item-level Unicode comparison wrappers and their declarations
are deleted. String-level casefold helpers remain markup-only, so Lambda's core
equality and order continue to follow **S6.2.2v3** bytewise UTF-8 semantics.

<a id="lr05-r4"></a>**LR05-R4 · `index_to_item` truncates int64 → int · RESOLVED 2026-08-26**
`index_to_item` now passes its `int64_t` index directly to the 64-bit `i2it`
lane, so the `~#` value emitted by mapping pipes is not narrowed through a C
`int`. This preserves the index carrier required by `S10.1.2`; the baseline
passes 3914/3914.

<a id="lr05-r5"></a>**LR05-R5 · `fn_label` flood-fill workspace bypassed the runtime allocator · RESOLVED 2026-08-28**
The flood-fill workspace now uses the existing checked `mem_alloc`/`mem_free`
path with `MEM_CAT_TEMP` instead of raw `malloc`/`free`. This keeps temporary
allocation failure and ownership tracking aligned with **D4.2.1v3** and
**D4.2.2v2**. Regression: `RuntimeShapeTransition.LabelStackAllocationFailureReturnsError`;
the representation suite passes 29/29 and the Lambda baseline passes
3977/3977.

### A.5 C transpiler — legacy C2MIR (LR_06)

<a id="lr06-r1"></a><a id="lr06-r1r9"></a>**LR06-R1 … LR06-R9 · All nine issues · RESOLVED (backend deleted)**
`lambda/transpile.cpp`, `transpile-call.cpp`, `lambda-embed.h`, and the
`jit_compile_to_mir` entry in `mir.c` have been removed from the tree. No core
or Jube build defines `LAMBDA_C2MIR`; `lambda/main.cpp` does not parse a
`--c2mir` flag; no test target builds it. The only surviving `c2mir` references
are in the vendored MIR archive build rules (`Makefile:219`–`222`, `:380`,
`:396`, `:402`), which Lambda does not invoke. Per CLAUDE.md rule 14 the path is
frozen; per this verification it is absent. Retired with it:

1. `#ifdef LAMBDA_C2MIR`-gated stale-by-default backend.
2. GROUP BY not implemented in `transpile_for`.
3. Typed-array support diverges from MIR Direct in C2MIR's favour — *note:* the
   underlying MIR Direct gap survives independently as
   [LR07-3](<Lambda_Issue_Ledger.md#lr07-3>), but there is no longer
   a more-complete backend to port from.
4. `_store_i64`/`_store_f64` SSA-reorder workaround with `MAX_LOOP_ASSIGN` cap —
   its runtime-side helper residue was subsequently removed as
   [LR03-3](#lr03-3).
5. `is_idiv_expr` boxed-result / INT-static-type mismatch.
6. `MAX_INFER_PROCS 32` / `MAX_INFER_CALL_SITES 64` silent inference truncation.
7. TCO iteration ceiling — *note:* survives on the MIR Direct side as
   [LR07-13](<Lambda_Issue_Ledger.md#lr07-13>).
8. Documentation-vs-code divergence on `fn_band`/`fn_bor` calling convention.
9. Two compile stages, two failure surfaces (`temp/_transpiled*.c` as the
   diagnostic of record).

### A.6 MIR Direct transpiler & JIT (LR_07)

<a id="lr07-r1"></a>**LR07-R1 · Indirect calls cap at 3 arguments · RESOLVED**
The `mir: calls with >3 args not yet fully supported` log and its wrong-value
return are gone. `transpile_call`'s dynamic path
(`transpile-mir.cpp:18132`ff) now dispatches
`fn_call0_into` / `fn_call1_into` / `fn_call2_into` / `fn_call3_into` for
0–3 args and **`fn_call_into` for any higher arity** (`:18152`), with each
argument boxed and rooted through `create_gc_root_slot` before the call.

<a id="lr07-r2"></a>**LR07-R2 · Parallel inference metadata tables · RESOLVED**
The `param_types[16]`, `param_mir[16]`, fixed alias-name table, and copied
32-entry parameter-name table are retired (no occurrences remain). Per-parameter
inference lives on the AST / function-analysis records. Core source arity is
capped only by the intentional `LAMBDA_MAX_FUNCTION_ARGS` language limit;
LambdaJS source formals stay dynamically represented. Remaining fixed
source-name staging buffers are tracked in
`vibe/Lambda_Design_Function_Arg.md`.

<a id="lr07-r12"></a>**LR07-R12 · Magic JIT layout offsets · RESOLVED 2026-09-05**
The JIT's `EvalContext.heap` and `Heap.gc` hops now derive from `offsetof` once,
with layout assertions; the remaining equivalent `64`-byte runtime-state load
uses the same named offset. Generated MIR no longer inherits these struct
positions as literals.

### A.7 Memory management & GC (LR_08)

<a id="lr08-r8"></a>**LR08-R8 · Dead free/frame stubs · RESOLVED 2026-09-05**
The unreferenced `free_item`, `free_container`, `frame_start`, and `frame_end`
no-ops and the lone public declaration are removed. Current ownership is the
precise GC and root-frame model required by **D1.5**; no compatibility caller
remained in the tree.

### A.8 Error handling (LR_10)

<a id="lr10-r1"></a>**LR10-R1 · Error code / table drift · RESOLVED**
`ERR_RETURN_OUTSIDE_FUNCTION` (227) and `ERR_UNHANDLED_ERROR` (228) now have
rows in `error_code_table[]`
(`lambda/runtime/lambda-error.cpp:128`–`129`), matching the enum
(`lambda-error.h:98`–`99`). `err_code_name`/`err_code_message` resolve them
instead of returning `"UNKNOWN_ERROR"`. There is still no compile-time check
that enum and table agree, so the two-places rule stands as a maintenance note.

<a id="lr10-r2"></a>**LR10-R2 · `err_free_stack_trace` leaks strdup'd native frame names · RESOLVED**
`err_free_stack_trace` (`lambda-error.cpp:1178`–`1188`) now frees the duplicated
name for native frames before freeing the node:

```c
// native frame names are duplicated during capture; Lambda frame names are debug-table owned.
if (trace->is_native && trace->function_name) mem_free((void*)trace->function_name);
```

Lambda-JIT frames still point at table-owned names, so the ownership split is
now explicit and correct.

<a id="lr10-r3"></a>**LR10-R3 · Release stack-trace frame counter · RESOLVED 2026-09-05**
`total_frames_found` and both increments are now ordinary code rather than
depending on release logging macro elision. The diagnostic path is build-mode
independent, preserving the error information expected by **S7.4.4**.

<a id="lr10-r4"></a>**LR10-R4 · Mismatched stack-trace depths · RESOLVED 2026-09-05**
All ordinary Lambda error paths use
`LAMBDA_ERROR_STACK_TRACE_DEFAULT_MAX_FRAMES` (64), the same default used by
raw and materialized capture. `set_runtime_error_no_trace` remains the explicit
low-stack escape hatch. Regression:
`StackTraceTest.RawStackTraceUsesSharedDefaultDepth`.

### A.9 Mark data API (LR_11)

<a id="lr11-r1"></a>**LR11-R1 · Stale `.bak` in tree · RESOLVED (for Lambda sources)**
`lambda/mark_editor.cpp.bak` is gone, as are the sibling Lambda-side `.bak`
files. The only remaining `.bak` files are inside the **vendored**
`lambda/tree-sitter-typescript/` import
(`define-grammar.js.bak`, `src/grammar.json.bak`, `src/parser.c.bak`), which
CLAUDE.md rule 16 puts off limits for in-place edits — they are upstream
artefacts, not Lambda drift.

<a id="lr11-r6"></a>**LR11-R6 · Conservative safety analysis (adjacent) · RESOLVED 2026-09-10 (record was never live)**
The entry claimed `function_needs_stack_check` was hard-`true` and
`function_is_tail_recursive` hard-`false`, so TCO existed but was switched off
at the gate. **Both functions were dead code when the claim was written.** At
the ledger's own verification commit `c568f0f93`, `git grep function_needs_stack_check`
matched only `doc/dev/lambda/LR_11` and `LR_12` plus the definition itself — no
call site anywhere in the tree — while the live eligibility test `should_use_tco`
was already wired into both lowering paths (`transpile-mir.cpp:24580`,
`interp_plan.cpp:1676`). The doc sweep read a vestigial function and inferred a
gate that no code consulted.

Commit `8d44a6ca3` (2026-09-01) deleted the whole vestige — `SafetyAnalyzer`,
`analyze_function_safety`, `function_needs_stack_check`,
`function_is_tail_recursive` — leaving `safety_analyzer.cpp` as pure tail-call
analysis. TCO is live on both tiers today: `transpile-mir.cpp:27906` wraps an
eligible body in the TCO loop, and `interp_plan.cpp:1693` marks tail calls for
T0. C-stack overflow is caught by the guard-page/signal handler in
`lambda-stack.cpp`, not by a per-function emitted check, so "every user function
pays for a stack check" was never true either.

**Live residue, re-filed rather than closed:** `is_tco_function_safe` is still
defined and declared but has **no caller** — the "all recursion is tail
recursion, so this frame cannot grow" conclusion is computed and discarded. That
is the surviving half, tracked as [LR07-13](Lambda_Issue_Ledger.md#lr07-13)
alongside the `LAMBDA_TCO_MAX_ITERATIONS` ceiling it would justify removing.

### A.10 Schema validator (LR_13)

<a id="lr13-r1"></a><a id="a8-schema-validator-lr_13"></a>**LR13-R1 · The dead unified-schema model · RESOLVED**
`schema_builder.cpp` (which could not compile — it referenced an undefined
`VariableMemPool` and was excluded from every build target) and `schema_ast.hpp`
were deleted, along with their three stale `exclude_source_files` entries and
`schema_builder.cpp.bak`. The two surviving structs (`TypeDefinition`,
`TypeRegistryEntry`) moved to `validator/validator.hpp`. This retires the "two
parallel type vocabularies" hazard ([LR03-R1](#a3-value--type-model-lr_03)); the
`TODO` it carried (map fields → runtime shape) went with it.

### A.11 Runtime builtins (LR_09)

<a id="lr09-r1"></a>**LR09-R1 · String-comparison inconsistency · RESOLVED (stale)**
`fn_eq`, `fn_lt_scalar`/`fn_gt_scalar`, and the sort total order all compare
strings by raw bytes and are mutually consistent. The utf8proc casefold
comparators are markup-parser-only and their Item-level wrappers have no callers
([LR05-R3](#lr05-r3)). Any future
collation support must be an explicit opt-in governing equality and ordering
together, not an operator change.

<a id="lr09-r2"></a>**LR09-R2 · `split` does not split on a pattern delimiter · RESOLVED**
Not a missing implementation: `pattern_split` (`re2_wrapper.cpp:1124`) computed
the right segments all along, and `list_push` then merged them back together.
`list_push` concatenates a pushed string onto the previous element unless
the eval context suspends it (`collection_io.cpp:90` — the condition does not
consult `is_content`, so it applies to every string push; the suspension flag
has since been retired, see below). `fn_split`'s
**string** path suspends merging around its own loop (`lambda-eval.cpp:5553`),
but the **pattern** path returns before reaching it (`:5530`, and `fn_split3` at
`:5677`), so every pattern split collapsed into one element. The keep-delimiters
form was the proof: segments *and* delimiters were all produced correctly, then
concatenated back into the input verbatim.

`pattern_split` now owns the suspension via an RAII guard, covering both callers
and restoring the flag on all of its early-return paths.

Fixing that exposed a second, independent defect in the same loop: one `pos`
cursor served as both the start of the pending segment and the resume point for
the next search, so a zero-length match's `pos++` stepped the *segment start*
over a character that then appeared in no segment at all —
`split("ab", \(d*))` returned `["", "", ""]`, losing `a` and `b`. The cursor is
now split into `seg_start` and `search`, and a zero-width advance steps a whole
codepoint so slices stay on character boundaries.

With the segments correct, the zero-width edge was still under-determined —
Python emits leading/trailing empties there, ECMAScript does not. Ratified as
**S17.1.1 / S1.11 (spec v15.1.0, decision record C18): `split` follows
ECMAScript.** The argument was internal rather than comparative: Lambda's own
empty-*string* delimiter already behaved like JS (`split("ab", "")` =
`["a", "b"]`), so following Python would have made the pattern path contradict
its sibling in the same function — the very inconsistency this fix set out to
remove. `pattern_split` now implements ECMAScript's `e == p` rule (a match
ending on the segment start contributes no segment, only advancing the search),
its loop bound is `search < len` rather than `<= len`, and both paths return
`[]` for an empty subject whose delimiter matches empty and `[""]` otherwise.

All 14 edge cases — leading, trailing, no-match, empty subject, empty
delimiter, zero-width, and UTF-8 zero-width — now match Node byte-for-byte, and
the six examples in `doc/Lambda_Sys_Func.md:463`–`468` hold. Covered by
`test/lambda/split_pattern.ls` across both tiers; `doc/Lambda_Sys_Func.md` gains
an edge-case table and the `doc/Lambda_Cheatsheet.md` defect note is removed.
Note the original ledger table's expected value for `split("a1b22c3", \(d+))`
was internally inconsistent — it omitted the trailing empty segment that the
spec and the sibling `\(d)` row require.

Follow-up: `pattern_split` and `fn_split`/`fn_split3` were later converted from
`list_push` + a merging suspension to plain `array_push` (D2.6.5), removing the
RAII guard, both flag set-sites and all six restore points — net −18 lines, and
`split` no longer touches the global flag at all. Output is byte-identical.

<a id="lr09-r3"></a>**LR09-R3 · `varg()` applies content normalization to the argument list · RESOLVED**
A variadic call collected its rest arguments with `list_push`, which applies
S16.7's content rules: `null` is dropped outright (`collection_runtime.cpp:286`)
and a string is concatenated onto the previous element unless the eval context
suspends merging. An argument list is neither the script top level nor a
container, so neither rule had a ruling behind it — and both destroy arity:
`n("a","b")` arrived as `["ab"]`, `n(1,null,2)` as `[1,2]`, and
`n("x",null,"y")` as `["xy"]`, three arguments collapsed into one. Numeric
arguments are unaffected, which is why it survived: every variadic example in
the docs and tests summed numbers, and `len(varg())` was the only quick tell.

Both builders now append with `array_push`, which writes the item verbatim:
`emit_variadic_args` (`transpile-mir.cpp`) for the MIR tier, and the
dynamic-call adapter (`lambda-eval.cpp:1231`) for T0 and `call()`. `array_push`
keeps the same content-list flattening as `list_push`, so the change removes
exactly the normalization this list never wanted and nothing else — an argument
that *is* a content list still arrives as one value
(`n(for (x in [1,2]) x)` → `[[1, 2]]`).

Verified on both tiers, including `varg(i)` indexing, a fixed-plus-rest
signature, an all-`null` argument list, and `call()` (S12.3.4), which shares the
adapter. `test/std/core/functions/variadic_args.ls` loses the note that kept it
to numeric arguments and now covers the string/`null` cases directly.

**Generalized to D2.6.5** (Formal Design v1.27.0): the append API *is* the
choice of content normalization — `list_push` for element and script top-level
content, `array_push` for every other collection — and a builder must never
re-express the choice as ambient state — the process-wide suppression flag that
used to exist for exactly that purpose has been retired.

A sweep of all 152 `list_push` call sites followed. The ~120 in
`lambda/input/markup/**` are correct: they build genuine element content. Of the
rest, **19 more sites had the same defect** and were converted: 17 in
`lambda-vector.cpp` (`reverse`, `take`, `drop`, `zip`, `array_split`, `shape`,
`math_random`, pipe-collect, vector ops), the JS→Lambda `start()` argument list
(`concurrency_js.cpp:169`), and `call()`'s packed-array widening
(`lambda-eval.cpp:1284`). `reverse(["a","b","c"])` returned `["cba"]` and
`take(["a","b","c"], 2)` returned `["ab"]`; `reverse([1,null,2])` returned
`[2,1]`. Reviewed and deliberately left on `list_push`: the markup parsers,
`collection_runtime.cpp` (that *is* the content recursion), `pattern_find`/
`fn_find` (push maps), `input-mark.cpp` (pushes into an element), and `path.c`.

`test/std/core/functions/collection_reverse.expected` had **encoded both bugs as
expected output** (`["cba"]`, `[false, true]`) — the suite was ratifying the
defect. Regenerated. The neighbouring tests could not have caught it either:
`take_drop.ls` used only numbers and `zip.ls` only ever paired a number with a
string, so two adjacent strings never met; both now cover strings and `null`.

Note the merge half is ambient-dependent — string merging is gated on an active
input context while null-stripping is unconditional (Design Appendix A, D2.6.5),
which is why the same function could merge in one call path and not another.

The `disable_string_merging` flag turned out to be **vestigial**: its sole
assignment set it to `false` (`input.cpp:1061`) and nothing anywhere set it
`true`, so no input format selected normalization through it. It has now been
retired — removed from `EvalContext` and `InputAllocationContext`, from
`list_push_with_owner`'s signature and both of its callers, and from the merge
gate — with output byte-identical before and after. Per-format policy
lives in the *builder* instead — MarkBuilder formats (latex, json, xml, yaml,
toml, csv, pdf, …) append with `array_append` and never normalize, which is why
LaTeX may hold consecutive strings, while the markup family (markdown, asciidoc,
textile, wiki) calls `list_push` and merges them. `input-ics.cpp` and
`input-mark.cpp` use both and so mix the two policies — worth reconciling, along
with retiring the dead flag.

<a id="lr09-r4-index"></a>**LR09-R4-index · `fn_index` invalid reads return `null` · RESOLVED 2026-09-13 (not a defect — ruled by S7.1.1v3 and S8.2.1v4)**
The former LR09-4 entry misclassified the implementation as swallowing errors.
The formal semantics explicitly require every invalid member/index **read** to
yield `null`, including fractional or negative sequence positions, out-of-range
positions, and keys outside a container's domain. Invalid **writes** remain hard
errors under S7.1.3v2, which is what `proc_invalid_member_access.ls` verifies.
`oob_read_null.ls` covers out-of-range, negative, and chained reads. The
former type-dependent JIT float-OOB residue is resolved as
[LR07-10](#lr07-10). The `-index` archive suffix
disambiguates this original LR09-4 record from an unrelated legacy LR09-R4
anchor already present in this archive.

<a id="lr09-r4"></a><a id="lr10-3"></a>**LR09-R4 · `set_runtime_error` message buffer cap · RESOLVED 2026-08-28**
`err_createf` and `set_runtime_error` now share the exact-size variadic
formatter backed by `mem_alloc`, so long diagnostics are not silently
truncated at 1023 bytes. The shared 64-frame trace default is recorded in
[LR10-R4](#lr10-r4). This also closes the duplicate LR10-3 index entry; its stable anchor
is retained here. Regression: `ErrorCreationTest.CreateFormattedErrorPreservesLongMessage`;
the error suite passes 121/121 and the Lambda baseline passes 3978/3978.

### A.12 Procedural runtime (LR_12)

<a id="lr12-r2"></a>**LR12-R2 · Mutation builtins swallow type errors · RESOLVED 2026-08-26**
`pn_push` and `pn_splice` now return `ItemError` for invalid owners, indices,
counts, views, and N-D arrays instead of returning the unchanged input. Their
registry rows publish `may_return_error`, so `or` recovery can observe the
failure. The successful owner-returning convention remains unchanged; only
the unresolved choice between updated-owner and unit conventions in
`S7.10.6` remains open. Targeted invalid-mutation probes and the full baseline
pass.

<a id="lr12-r3"></a>**LR12-R3 · Safety gate hard-coded, TCO disabled despite being implemented · RESOLVED 2026-09-10 (record was never live)**
The procedural-runtime face of the same mistaken reading recorded in
[LR11-R6](#lr11-r6). `function_needs_stack_check` / `function_is_tail_recursive`
were never called by any code; `should_use_tco` was and is the real gate, and it
was already wired at the commit the ledger verified against. The vestigial
functions were deleted in `8d44a6ca3` (2026-09-01). No behaviour changed when
they went, which is itself the proof they gated nothing.

<a id="lr12-r8"></a>**LR12-R8 · `push`/`splice` mutate a module-level `let` in place, falsifying `fn` purity · RESOLVED 2026-08-26**
The COW selector only found local `MirVarEntry` bindings, so a module-level
binding fell through to the raw in-place mutator. The fix applies the existing
E211 immutable-root validation to the builtin's owner argument during AST
construction, rather than silently copying in the COW path. This enforces
`S9.1.1` and `S9.1.6`: mutation through a module-level `let` is rejected, while
the caller must use an allowed mutable owner. A targeted module-let probe now
raises E211 and the full baseline passes 3914/3914.

<a id="lr12-r9"></a>**LR12-R9 · Construction/insertion aliases instead of capturing by value · RESOLVED 2026-09-13**
The **S9.1.2/S9.1.3/S9.3.1** COW implementation is now unconditional. Commit
`b588f3f191` flipped capture on by default, and `9600c94d94` retired the
`LAMBDA_COW_CAPTURE` escape hatch; current runtime code no longer consults it.
Insertion capture and plain-parameter snapshot coverage pass on both tiers,
including the two-node-cycle and nested-update probes. The dedicated COW
fixtures pass under JIT with forced collection and freed-object poisoning, with
the ruled value preserved.

---

### A.13 Sibling vibe ledgers

<a id="issues0-r9"></a>**Issues0 #9-R · ShapePool hash collision reused a different shape · RESOLVED 2026-08-27**
The hashmap now confirms the existing structural shape comparison after the
signature routing key, and retains element names as cache identity metadata.
This prevents a colliding signature from reusing a shape with different field
names, types, offsets, flags, or element identity. The focused regression is
`NamespaceTest.ShapePoolCollisionDoesNotAliasDifferentFieldNames`; the full
namespace suite passes 39/39 and `make test-lambda-baseline` passes 3976/3976.

<a id="i8-consoleesc-r"></a>**Issues8 · Console formatter does not escape quotes or backslashes inside collections · RESOLVED 2026-08-27**
The collection printer's Item and legacy TypedItem string/symbol branches used
raw buffer interpolation, so quotes, backslashes, and control characters made
collection output ambiguous. The shared length-based `print_quoted_text` helper
now emits the grammar-supported Lambda escapes for collection members. Standalone
strings retain their existing display behavior to avoid double-escaping an
already serialized string; no new data structure or design ruling was needed.

Regression: `NamespaceTest.PrintCollectionEscapesStringContents` covers quote
and backslash members. The 42 affected golden outputs were regenerated from
the fixed printer, and `make test-lambda-baseline` passes 3976/3976.

<a id="lint-e1-r"></a>**Lint E1 · Unchecked allocation dereference in `build_ast.cpp` · RESOLVED 2026-08-27**
The custom `mem_alloc` calls and `strview_to_cstr` literal copies were
fallible, but their callers read the returned buffers before checking them.
`ast_copy_source_text` now centralizes the seven literal source-copy sites,
reports `E309`, and returns the existing `TYPE_ERROR` recovery value. This
keeps the literal path aligned with **D4.2.1v3** and **D4.2.2v2** without
introducing a new data structure or design rule.

Regression: `AstBuildAllocationTest.SizedLiteralCopyFailureDoesNotCrash`.
The focused error suite passes 120/120 and `make test-lambda-baseline` passes
3976/3976.

### A.14 Design-gap closures

<a id="oi-4"></a>**OI-4 · RegExp semantics · RESOLVED 2026-09-15**
The runtime now uses selective structural-equivalence routing rather than the
former token-presence heuristic. `js_regex_scanner_analyze` reduces
`nullable`, `has_capture`, and `may_skip_capture` facts through a depth-bounded
open-group stack, with no routing AST. It selects `js_bt_regex` for
backreferences, assertions, multiline anchors, optional nullable iterations,
and repeated bodies that may skip a capture; ordinary exact repetition shapes
remain on RE2.

`js_create_regex` now treats a required backtracker compilation failure as an
explicit error instead of falling through to RE2. `JsBtExecResult` also
separates no-match from budget exhaustion and allocation failure; shared
RegExp/String consumers propagate the latter outcomes without no-match state
updates. This fulfils the guest-semantics and explicit-failure requirements of
**D1.3**, **D1.4v3**, and **D8.4.3v2**.

Focused evidence: `test_js_regex_router_poc_gtest` passes its 4,260-case route
audit, 13-pattern × 765-subject Node differential (zero divergent digests),
and explicit resource-error probe. `test_js_bt_regex_gtest` passes all 50
matcher tests, including all three anti-DoS budget tests.

---
