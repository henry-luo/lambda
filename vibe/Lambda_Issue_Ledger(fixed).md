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

This archive contains **79 historical records**: 78 RESOLVED entries and one
CLOSED design decision. Duplicate and split records remain separate so their
provenance is not lost. The first sections contain the 34 records formerly
interleaved with live entries; §15 preserves the 44 records from the former
resolved/obsolete appendix.

## 1. Compilation pipeline, CLI & REPL (LR_01)

<a id="lr01-8"></a>**LR01-8 · `init_module_import` pointer-walk is layout-coupled · RESOLVED 2026-09-05**
`init_module_import` was unreachable legacy C2MIR glue: no MIR Direct path
called it, while the live importer links individual symbols and immutable module
layout records. Removing the dead `uint8_t*` field walk removes its unguarded
`Mod` layout contract outright, consistent with the single MIR Direct pipeline
in **D8.2.5**. Existing paired-loop and module fixtures still pass after the
removal.


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


## 6. C transpiler — legacy C2MIR (LR_06)

**All nine issues are archived below as [LR06-R1…R9](#lr06-r1r9).** The C2MIR backend no longer exists in the tree.

## 7. MIR Direct transpiler & JIT (LR_07)

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


## 8. Memory management & GC (LR_08)

<a id="lr08-1"></a>**LR08-1 · Decimal `mpd_t` leak (in-code TODO) · RESOLVED 2026-09-05**
Per **D4.3.3**, the GC delegates out-of-zone cleanup to the C++
`heap_gc_destroy_external_payload` bridge. For `LMD_TYPE_DECIMAL`, it calls
`decimal_payload_release`, which runs `mpd_del` and clears `dec_val` before
sweep reclaims the wrapper. Teardown uses the same bridge, so it has one
idempotent ownership path rather than a Decimal-specific second free.
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


## 9. Runtime builtins (LR_09)

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

---


## 11. Mark data API (LR_11)

<a id="lr11-5"></a>**LR11-5 · `deep_copy` of `PATH` is shallow · RESOLVED 2026-09-05**
`path_clone` replays the immutable path spine into the destination pool, giving
every copied non-`sys` path independent names and links. Resolution and metadata
caches are deliberately not copied, so no source-owned payload survives. This
keeps the ownership chain precise under **D4.4.3**. Regression:
`MarkBuilderDeepCopyTest.CopyPathRehomesSpineAndDropsSourceCaches` destroys the
source pool before reading the copied path.


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


## 13.1 Verification-pass records (LR_03 and ledger hygiene)

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
   *note:* the runtime-side helpers persist as
   [LR03-3](<Lambda_Issue_Ledger.md#lr03-3>).
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

---
