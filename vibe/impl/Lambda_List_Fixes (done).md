# Lambda List/Array Kind, Content, Text, `++`, and Type Families — Implementation Plan

> **Status:** IN PROGRESS — P0 and P1 done and committed (`fc3153b3b`,
> 2026-09-22; §7–§10, P1 residue in §10); P2, P3 and P4 done in the working
> tree, uncommitted (§11, §12, §13 — P2/P3 were lost to a pull's autostash on
> 2026-09-23 and recovered, see §13); P5 and P6 not started
>
> **Date:** 2026-09-22
>
> **Last verified against the live tree:** 2026-09-23 (anchors below are from
> the tree at 2026-09-22; re-resolve before editing)
>
> **Design authority:** `vibe/Lambda_Design_Syntax.md` §7.27 (points 1–26) and
> §7.28; `vibe/Lambda_Type_Pattern.md` §1.3
>
> **Normative semantics** (`doc/Lambda_Formal_Semantics.md` 29.0.0):
> S2.2.3, S2.5.1v2–S2.5.8, S2.6.1–S2.6.5, S3.3v2, S7.10.1v2, S7.10.5v2,
> S8.3.1v3, S10.1.2v2, S10.1.5v2, S10.6.1, S11.1.1v3, S11.1.2v2, S11.1.6,
> S12.3.5v2, S16.7.2v2/S16.7.3v2, S16.8.6v2
>
> **Normative implementation constraints:** D2.6.5v3 (three append
> disciplines, one kind flag), S1.6/SI1 (tier parity, representation
> invisible), S9.3.1 (construction captures by value), D1.6 (MIR Direct only)
>
> **Ledger:** [LR05-10](../Lambda_Issue_Ledger.md#lr05-10) (umbrella),
> [LR05-11](../Lambda_Issue_Ledger.md#lr05-11), [LR05-12](../Lambda_Issue_Ledger.md#lr05-12),
> [LR12-28](../Lambda_Issue_Ledger.md#lr12-28), [LR03-12](../Lambda_Issue_Ledger.md#lr03-12),
> [LR05-9](../Lambda_Issue_Ledger.md#lr05-9) (`++` bit reinterpretation);
> the S2.2.3/S2.6.x and S2.5.x rows of semantics Appendix A

---

## 0. Scope and non-goals

**In scope** — every fix the 2026-09-21/22 rulings require:

| Ruling | Behaviour to land |
|---|---|
| S2.5.1v2, S2.5.5v2, S2.5.6 | one kind bit; list never nests; ≥ 2 items; void vs null; `()` parses; lists do not normalize; slot stores convert to the array image; list prints as array |
| S2.5.2v2, S2.5.7, S7.10.1v2, S7.10.5v2, S8.4.1v2, S10.1.2v2, S10.1.5v2 | every sequence operation preserves the input kind; mixing gives array; `for` (all clauses) is a list; constructors fix their kind; `fill` follows its item; `zip` pairs are arrays |
| S2.5.8, S8.3.1v3 | text walks as a sequence; result kind for text; `in` is character membership; `"s"[-1]` is `null`; scalars have no content, pipe/`that` treat them as one member |
| S10.6.1 | the `++` table; LR05-9 |
| S12.3.5v2 | `*` never mutates; `*null` nothing; `*scalar` one item; `*range` spreads |
| S2.2.3, S2.6.2, S2.6.4, S2.6.5 | lone `""` dropped; binaries merge; content writes normalize; `push` on elements |
| S11.1.1v3, S11.1.2v2, S11.1.6, S16.8.6v2 | `T{n,m}` grammar; `T[n]` = array of n; `T[n+]`/`T[n, m]` retired; island spelling; boundary semantics of occurrence types; `is list`; `type()` names `list` |
| D2.6.5v3 | content / sequence / verbatim appends |

**Non-goals** (tracked elsewhere): the LaTeX parser carrying verbatim string
runs in non-merging items (S2.6.1 transitional deviation); `keys`/`values`/
`names` as builtins (S8.4.1v2 says deliberately not built in); the relative-path
parse defects (`\.1`, `\.[1]`, bare `\.` — a separate task session); the
wrong-value ledger items not caused by these rulings (LR03-11, LR04-9,
LR07-17, LR07-18, LR10-7, LR10-8, LR12-27); user-doc prose beyond the
occurrence tables named in P5.

---

## 1. Implementation decisions

### 1.1 One kind bit: keep `is_spreadable`, retire the Lambda meaning of `is_content`

`Array` carries two flags (`lambda/lambda.h:1015`–`1016`): `is_content`
("content list or value list") and `is_spreadable` ("spread when added to
collections"). The survey showed they are set by disjoint producers and read
by disagreeing consumers (LR05-10). D2.6.5v3 rules that content-ness is a
property of the **destination** — an element's items are content because the
container is an element; the script top level is content because the runner
builds it — never of the value. Normalization is applied when content is
built and maintained by the element write path (S2.6.5), so no value ever
needs to remember it was built as content. Therefore:

- **`is_spreadable` is the kind bit**: set ⇔ the value is a list. Nothing
  else about a list is stored.
- **`is_content` loses its Lambda meaning.** The bit itself stays allocated,
  because LambdaJS uses `is_content == 1` as its *Arguments object* marker
  (`js_runtime_state.cpp:2263`; ~35 reads across `js_runtime.cpp` and
  `js_globals.cpp`; `lambda.h:1066` ties the strict-arguments companion bit to
  it). Rename the field `is_js_arguments` so the bit has one honest meaning;
  the JS sites are a mechanical rename. `js_runtime.cpp:23228` (clearing
  `is_content` on a `split` result "to prevent array flattening in JS")
  becomes: clear `is_spreadable` on every Lambda value published into JS — a
  list never spreads inside JS.

Every Lambda reader of `is_content` and its replacement:

| Site | Today | After |
|---|---|---|
| `collection_runtime.cpp:193` (`array_push` splice test), `:385` (`list_push`) | splice iff `is_content` | splice iff `is_spreadable` (sequence append) / always splice lists (content append) |
| `lambda-data-runtime.cpp:1564`, `:1625`, `:1778` (1-D/N-D promotion guards) | skip if either flag | skip if `is_spreadable` |
| `lambda/core/print.cpp:714`–`727` (`print_root_item`: content block prints one item per line, no brackets) | keyed by `is_content` | the runner prints the script's content items itself (§1.7); `print(list)` prints `[…]` |
| `lambda/io/collection_io.cpp:73`, `:86`; `lambda/runtime/render_map.cpp:577`, `:593`, `:611` (UI/render "content list" tests) | value flag | decided by the container (element content) or the call site — audit each |
| `lambda/io/mark_builder.cpp:943`–`945` (clone preserves the bit) | preserves `is_content` | preserves `is_spreadable` (§1.5) |
| result setters — vector ops `lambda-vector.cpp:585`, `:1189`; pipes `:2041`, `:2057`, `:2073`; reverse `:2403`, `:2418`; sort `:2436`, `:2514`; take `:2792`; drop `:2856`; slice `:2908`; zip `:4998`; `split` `lambda-eval.cpp:6813`, `:6881`, `:7045`; `find` `:7614`–`:7658`; `re2_wrapper.cpp:940`; `list_end` `lambda-data-runtime.cpp:1488` | each sets `is_content = 1` | replaced by the result-kind helper (§1.4) or cleared (constructors build arrays) |

### 1.2 Three appends (D2.6.5v3)

| Append | Used by | Behaviour |
|---|---|---|
| **content** (today `list_push`) | element content, script top level, content writes (S2.6.5) | drop `null` and `""`; splice any list; merge adjacent strings and adjacent binaries — **always**, no `input_context`/`input_allocation_context` gate (`collection_runtime.cpp:423` goes; the gate is the retired `disable_string_merging` in disguise, D2.6.5v3 footnote) |
| **sequence** (new; today `array_push` + the `*_spread` variants) | list and array literals, `push`/`splice`/`a[i] = v` on arrays and lists | splice iff `is_spreadable`; store everything else verbatim, `null` included; never merge |
| **verbatim** (`array_push_verbatim`, renamed from `array_push_argument` in P1) | argument and rest lists, `zip` pairs, every positional runtime structure (group/join/order keys and rows, path keys, query matches, index-addressed vector results), element copies (clones, set operators, `reverse`/`take`/`drop`/`slice`/`unique`), and every single-value slot after the image conversion (§1.6) | store the item as one value whatever it is |

`list_push_spread` (`:486`), `array_push_spread` (`lambda-data-runtime.cpp:1815`)
and `array_push_spread_all` (`:1824`, "spread any array unconditionally", used
by pipes) collapse into the sequence append plus one explicit
`seq_push_spread(dst, item)` for the `*` operator (§1.8). `ITEM_NULL_SPREADABLE`
(`lambda.h:1722`; produced by `array_end` `lambda-data-runtime.cpp:1770`,
skipped at `collection_runtime.cpp:488`, `lambda-data-runtime.cpp:1816`,
`:1825`, and special-cased in `transpile-mir.cpp:16442`, `:20830`, `:20900`,
`interp.cpp:2412`, `:3203`, `:3232`) is **retired**: it is the hidden third
empty value the survey found, and S2.5.5v2 leaves no room for it.

### 1.3 List producers finish in two modes (void vs null, S2.5.5v2)

`(…)` literals, functional blocks, and for-expressions (whatever their
clauses) are the list producers. Each finishes in one of two modes chosen by
the **producer's own syntactic position**:

- `list_finish_item` — the producer is a direct item of a list or array
  literal, a statement of a block, or a child of element content / the script
  top level: its items are spliced straight into the enclosing builder, zero
  items contributing nothing.
- `list_finish_value` — anywhere else (binding, argument, return, operand,
  field value, `if`/`match` branch, subscript): collapse — 0 → `ItemNull`,
  1 → the item, ≥ 2 → the list with `is_spreadable = 1`. No normalization
  ever (`list_end` today sets `is_content` and the literal path normalizes:
  `("a", "b")` is `"ab"` — LR05-10).

This is the one place where syntax legitimately decides: it is the
*producer's* position (the ruling's "list expression in item position"), not
the consumer guessing from its siblings. `has_spreadable` in the array-literal
emitters (`transpile-mir.cpp:20509`–`20525`, `:20865`; `interp.cpp:2374`–`2398`,
`interp_array_has_spread`) is deleted: a literal pushes every item through the
sequence append, which reads the value's bit.

### 1.4 Result-kind helper

`seq_result_kind(a, b, …)`: list iff every sequence operand is a list
(`is_spreadable`); an array or range operand forces array; scalars and `null`
are ignored; text operands follow S2.5.8 (§1.9). `array_transform_copy_cert`
(`lambda-vector.cpp:165`–`177`, already called by reverse/sort/unique/take/
drop/slice with source and result) is extended to carry the kind bit; every
multi-input operation calls the helper explicitly. A list result of ≥ 2 items
keeps the bit; a list result of 0 or 1 items is finished by
`list_finish_value` (collapse) — sys functions never return a one-item or
empty list.

### 1.5 Kind survives

The bit lives on the container header, so bindings, arguments, returns, and
`var` mutation already preserve it. It must also survive: COW clone/detach
(`mark_builder.cpp:943` and `cow_prepare_write`), typed `T[]`/`T*` admission
(`lambda-eval.cpp:9918` rebuilds the value as a numeric array and drops it),
`push`/`splice` result construction, and N-D/1-D promotion (`array_end`
promotes only non-lists; a list of numbers stays a generic list — acceptable,
lists are transient; revisit only if a benchmark shows it).

### 1.6 Slot stores convert to the array image (S2.5.6)

Map literal `{f: (1, 2)}`, `<e a: (1, 2)>`, object fields, `fn_map_set`
(`lambda-eval.cpp:11821`), `map_set_cow` (`:10499`), attribute set, object
field set: a list value is stored as its array image. The source must not be
mutated (`let l = (1, 2); let m = {f: l}; [l, 9]` keeps `l` a list), so the
store makes the image a **separate header** — either a shallow copy (lists are
small and transient) or a fresh `Array` header COW-sharing the items buffer
with the bit clear; pick by measurement in P3. `fill` is *not* a slot store
(S2.5.7: it splices its item).

### 1.7 Top-level content printing

`print_root_item`'s content-block branch (`print.cpp:714`) moves to the
runner: the runner owns the script's content list and prints its items one
per line. A list reached any other way prints as an array (`print(l)`,
`string(l)`, `format(l, …)` — the last two already do).

### 1.8 `*` spread (S12.3.5v2)

`item_spread` (`lambda-data-runtime.cpp:2397`) sets the bit on its operand —
the mutation LR05-12 describes, including pooled JIT literals
(`transpile-mir.cpp:8148`) — and has a dead branch (the `List*` arm repeats the
`LMD_TYPE_ARRAY` test). It is retired: the emitters pass per-item spread
intent, and `seq_push_spread` splices any sequence (list, array, **range** —
materialized), nothing for `null`, and a non-sequence as one item. `[*xs]`
thereby packages any value as an array.

### 1.9 Text (S2.5.8)

Text values are walked as sequences by every sequence operation and placed as
one value by `++` and the appends. Select/reorder operations (`reverse`,
`sort`, `unique`, `take`, `drop`, `slice`, `[i to j]`, `that`) over a string
return a string (their items are always characters); a mapping pipe returns a
string when every result is a string (concatenated) and an array otherwise;
symbols and binaries alike with their own kind. `fn_in` (`lambda-eval.cpp:3814`,
`strstr`) becomes code-point membership; `contains` keeps the substring test.
`item_at_empty_string` (`lambda-data-runtime.cpp:3064`, used at `:3119`,
`:3131`, `:3144`, `:3150`) yields `null` for out-of-range string subscripts
(S7.2.1); `""` stays the empty *result* of a text operation (S7.10.1v2).

### 1.10 `++` (S10.6.1)

`fn_join` (`lambda-eval.cpp:441`) implements the table: sequence `++`
sequence → concatenate, kind by §1.4 (fix LR05-9 first: the same-type
shortcut at `:527` copies the right `ArrayNum` payload under the left element
type — widen to a common element type or fall back to a generic array);
sequence `++` scalar → append as one item (text is a scalar here); scalar
`++` sequence → prepend; scalar `++` scalar → text, same text kind kept;
`null` identity; map/element operands → error. Paths keep S2.4.2v4.

### 1.11 Type families (S11.1.1v3, S11.1.6, S16.8.6v2)

- **Grammar** (`grammar.js:1099`–`1112`): `occurrence_count` becomes the
  brace form `{n}` / `{n,m}` / `{n,}`, with the same same-line guard the
  `_index_lbracket` alias gives `[` (a line-start `{` after a complete type is
  the S16.2.3 error, never a continuation); `[n]` moves to the array family as
  a counted `T[]`; `[n+]` and `[n, m]` are removed. Regenerate with
  `make generate-grammar`; never edit `parser.c`.
- **C parser** (`parse_type_pattern.cpp:925` `apply_occurrence`): `[n]` →
  `OPERATOR_ARRAY` with a count; `{…}` → `OPERATOR_REPEAT`; `[n+]`/`[n, m]` →
  a diagnostic naming the new spelling. String islands: the island tokenizer
  and `compile_pattern_to_regex` (which re-reads the occurrence spelling for
  `REPEAT`, `:397`) emit regex `{n,m}` from the brace form.
- **Semantics**: `fn_is` / contract admission for occurrence types admit
  `null` (0), a bare `T` (1), a sequence of ≥ 2 `T` (list or array) — in slot
  position a run; `T[n]` admits arrays and lists of length n; `LIT_TYPE_LIST`
  hard-coded false (`lambda-eval.cpp:2198`) becomes the bit test; `fn_type`
  returns `list` for a spreadable array; `(P, Q)` list patterns collapse and
  admit the array image; `<:` over the families (`int <: int*`, `int? <:
  int*`, `int[2] <: int*`, `int[] </: int*`, `int* </: int[]`).

---

## 2. Phases

Each phase ends with `make test-lambda-baseline` at 100% on **both** tiers
(`LAMBDA_TIER=jit` and `LAMBDA_TIER=interp`) and the phase's fixtures green.
Goldens that change are triaged one by one: **now-correct under a ruling →
update the golden and cite the ruling in the fixture; anything else → a
regression to fix before moving on.**

### P0 — Fixtures first (DONE 2026-09-22)

The survey probes (`temp/spec_survey/listarray/`, `…/collections/`,
`…/strings/`, `…/types/`, `temp/ledger_repro/`) became fixtures with `*.txt`
goldens written **from the rulings**, not from today's output. Because the
baseline gate (`test_lambda_gtest`) auto-discovers `test/lambda/` and
`test/lambda/proc/`, red-on-entry fixtures live in **`test/lambda/ext/`** and
**`test/lambda/proc-ext/`** (the `test_lambda_extended_gtest` directories, run
by `test-lambda-full`) and **move to the baseline directories as each phase
turns them green**. Every fixture header names its phase. The two negative
fixtures (`test/lambda/negative/semantic/occurrence_retired_{plus,range}.ls`,
`@expect-error: E100`) are registered in `test_lambda_errors_gtest` by P5,
with the diagnostic's final wording; the JS Arguments guard is written in P1
next to the rename (it must be green before and after). On entry: every
fixture parses except `type_families.ls` (`T{n,m}` is new syntax, P5); the
goldens differ from today's output exactly where the rulings differ. Two
fixtures pin decisions the rulings leave to §1.3: `pipe_that_kind` records
that a pipe or `that` result is a *value* — `[L that ~ > 9, 9]` is
`[null, 9]` and `[null |> ~ + 1]` is `[null]` — not a syntactic producer;
`spread_star` shows the pooled-literal mutation only under `LAMBDA_TIER=jit`,
so when it moves to the baseline it also joins the tier-pinned list in
`test_lambda_gtest.cpp` (`interp`/`jit`/`auto`). Files:

| Fixture | Pins |
|---|---|
| `list_kind_literal.ls` | S2.5.1v2: `type((1,2))`, `(1,2) is int[]`/`is list`, `((1,2),(3,4))`, no normalization, `[(1,2), 3]` |
| `list_collapse_void.ls` | S2.5.5v2: `()` ≡ null, `(x)` ≡ x, `[for (x in []) x, 1]` = `[1]`, bound empty = `[null, 1]`, `push(a, e)`, the n = 1 edge (`len(take((10,20,30),1))` = 0), `[for …]` idiom |
| `list_kind_transform.ls` | S2.5.7: every operation in the §3.1 matrix with list, array, range inputs |
| `list_kind_landing.ls` | S2.5.6: `{f: (1,2)}.f`, `<e a: (1,2)>`, object field, `push`/`splice`/`a[i] = list` splice, `print`/`string`/`format` of a list |
| `list_kind_for_clauses.ls` | S2.5.2v2: `where`, `order by`, `limit`, `offset`, `group by`, nested for, `fill` |
| `pipe_that_kind.ls` | S10.1.2v2, S10.1.5v2: list/array/range/map/element/scalar/null sources; empty results `null` vs `[]` vs `""` |
| `text_sequence.ls` | S2.5.8: `reverse`/`sort`/`unique`/`take`/`drop`/slice on strings, symbols, binaries; `in` vs `contains`; `"abc" \|> upper(~)`, `\|> ord(~)`, mixed results; `"s"[-1]` |
| `concat_table.ls` | S10.6.1: every operand pair incl. `null`, text kinds, ranges, maps (error), LR05-9 numeric arrays |
| `spread_star.ls` | S12.3.5v2: `[*a, 3]` leaves `a` unchanged (call twice through a function — the JIT pooled-literal case), `*range`, `*null`, `*scalar`, `[*xs]` packager |
| `content_normalize.ls` (extend the existing S2.6 fixtures) | S2.2.3 lone `""`, binary merge, S2.6.5 writes: `e[i] = null`/`""`/list/string-beside-string, removal merging, `push` on an element |
| `type_families.ls` + `negative/…/occurrence_retired.ls` | S11.1.6: `null is int*`, `[] is int?`, `[5] is int*`, `[5,6] is int*`, `(1,2) is int[2]`, `{f: int*}` cases, `int{2}` vs `int[2]`, `\(d{3})`, `T[n+]`/`T[n, m]` rejected with the hint |
| `proc-ext/content_writes.ls` | S2.6.5 content writes in a `pn` (the write half of `content_normalize`) |
| `proc-ext/list_var_mutation.ls` | kind survives `push`/`a[i]=` on a `var` list; `push(a, e)` pushes `null`; a field stores the image |

The JS guard — a `test_js_*` case that a Lambda list published into JS is a
plain array and that Arguments objects still iterate with a snapshot length —
belongs to P1 (see above).

### P1 — One kind bit, three appends, two finish modes (§1.1–§1.3, §1.5, §1.7)

1. `lambda.h`: rename `is_content` → `is_js_arguments`; comment `is_spreadable`
   as "the list kind bit (S2.5.1v2)". Mechanical rename across `lambda/js/`.
2. `collection_runtime.cpp`: introduce `content_push`, `seq_push`,
   `verbatim_push`, `seq_push_spread`; port `list_push` → content (drop `""`,
   merge binaries, no context gate), `array_push` → sequence,
   `array_push_argument` → verbatim; delete `list_push_spread`,
   `array_push_spread`, `array_push_spread_all`, `item_spread`,
   `ITEM_NULL_SPREADABLE`.
3. `lambda-data-runtime.cpp`: `list_end`/`array_end` → `list_finish_value` /
   `list_finish_item`; promotion guards on `is_spreadable` only.
4. Emitters (`transpile-mir.cpp` array/list literal, block, for-expression
   finish; `interp.cpp` likewise): choose the finish mode by the producer's
   position; delete `has_spreadable`/`interp_array_has_spread`; list literals
   built with the sequence append (no normalization); accept the `()` literal
   (parser: `parse.c`/`parser/`, and `grammar.js` for the CST verifier).
5. Runner: print top-level content items one per line; `print_root_item`
   loses its content branch.
6. `mark_builder.cpp:943`, `cow_prepare_write`: carry `is_spreadable`.
7. `collection_io.cpp`, `render_map.cpp`: replace the value-flag tests with
   container/call-site decisions (Radiant baseline must stay green).
8. JS export boundary: clear `is_spreadable` on values handed to JS.

Acceptance: `list_kind_literal`, `list_collapse_void`, `list_kind_landing`
(print/format half), `proc/list_var_mutation`; baseline + `test-radiant-baseline`
+ the JS gate green; the `("a","b")` and hidden-empty behaviours gone.

### P2 — Transforms preserve kind (§1.4)

`lambda-vector.cpp`: vector ops (`:543`–`585`, `:1148`–`1189`), `fn_reverse`
`:2392`, `fn_sort1/2` `:2425`/`:2503` (and `vector_to_plain_array` `:101`),
`fn_unique` `:2688`, `fn_take`/`fn_take_last`/`fn_drop`/`fn_slice*` `:2761`–
`:2919`, `fn_zip` `:4963` (pairs verbatim arrays; outer kind by helper),
`fn_fill` `:1654` (splice the item; kind follows it), `fn_pipe_collect`
`:2012` and the MIR/interp pipe and `that` collectors (`transpile-mir.cpp:30053`,
`:30316`; `interp.cpp:2896`) — kind from the source, scalar as one member
(`5 that ~ > 3` is `5`), empty `[]`/`null`/`""` by source kind;
`fn_sort_by_keys` `:2494` (keep the list; drop the early return `:2455`);
limit/offset via `fn_take`/`fn_drop` (`transpile-mir.cpp:15203`–`15209`) keep
the list; `child_query_collect` (`lambda-eval.cpp:3773`) stops treating
`is_spreadable` as "previous query result" (use its own marker or none) so
`(for …)[int]` and `(1 to 3)[int]` work; set operators; `split`/`find`/
`range()`/`varg()`/rest → arrays (`fn_split` `lambda-eval.cpp:6813`…, `find`
`:7614`…, `re2_wrapper.cpp:940`); typed admission `lambda-eval.cpp:9918`
keeps the bit.

Acceptance: `list_kind_transform`, `list_kind_for_clauses`, `pipe_that_kind`.

### P3 — Landing: slot stores and content writes (§1.6, S2.6.5)

Map/element/object literal construction and `fn_map_set`/`map_set_cow`/
attribute/field setters convert a list to its array image without touching
the source; `push`/`splice`/`fn_array_set` (`lambda-eval.cpp:8208`) on arrays
and lists splice a list value; the element arm of `fn_array_set` and `push`
on an element (`collection_runtime.cpp:240`, "expected a growable array, got
element") go through the content append with S2.6.5's re-normalization
(write `null`/`""` removes, a list splices, a string beside a string merges,
removing a separator merges its neighbours); `<e "">` drops the lone `""`;
adjacent binaries merge. `input-ics.cpp` / `input-mark.cpp` use the content
append for element content only (D2.6.5v3 footnote).

Acceptance: `list_kind_landing`, `content_normalize`.

### P4 — Text, `++`, `*` (§1.8–§1.10)

`fn_reverse`/`fn_sort*`/`fn_unique` text arms (`lambda-vector.cpp:2396`,
`:2429`, `:2696`), `take`/`drop`/`slice` on text, `that`/pipe text result
kind, `fn_in` (`lambda-eval.cpp:3814`), `item_at_empty_string` users; `fn_join`
(`lambda-eval.cpp:441`, `:527`) table incl. LR05-9; `seq_push_spread` for the
`*` operator in both emitters, `item_spread` deleted, range materialization.

Acceptance: `text_sequence`, `concat_table`, `spread_star`.

### P5 — Type families (§1.11) and corpus migration

Grammar + regeneration; C parser; islands; `fn_is`/admission/`<:`; `fn_type`
→ `list`; `is list`. Then the migration sweep: the corpus uses of the retired
spellings — `test/lambda/string_pattern.ls:140` (`\(d[2, 4])`),
`test/lambda/type_pattern.ls:62`, `:88`, `:129`, `:161` (`Point[3+]`,
`string[1+]`, `Product[1+]`, `(int*)[2+]`) and any others the grammar change
surfaces — and the ~40 `T[n]`-style type uses (`type_syntax_edges.ls:20`/`:23`,
`type_occurrence.ls:12`/`:13`/`:79` among them), which keep their meaning at
n ≥ 2 and change at `int[0]` (`[]`, not void) and `int[1]` (`[int]`, not a
bare int) — re-verify each golden against S11.1.6. User docs:
`doc/Lambda_Type.md` §Type Occurrences (`int[3+]`, `int[2, 10]` rows, and the
`T*`/`T+` "pattern-only cardinality" rows), `doc/Lambda_Data.md` (`s[-1]`),
`doc/Lambda_Sys_Func.md` (`zip` shown as list pairs).

Also found while writing the fixture: an occurrence type cannot be written
inline as a `<:` operand — in value position `int* <: T` parses `*` as
multiplication (S11.1.4v2 says the operands are type values). The fixture
binds them first (`type IntRun = int*`); P5 decides whether `<:` should take a
type-expression operand like `is` does, or whether binding stays the rule.

Acceptance: `type_families`, the negative fixture, all migrated goldens.

### P6 — Close-out

Semantics Appendix A: rewrite the four 2026-09-21/22 rows and the S2.2.3/S2.6
row as conformant (with dates and baseline counts); drop the `*` marks that
no longer apply (S2.2.3, S2.5.1v2, S2.5.2v2, S2.5.5v2–S2.5.8, S7.10.5v2,
S10.6.1, S11.1.1v3, S11.1.6, S12.3.5v2). Design Appendix A: D2.6.5v3 row.
Move LR05-10/11/12, LR12-28, LR03-12, LR05-9 to the fixed ledger with `-R`
suffixes. Update `vibe/Lambda_Type_Pattern.md` §1.3 status and
`Lambda_Design_Syntax.md` §7.27/§7.28 status lines; rename this file
`Lambda_List_Fixes (done).md`.

---

## 3. Reference matrices

### 3.1 Operation → result kind (S2.5.7; L = list, A = array, S = string)

| Operation | list in | array / range in | string in | mixed |
|---|---|---|---|---|
| `sort`, `reverse`, `unique`, `take`, `drop`, `slice`, `[i to j]` | L | A | S | — |
| mapping pipe, `that` | L | A | S iff all results strings, else A | — |
| vector arithmetic, masks (`eq` …), `abs`… | L | A | — | list ⊕ array → A; list ⊕ scalar → L |
| `++` | L | A | text concat | any A/range operand → A |
| set ops `\| & !` | L | A | — | any A → A |
| `zip` | L (pairs A) | A | A | any A → A |
| `fill(n, x)` | L | A | A | — |
| `for …` (all clauses), blocks, `(…)` | L | L | L | — |
| `split`, `find`, `range()`, `varg()`, rest, `content(e)`, `keys`/`values`/`names`, `math.random` | — | A | A | — |
| scalar / `null` source | — | — | — | one member / empty (S10.1.2v2, S10.1.5v2) |

### 3.2 Landing → what happens to a list

| Position | Effect |
|---|---|
| item of `(…)`, `[…]`, block statement, content child (expression form) | splices, even nothing (§1.3) |
| `push`/`splice`/`a[i] = v` on an array or list | splices a list value; a collapsed `null` inserts `null` |
| map field, attribute, object field | stored as the array image (§1.6) |
| binding, argument, return, operand, subscript, `if`/`match` branch | passes through; empty/one-item forms have collapsed |
| `print`/`string`/`format` | as an array; top-level content one item per line |
| into JS | `is_spreadable` cleared |

---

## 4. Gates and measurement

- `make test-lambda-baseline` 100% on both tiers after every phase; the new
  fixtures run under `LAMBDA_TIER=jit` and `LAMBDA_TIER=interp` and must be
  byte-identical (S1.6). `make test-radiant-baseline` after P1 and P3 (content
  and render sites). The JS gate (`test_js_*`, Test262 subset) after P1.
- Forced-GC pass on the new fixtures (`LAMBDA_GC_FORCE_EVERY=1`, with
  `POISON_FREED=1`): the image copy in §1.6 and the finish modes in §1.3
  allocate; every new allocation site takes `RootFrame`/`Rooted` per rule 15.
- MIR emission tests (`test/mir/`): the emitter changes in P1/P2/P4 must keep
  the emission ratchet.
- Performance: **release build only**, one benchmark run at a time; compare
  the container-heavy rows (json2, havlak, deltablue, richards, the text rows)
  before P1 and after P2. The per-item kind check in the sequence append
  replaces today's `is_content` check on array-typed items, so the expected
  delta is noise; the `array_end` promotion change (lists of numbers stay
  generic) is the one thing to watch — if a comprehension-matrix row moves,
  promote list results too and carry the bit on `ArrayNum`.

---

## 5. Risks and migration

- **JS Arguments marker.** The rename touches ~35 sites; a missed site reads
  the bit under its old name and silently changes Arguments behaviour — the
  P0 JS guard exists for this. `array_flags` has no free bits (memory note),
  so keeping the bit under a new name is the only zero-cost route.
- **Goldens that encode today's behaviour.** Bound for-results as `[v]` or
  `[]`, `order by` arrays, `sort` of lists as arrays, `s[-1]` as `""`,
  `that` on a scalar as `[5]`, `keys`… Every diff is triaged against a
  ruling; none is accepted "because it was green".
- **The n = 1 edge** (S2.5.5v2) may silently change a script that iterates a
  computed list: `for (y in take(xs, 1))` runs zero times after P1. The
  baseline sweep must grep for `for (… in <call/for-expr>)` over list
  sources and wrap them in `[…]` where the golden shows the change.
- **Content-flag consumers outside the runtime** (`collection_io.cpp`,
  `render_map.cpp`, Radiant's editing paths): each must find its "is this
  content" answer in the container. Do this before deleting the field's
  Lambda meaning, not after.
- **Input parsers.** `input-ics.cpp`/`input-mark.cpp` mix MarkBuilder appends
  with `list_push` (D2.6.5v3 footnote); LaTeX stays verbatim by design.
- **Type-family migration** is small in the tree (five retired spellings, ~40
  `T[n]` uses) but user-visible: `int[0]`/`int[1]` change meaning; the
  parser's diagnostic for `[n+]`/`[n, m]` must name `{n,}`/`{n,m}`.
- **Tier parity.** Every emitter change lands in both `transpile-mir.cpp` and
  `interp.cpp` in the same commit; a fixture green on one tier only is a
  failure.

---

## 6. Progress

| Phase | Status | Fixtures | Baseline (jit / interp) | Notes |
|---|---|---|---|---|
| P0 fixtures | **done 2026-09-22** | 11 in `ext/`, 2 in `proc-ext/`, 2 negative | n/a (ext, red by design) | goldens from the rulings; all parse except `type_families` (P5 syntax) |
| P1 kind bit + appends + finish modes + library migration | **done 2026-09-22, committed `fc3153b3b`** (see §7–§9; residue §10) | `list_kind_literal` green both tiers and moved to the baseline; new baseline fixtures `list_positional_verbatim`, `list_declarations`; `list_var_mutation` blocked by LR12-27 (§9); `list_collapse_void` green except its P2 `take` lines; new baseline fixture `ndim_sequence_ops` | official `make test-lambda-baseline`: every remaining failure fails on HEAD, flakes, or arrived with the 17:15 upstream rebase (§9) | library + test migration (§8); positional sites, S2.5.4 declarations, type names (§9) |
| P2 transforms | **done in the working tree 2026-09-22** (see §11) | `list_kind_transform`, `list_kind_for_clauses`, `pipe_that_kind`, `list_collapse_void` green both tiers and under forced GC; moved to the baseline | official `make test-lambda-baseline`: only the pre-existing failures (§11) | 24 goldens followed the rulings (grouping-only diffs); query result kind unruled ([LR05-13](../Lambda_Issue_Ledger.md#lr05-13)) |
| P3 landing + content writes | **done in the working tree 2026-09-23** (see §12) | `list_kind_landing`, `content_normalize` green both tiers and under forced GC, moved to the baseline; `content_writes` likewise, to `test/lambda/proc`; `list_var_mutation` green except its three LR12-27 lines | official `make test-lambda-baseline`: only the pre-existing failures (§12) | image = one-level copy (`slot_image`); 2 goldens and 2 scripts followed the rulings |
| P4 text, `++`, `*` | **done in the working tree 2026-09-23** (see §13) | `text_sequence`, `concat_table`, `spread_star` green on interp, jit and auto and under forced GC, moved to the baseline; `spread_star` pinned in the tier-parity table | official `make test-lambda-baseline`: **5826 passed, 0 failed** | LR05-9 fixed with the `++` table; 9 proc scripts migrated off `++` string interpolation; 3 fixtures pinned retired rules and were rewritten |
| P5 type families + migration | not started | | | `make generate-grammar` |
| P6 close-out | not started | | | Appendix A, ledger, rename file |

---

## 7. P1 status (2026-09-22)

**Landed in the working tree (both tiers, uncommitted):** `is_content` renamed
`is_js_arguments` (JS Arguments marker only; JS gate spot-checked); the kind
bit `is_spreadable` set by `list_end` and every former `is_content = 1`
producer; `array_push` is the sequence append (splices a list by its bit,
skips the item-position marker, captures spliced items); two finish modes —
`list_end` / `list_collapse_value` (value position) and `list_end_item` /
`list_collapse_item` (item position), chosen by the producer's own position
through `MirTranspiler::list_item_producer` / `InterpState::list_item_producer`
(`mir_box_sequence_item`, `interp_eval_sequence_item`); for-expressions collapse
whatever their clauses; list literals and functional blocks use the sequence
append (no normalization), the script root keeps the content append;
`array_end` no longer returns `ITEM_NULL_SPREADABLE` (the marker survives only
as the item-position finish, consumed by the enclosing append — the plan's
"retire" became "confine"); the syntactic `has_spreadable` push selection is
gone (the scan still gates the compact/N-D paths; pipe items keep
`array_push_spread_all` until P2); `()` parses as the empty list; `type()`
names `list`, `is list` tests the bit; `child_query_collect` distributes only
over a list's container items; async launch arguments use the verbatim append.

**Baseline (`test_lambda_gtest`, debug, default tier):** 132 failures. Against a
debug build of HEAD without these changes (`temp/p1_ctl_debug_lambda.exe`): 69
fail on HEAD too — a flaky satellite-compile crash (`prepass_collect_call_sites`
reads a stale AST param; `pdf/phase1_skeleton` crashes 4/6 on HEAD) and a
deterministic `conc/*` "invalid task handle" on the interp/auto tiers (the
synchronous `main` satellite re-loads the `worker` module, whose layout BSS is
then read as zero: "sealed layout changed … requested var_count=0"). 52 are
regressions from P1, in two classes, both direct consequences of S2.5.1v2 /
S2.5.5v2:

1. *Goldens that encode the old behaviour* (~20 tests): top-level lists print
   as content (`for … order by` at the top level), empty array filters are
   `[]`, bound for-results spread in array literals (`[t1, t2, t3]` with a
   list-returning function), one-item for-results are their item.
2. *Library code that treats a bound for-expression as an array* (chart,
   latex, math, pdf, editing, dom-derive; ~120 `let x = (for …)` sites in
   `lambda/package/`, more as bare function results): at one item the list
   collapses (`hits[0]` on the collapsed map is `null` —
   `pdf/resolve.ls:51 find_obj`; `latex children_array` returns the string, so
   `[0]` is its first character), at zero it is `null`. The migration is not
   mechanical: a data use needs `[for …]`, but a content use (building element
   children) needs the list, and some sites are both.

**Spec conflict found:** S8.3.3 ("for-expressions and spreads splice at the
construction site — by the time `count` applies, there is one value"; pinned
by `len_iter_law.ls` `len([1, bound_for, 4])` = 3) contradicts S2.5.1v2 /
S2.5.6 (a bound list spreads wherever it lands: 4). Needs S8.3.3v2.

## 8. P1 library and test migration (2026-09-22, USER chose "migrate now")

**Finding the sites.** A temporary compile-time log in `transpile_for` and in
the multi-value list/block paths (removed afterwards) recorded every list
producer that finishes in **value position** — exactly the sites whose meaning
S2.5.5v2 changed. Importing every `lambda/package` module under
`LAMBDA_TIER=jit` gave **205 sites** (186 for-expressions, 19 multi-value
blocks); no other list-literal site existed. Every other for-expression sits in
an item position, where splicing is unchanged.

**Rule applied.** The old value-position for-result was an array (never
collapsed, `[]` when empty) that nevertheless spread in content and `(…)`
lists. So: a value-position for-result used as **data** (indexed, `len`,
iterated, `join`/`sum`/`max`/`min`, piped, filtered, spread into `map([...])`,
stored in a field, passed to a helper that iterates it) becomes `[for …]`,
which restores the old array exactly; a result placed **only as content**
keeps the list (content drops `null` and splices, so collapse is invisible
there). Callers of every function whose tail was wrapped were checked for
content or `(…)` placement: none. 182 for-sites wrapped (152 by a
paren-to-bracket / bracket-insertion tool, 36 by hand where the end was
multi-line or an element literal), across chart, pdf, math, latex, openapi,
dom, editor. Kept: `graph/transform/html.ls:357,360` (content branch),
`dom/details.ls:32` (a `pn` effect loop).

**Effect-sequencing blocks (`dom`).** A functional block now yields every
statement's value (S2.5.3, S2.5.1v2), so
`fn abort(host, token) { dom.edit_abort_transaction(host, token)  false }`
returns `(null, false)` — a truthy list. Ten such blocks in `dom/` bind the
effect instead (`let _aborted = …`, S2.5.4), which is correct under the ruling
and under any future "blocks drop null" ruling. `let _` cannot repeat in one
scope (E209), hence distinct names. **Raised with the user** as a language
consequence.

**Runtime bug exposed and fixed (S1.6).** `[for (i …) [x, y]]` is an array
literal, so `array_end` promotes uniform numeric rows to one 2-D `ArrayNum`;
`reverse`, `sort`, `unique`, `take`, `take_last`, `drop`, `slice` walked its
flat storage (`len(reverse([[1,2],[3,4],[5,6]]))` was 6 — also at HEAD). They
now unstack an N-D input into its leading-axis rows (`array_num_get`, the view
indexing and `for … in` already use) and re-dispatch (`VECTOR_NDIM_ROWS`,
`lambda-vector.cpp`). Fixture `test/lambda/ndim_sequence_ops.ls`.

**Tests.** Goldens updated where the test's subject is list/for/pipe semantics
and the new output is the ruled one: `for_clauses_test` (top-level ordered
fors print as content; `[[84]]` → `[84]`, the LR05-10 `order by` fix),
`pipe_where` and `that_implicit_name` (empty array pipe/`that` is `[]`),
`map_spread_len` and `closure_capture_sites` (a bound or returned list spreads
in an array literal), `for_group_test`, `keyword_binding_clause_priority`,
`for_expr_content` (top-level lists are content), `len_iter_law` (S8.3.3v2),
`proc/function_colour_poly`. Scripts migrated with `[for …]` where the list
shape is incidental to the test's subject: `for_at_pairs`,
`io_sqlite_for_clauses` (top-level string lists would merge as content),
`complex_iot_report_html` (68 data sites), `dom_derive_{query,chardata,
traversal}` (`fn descendants(n) { [for (c …) (c, *descendants(c))] }` — the
recursive `(c, descendants(c))` now carried `null` for leaves, S2.5.5v2),
`pdf/phase7_text_state`, `proc/type_infer_carrier_lanes`. Reverted one script
migration (`function_colour_poly`): wrapping a `function`-typed call's result
in `[…]` made the checker infer `(int | error)[]` and raise E208 where the
bare for-expression did not — an asymmetry worth a ledger look.

**Spec.** S8.3.3v2 (a list splices wherever it lands; `count` of a list is its
`len`) and the S12.3.5v2 clarification that `*x` is the list of `x`'s items
(which yields every case the ruling lists) — raised with the user.

**Gate.** Debug `test_lambda_gtest` 52 failures, compared one by one against a
debug HEAD binary run with HEAD's scripts, goldens and packages
(`LAMBDA_HOME=temp/p1_pristine/lambda`): 44 fail there too, 8 pass on both on
re-run (satellite-compile race under parallel load). **No new failure.** The
pre-existing debug failures (flaky satellite teardown crash — `pdf/*`,
`graph/*`; deterministic `conc/*` "invalid task handle" on interp/auto; large
module MIR-interp crash importing `lambda.doc.math.metrics`) are unrelated to
this plan.

## 9. P1 official gate (2026-09-22)

The full `make test-lambda-baseline` exercised suites the direct runs had not,
and three P1 defects surfaced. All three are fixed.

**Positional sites (D2.6.5v3).** `array_push` had spliced only `is_content`
lists; P1 made it splice every list, and code that built positional
structures with it began splitting for-results. A for-expression passed as one
argument became several in the rest list (`count_args(for (x in [1, 2]) x)`
was 2, the LR09-9 invariant, std `variadic_args`). Every positional site now
uses the verbatim append `array_push_verbatim` (renamed from
`array_push_argument`): rest and argument lists (JIT, dynamic-call adapter,
dynamic apply, JS timer packs, concurrency args), group/join/order keys and
rows, path keys, query matches, clones, set operators, element-copying
transforms, `zip` pairs, index-addressed vector results, and the hosted-Python
list operations. The sequence and verbatim appends share one single-item
store (`array_append_one`); the verbatim append now rehomes 64-bit scalars
through `array_set` like every other store. The sequence append tests the
empty marker and the list bit before a native-lane store, which had kept a
list whole in a container lane. The same sweep fixed two HEAD bugs: a
list-valued `order by` key did not sort (`[3, 1, 2]`), and a list-valued
`group by` key made the wrong groups. Fixture:
`test/lambda/list_positional_verbatim.ls`.

**Declarations (S2.5.4, [LR02-19](../Lambda_Issue_Ledger.md)).** The let-group
builder kept only the last non-declaration item, so `(let x = 1, x, 2)` was
`2` (HEAD too). A group of one declaration evaluated the declarator
(`(let x = 5)` was `5`), and a block of one declaration took the declaration's
type, sending `[{ let x = 5 }, 9]` down the compact int lane (`[0, 9]` on the
interpreter). A declaration-only group or block is now `null` and splices
nothing in an item position. An instrumented scan found no multi-item
let-group or lone-declaration group in the test corpus or the packages.
Fixture: `test/lambda/list_declarations.ls`.

**Handlers (S12.1.3, [LR12-29](../Lambda_Issue_Ledger.md)).** The Radiant
gate showed paste into a text field doing nothing. The interpreter ran `on`
handler bodies as functional blocks; once blocks kept `null`, the `<input>`
keydown handler returned `(null, verdict)`, a truthy list read as "handled".
Handler frames are now procedural in the interpreter, as they always were in
MIR. The todo apps behind the UI tests (`test/lambda/ui/todo.ls`, `todo2.ls`)
held value-position for-results used as data (`len(for …)`,
`~.items = for …`, `max(for …)`); they now use `[for …]`, the library rule.

**Type names.** `print(type(l))` and `string(type(l))` said `array` for a
list while the top-level printer said `list`. Printing, `string()` and
`name()` now share `type_alias_name` (`integer`, `number`, `list`), which
also retired their copies of the `integer`/`number` special cases.

**Goldens and tests.** Five std goldens follow the rulings: ordered
for-expressions spread and print as content (S2.5.2v2; `for_clauses`,
`for_expression`, `for_group`, `for_let`) and an array filtered to nothing is
`[]` (S10.1.5v2; `where_filter`). The parser POC's
`(// comment\n)` rejection became an accept test for `()` (S2.5.5v2); the
Tree-sitter reference grammar still has no list literal at all
([LR02-18](../Lambda_Issue_Ledger.md)), so `()` there waits for P5.
`list_kind_literal` moved to the baseline.

**Residue, all outside P1.** Each was compared against a debug build of the
pre-rebase HEAD and of the upstream HEAD pulled in at 17:15 (`bb3909e36`,
worktree `temp/tune14/head_control`):
- The forced-GC sweep fails 62–64 cases against 61–73 on HEAD, with the same
  signatures: a nondeterministic memtrack leak line and a shutdown SIGTRAP.
- `dom_document_cookie`, `mathlive` and `tune23_record_constructor` fail on
  HEAD too. MathLive shows 90 baseline regressions with P1 against 95–101
  without, and every P1 regression is also a HEAD regression.
- Arrived with the upstream rebase, not with P1: the `cow_move_out_bind` crash
  (opt contract, tier parity, `proc_cow_move_out_bind`), the
  `js_corpus_array_methods` ratchet (+419 instructions), and the
  `graph_layout` goldens. The upstream HEAD fails all three identically.
- `list_var_mutation` stays red. `push` onto an unannotated int array is a
  silent no-op ([LR12-27](../Lambda_Issue_Ledger.md), not in this plan), which
  blocks three of its lines. The field line is P3.
- A satellite teardown race in `test-batch` ([LR01-15](../Lambda_Issue_Ledger.md))
  that P1's timing made frequent (7 of 10 runs of one std pair, 3 of 10 on
  the pre-rebase HEAD) took out 41 std cases in one gate run. Fixed by
  quiescing satellite workers in `runtime_reset_heap`.
- Radiant (`make test-radiant-baseline`): with the handler fix and the todo
  migration, the UI automation baseline passes 119 of 119 standalone. The
  layout runner needs `NODE_PATH=<repo>/node_modules`, because
  `test/layout` links into the lambda-test checkout, which has no
  `puppeteer`. Every remaining failure fails the same way on upstream HEAD, or
  passes standalone and flakes only under the gate's load:
  - DOM UI: `dom_codemirror_type`, `dom_pkg_context_menu`,
    `dom_pkg_focus_policy`, `dom_pkg_keyboard_activation`.
  - View command: three leak checks and the script-budget test.
  - `radiant_view_pdf2_iframe_interaction`.
  - The page snapshot, which is stale against the lambda-test data (4 new
    pages; same five per-file drops).
- Rebuilding `lang-python.dylib` for the verbatim switch exposed a latent
  linkage break from 2026-09-18 ([LR01-14](../Lambda_Issue_Ledger.md), fixed
  in `py_runtime.h`). `test_py_gtest` passes 39 of 43, and the four failures
  are identical on upstream HEAD.

## 10. Status check after the P1 commit (2026-09-22)

P0 and P1 were committed as `fc3153b3b` ("lambda list semantics change"); the
later `list fixes` commits (`e3b5723bf`, `42a3d2a82`) carry the effect-colour
work (LR12-30) and the auto-tier key-linking fix (LR01-16), not plan phases.
The P2 request was interrupted before any P2 change was made.

**Fixtures on a debug build of `43fd2c3d1`** (golden lines matched, compared by
position; driver `temp/list_status/run_fixtures.py`):

| Fixture | Phase | jit | interp | What still fails |
|---|---|---|---|---|
| `list_kind_literal`, `list_positional_verbatim`, `list_declarations`, `ndim_sequence_ops` | P1 | pass | pass | — (baseline) |
| `ext/list_collapse_void` | P1/P2 | 24/27 | 24/27 | the one-item `take` lines |
| `ext/list_kind_landing` | P1/P3 | 14/18 | 14/18 | print/format half passes; field, attribute and object stores keep the list (LR12-28) |
| `ext/list_kind_for_clauses` | P2 | 14/16 | 14/16 | `order by` / `limit` return arrays |
| `ext/list_kind_transform` | P2 | 14/32 | 14/32 | per-function result kinds |
| `ext/pipe_that_kind` | P2 | 8/21 | 8/21 | pipe and `that` result kinds, scalar sources |
| `ext/content_normalize` | P3 | 6/7 | 6/7 | lone `""`, binary merge |
| `proc-ext/content_writes` | P3 | 1/7 | 1/7 | content writes do not re-normalize |
| `proc-ext/list_var_mutation` | P1/P3 | 2/7 | 2/7 | three lines LR12-27, two lines P3 |
| `ext/text_sequence` | P4 | 8/17 | 8/17 | text as a sequence |
| `ext/concat_table` | P4 | 10/17 | 10/17 | the `++` table, LR05-9 |
| `ext/spread_star` | P4 | 10/18 | 14/18 | tiers differ: the JIT `*` marks the pooled literal `lit()` returns (LR05-12) |
| `ext/type_families` | P5 | parse error | parse error | `T{n,m}` is not yet syntax |

The negative fixtures `occurrence_retired_{plus,range}` are still accepted (P5).

**P1 residue.** Four P1 steps did not land as §2 describes them; none changes
a ruled behaviour, and each stays open:

1. **JS guard test not written.** The `test_js_*` case (a Lambda list reaches
   JS as a plain array; Arguments objects keep their snapshot length) does not
   exist; the rename was checked by spot runs of the JS gate only.
2. **JS boundary (P1 step 8).** Only the `split` result cleared
   `is_spreadable` on its way into JS (`js_runtime.cpp:23334`), not every
   Lambda value published into JS. P2 made `split` build an array, so that
   clear is gone; the general boundary is still open.
3. **Content-flag consumers (P1 step 7).** `collection_io.cpp:73`, `:86` and
   `render_map.cpp:577`, `:593`, `:611` now test the list bit under its new
   name; the container/call-site audit was not done. The UI and DOM gates
   pass (119/119, 127/127).
4. **Root printing (P1 step 5).** `print_root_item` keeps its
   one-item-per-line branch, keyed on the list bit, instead of moving to the
   runner. Output matches S2.5.6 (a list reaching the root is the script's
   content), so only the structure differs from §1.7.

## 11. P2 status (2026-09-22)

**Landed in the working tree (both tiers, uncommitted).**

*One finish for every result kind (§1.4).* `item_is_list` (the bit on a
generic or packed array), `seq_finish_kind(result, as_list)` (a list collapses
through `list_collapse_value`; an array clears the bit), and
`seq_operands_are_lists(a, b)` (a list only when some operand is a list and none
is of array kind; scalars and `null` are neutral), all in
`lambda-data-runtime.cpp`. Every multi-input operation decides the kind before
it allocates, since a collection may move its operands.

*Transforms.* `reverse`, `sort` (both forms), `unique`, `take`, `drop`, and
`slice` (which `[i to j]` calls) finish in the source's kind; `reverse`,
`take`, `drop`, and `slice` share one rooted copy (`vector_select_items`,
`vector_select_range`) and one C15 count check (`vector_count_arg`).
Element-wise arithmetic (`vec_model_op`), masks (`vec_cmp`), and the unary
element-wise functions (`abs`, `round`, `floor`, `ceil`, the math functions,
`sign`, `clip`, negation, `cumsum`, `cumprod`) keep the kind (S7.10.5v2). Masks
gained a generic path: a list, mixed array, or range compares item by item —
before, only a packed array did, and `(1, 2, 3) eq 2` compared the whole list
to `2`. `zip` and the set operators are lists only for two lists; `fill`
follows its item. `split` and `find` build arrays (S2.5.7), `[]` when nothing
matches. `(1 to 3)[int]` filters the range.

*Pipes and `that`.* Both tiers finish through `pipe_end(result, source)`: a
list, `null`, or scalar source collapses (`5 |> ~ + 1` is `6`,
`5 that ~ > 9` is `null`); an array, range, map, or element source gives an
array, `[]` when empty; text keeps its array result until P4. A pipe item in
an array literal is placed as a value like any other: the syntactic
`array_push_spread_all` special case, and the function, are gone.

*For-expression windows.* `limit` and `offset` trim the raw stream in place
through `for_window` in both tiers, ordered or not, so the single
`list_collapse_*` decides the kind. Calling `take`/`drop` on the stream, as
the unordered path did, would now collapse twice and turn a lone row into a
list. `order by` no longer clears the bit. Counts follow take/drop's contract
on both paths, so an ordered `limit -1` is now an error; it was silently a
limit of 0. `fn_take_last` and `emit_machine_count` lost their last users and
were removed.

*Typed admission (S11.1.6).* `runtime_type_admit_array_env` carries the bit to
the fresh container when admission packs or rebuilds a list, so
`let t: int[] = (4, 5)` stays a list and `fn f(xs: int[])` receives one.

*Checker.* A selection can leave a list one item or none, which is an item or
`null`, not a container. `take`, `drop`, and `unique` (new registry kind
`SYS_RESULT_SELECTION_OF_ARGUMENT`) and `slice` are therefore typed open when
the source may hold a list (`type_may_hold_list`: any array type, `any`, and
unions that reach them, since an annotation admits lists). `fill` with such an
item is open too, because a list item splices. `pipe_collection_result_type`
types a pipe or filter: a scalar source maps to the body's type and filters to
`T?`, `null` gives `null`, a range, map, element, or text source filters to an
array, and a filter over an array type or a pipe over an open source is open.
Without this, `len(take(L, 1))` would unbox `10` as a container (`fn_len_l`).
Every benchmark `fill` passes a scalar item (228 sites), so the open `fill`
type costs them nothing.

*Latent P1 defect.* `list_end` and `list_collapse` returned a one-item list's
item straight from its storage; the item now goes through `array_get`, which
re-homes a wide scalar held in the dying container (D2.5.2v2).

**Where P2 differs from §1.**
- A packed array can be a list. A list-kind vector result keeps its compact
  `ArrayNum` and carries the bit; `item_is_list`, `type()`, `is list`, the
  sequence append, and the root printer all read the bit on both
  representations. §1.5's "lists stay generic" still holds for list producers.
- Query results (`e[T]`, `e?T`) still carry the bit at any length. Their kind
  is unruled ([LR05-13](../Lambda_Issue_Ledger.md#lr05-13)), and a pipe or
  `that` over one now yields a collapsing list where it used to yield an array
  (`page?<p> |> len(~)` prints as content).

**Fixtures and goldens.** `list_kind_transform`, `list_kind_for_clauses`,
`pipe_that_kind`, and `list_collapse_void` pass on both tiers and under
`LAMBDA_GC_FORCE_EVERY=1` with poisoned frees, and moved to the baseline.
The first gate run had 26 new failures. Every changed golden differs from its
old output only in grouping (brackets, quotes, line breaks — checked
mechanically), and each follows a ruling: `split` and `find` results are
arrays, so they no longer splice or merge into top-level strings
(`split_null_concat`, `string_funcs`, `string_pattern_ops`, `typed_array_split`,
`pipe_sysfunc`, `find_replace_options`, `tune22_text_paths`, std
`string_pattern_funcs`, `string_split_join`); a pipe or `that` over an array
is one array item and over a scalar is its value (`chained_comparisons`,
`complex`, `member_int_steps`, `path_index_capture`, `sized_numeric_type_annot`,
std `pipe_operator`); an array's transform is an array (`vector_sys_func`, std
`collection_construction`, `collection_reverse`, `collection_sort`,
`take_drop`, `zip`); a pipe over a query result is a list (`query`, see
LR05-13). `pipe_spread` pinned the retired "pipes spread their arrays" rule; it
now cites S10.1.2v2/S10.1.5v2 and gained list-source cases that do spread.
`tune26_split_string_lane` (and its opt contract, which keeps
`fn_mutable_value_calls == 0`) prints the split array as one item.
`vector_performance` and `ndim_sequence_ops` kept their goldens: the first now
builds its vectors with `[for …]` (the list shape was incidental), the second
counts rows with `len(take(nd, 2))`.

**Gate.** Official `make test-lambda-baseline` after the golden updates: std
105/105, MathLive 921/921, MIR emission 172/172, JS opt 79/79. The remaining
failures are the pre-existing set from the post-LR01-16 gate: 22 in
`test_lambda_gtest` (13 `conc/*` and 3 proc async on the interpreter, LR01-17;
4 graph goldens; `cow_move_out_bind` and Tune27 through it), `LambdaOptCow.MoveOutBindsBorrow`,
the `js_corpus_array_methods` ratchet, `dom_document_cookie`, and three forced-GC
cases. Two other forced-GC cases failed once under the gate's load and pass 5 of 5
standalone. `make test-radiant-baseline` (run although §4 schedules it only after
P1 and P3, because `split`, `find`, and the transforms now land in content as
arrays, S2.6.3): layout baseline 2934 passed and 0 failed, UI automation 119/119,
DOM UI 125 passed and 0 failed, render, WPT, and fuzz suites green. Its two red
groups match the P1 run exactly: the page suite's five per-page drops, with the
same numbers as `temp/p1_radiant2.log` (stale snapshot), and the view command's
three memtrack leak checks plus the script-budget test.

**Performance (§4).** Release builds A/B'd on one machine, alternating run by
run, `LAMBDA_TIER=jit`, the median of 5 runs (11 for the rows that first moved
by more than 2%): the pre-P1 control (`bb3909e36`, whose engine sources match
`dd32bf595`; release build in `temp/tune14/head_control`, which now holds a
release binary) against P2 (a worktree carrying this patch). Every row's output
is identical, and every ratio is noise:

| Row | pre-P1 ms | P2 ms | P2/pre |
|---|---:|---:|---:|
| awfy/json2 | 2.6 | 2.5 | 0.99 |
| awfy/havlak2 | 61.1 | 60.7 | 0.99 |
| awfy/deltablue2 | 28.2 | 28.4 | 1.01 |
| awfy/richards2 | 370.1 | 371.5 | 1.00 |
| awfy/cd2 | 204.7 | 198.8 | 0.97 |
| jetstream/deltablue2 | 25.8 | 26.0 | 1.01 |
| jetstream/richards2 | 409.2 | 406.1 | 0.99 |
| jetstream/splay2 | 345.6 | 343.9 | 1.00 |
| text/fast_diff2 | 58.1 | 58.2 | 1.00 |
| text/microdiff2 | 62.8 | 62.9 | 1.00 |
| text/log_pipeline2 | 2807.5 | 2788.2 | 0.99 |
| text/text_search2 | 6075.4 | 5869.8 | 0.97 |
| text/three_way_merge2 | 5528.3 | 5474.9 | 0.99 |
| text/prettier_ast2 | 634.1 | 637.3 | 1.01 |

The machine carried another session's compile load during the first pass
(load average 9–20), so absolute times are high; the alternation keeps the
ratios comparable. No benchmark uses a pipe with `~`, `that`, `take`, `drop`,
or `unique`, and `slice` almost only on text, so the checker's open result
types do not reach them.

## 12. P3 status (2026-09-23)

**Landed in the working tree (both tiers, uncommitted).**

*Slot stores take the array image (S2.5.6, §1.6).* `slot_image(value)`
(`lambda-eval.cpp`) returns a list's array image, a one-level copy
(`cow_clone_one_level`, children marked shared) with the kind bit clear, and
any other value unchanged, so the source list keeps its kind. The runtime
converts at `fn_map_set`'s entry — which also covers `map_set_cow`, the
checked map setters, `map_literal_put`, and the `put`/`del` write set — and at
`vmap_set`'s. The MIR converts where it evaluates a slot value
(`mir_box_slot_value`): map literal fields (the inline container arm and the
boxed fallback), element attributes, object literal fields and defaults, and
every member-assignment path (edit bridge, typed path store, COW path set,
marked root, the T20-1d guarded store, the fallback setter). The call is
emitted only when the value's static type may hold a list
(`lambda_type_may_hold_list`, promoted from the checker to `type_contract`).
The trusted-contract direct field writer cannot build the image, so a
container field whose value may be a list takes the setter instead; a map
value can never be a list, so record-shaped fields keep the direct write. The
interpreter converts each literal value as it roots it. Record results
(`mir_scalar_record_shape`) admit only int, bool, and string fields, so no
list reaches them.

*Insertion splices a list (S2.5.6).* `a[i] = list` replaces one item with the
list's items (`array_splice_list_at`): a packed array keeps its lane when every
item fits it (`array_num_admits_value`, split out of
`array_num_store_admitted`), otherwise it widens, and a generic array is
rebuilt from a snapshot in its own lane. A typed array admits each item, not
the list (`runtime_admit_list_items`), in the checked setter and in typed
`push`, which admits every item before it appends the first, so a bad item
cannot leave a partial append. Untyped `push` already spliced (P1).

*Content stays normalized (S2.2.3, S2.6.2, S2.6.4, S2.6.5).* The content append
(`list_push`) drops `""`, merges adjacent binaries (`heap_binary_concat`), and
merges strings wherever content is built: the `input_context` gate — the
retired `disable_string_merging` flag in disguise (D2.6.5v3) — is gone, and the
merged string is allocated by the owner (the runtime context's allocator, the
input pool, or the heap when there is no context). `e[i] = v` on an element
(`element_content_set`) snapshots the children and appends them back through
the content append, so `null`/`""` removes the child, a list splices, and
neighbours of a removed separator merge; `push` on an element appends content.
The `put`/`del` write set rebuilds an element through the content append and
an array through the verbatim append, splicing an inserted list with the
sequence append.

*Appends that built sequences with the content append (D2.6.5v3).* `find`'s
match lists (runtime and RE2) and `path.c`'s directory listings and glob
matches now use `array_push_verbatim`; `input-ics.cpp` builds its component
sequence with the pool-owned verbatim `array_append` (the static input library
has no runtime append). `input-mark.cpp`'s `list_push` is element content, the
content append's own case.

**Image strategy.** §1.6 left the choice to measurement. No benchmark stores a
list into a slot, and a list is transient and small, so the one-level copy was
taken over a second header sharing the items buffer, which would need a COW
protocol on the shared buffer. What every write pays is the check, measured
below.

**Fixtures and goldens.** `list_kind_landing` and `content_normalize` (to
`test/lambda/`) and `content_writes` (to `test/lambda/proc/`) pass on both
tiers and under forced GC with poisoned frees. `list_var_mutation` passes
except its three LR12-27 lines (`push` onto an unannotated `var a = [1]`), and
stays in `proc-ext`. A probe (`temp/p2/p3_probe.ls`) covered nested
`m.a.b = list`, an attribute write, typed `int[]` index writes and pushes (the
lane survives), a typed record field, and a removed separator element. Gate
triage: `expr_stam` stores a for-result in a field, which is now the array
image (golden updated); `string_pattern_ops` printed a bare `""` at the top
level, which content now drops, so the case reads `[replace(…)]`;
`test_item_repr_gtest` built element content from zeroed slots, which read as
`null` and a content write now removes, so the fixture seeds two integers;
the `lambda_corpus_prettier_ast` ratchet grew by 17 instructions (the
`slot_image` calls; budget updated with a note).

**The direct field writer, and what the A/B caught.** The first cut sent a
container-field store whose value may hold a list to the checked setter,
because the raw writer cannot build an image. That cost awfy/havlak2 26% and
awfy/cd2 15% (`s.items = items`, `tree.vals = vals`), which the release A/B
found and a build with only that skip reverted confirmed (1.02 and 0.975).
The writer keeps the direct store instead: an inline test — no tag byte, the
kind bit in the container's flags, an array or packed-array header — takes a
list to `lambda_direct_field_store_image`, which re-derives the slot from the
rooted owner because the image copy may move it, and everything else stores
raw. `mir_box_slot_value` tests inline for the same reason: with a
`slot_image` call at every site, awfy/deltablue2 ran 3.3% slower, since
`w.vars[vid].constraints = cs` stores an array-typed value that (after P3)
can never be a list. Emission grew: `lambda_corpus_deltablue` +43 and
`lambda_corpus_prettier_ast` +93 module instructions; both budgets are
updated with that note.

**Gate.** Official `make test-lambda-baseline`: std 105/105, MathLive 921/921,
MIR emission 172/172, the ratchet green except the pre-existing
`js_corpus_array_methods`. The remaining failures are the pre-existing set
(§11) plus two known flakes that also fail on the pre-P1 control:
`proc_tune23_record_constructor` on the `auto` tier (a satellite compile of
`forward_column` reports an unavailable ITEM->INT_LANE transition; a pre-P1
debug control logs it on every run and prints its output only sometimes) and
the `tune30_inline_arg_interval` forced-GC case (1 in 5 standalone).
`make test-radiant-baseline`: layout baseline 2934 passed and 0 failed; the
page suite's five drops and the view command's four leak/budget checks match
the P1 run exactly. Four UI cases failed in the gate run and pass standalone;
the baseline suite then ran 118/119 and 119/119 (a different flake), the DOM
suite 127/127 twice.

**Performance (§4).** Release builds A/B'd on one machine, alternating run by
run, `LAMBDA_TIER=jit`, the median of 5 runs (11 or 21 for the rows that
moved), pre-P1 (`bb3909e36`) against P3. Every row's output is identical and
every ratio is noise: json2 0.97, havlak2 1.01, awfy/deltablue2 1.00,
awfy/richards2 1.01, cd2 0.98, jetstream deltablue2 0.99, richards2 1.01,
splay2 1.00, fast_diff2 1.00, microdiff2 1.01, log_pipeline2 1.00,
text_search2 1.00, three_way_merge2 1.00, prettier_ast2 1.00.

---

## 13. P4 status (2026-09-23)

**Landed in the working tree (both tiers, uncommitted).** The three P4
fixtures are green on `interp`, `jit` and `auto` and under forced GC, and have
moved into `test/lambda/`. The official `make test-lambda-baseline` run is
**5826 passed, 0 failed**.

*Recovery first.* P2 and P3 were not in the tree at the start of this phase: a
`git pull --tags` on 2026-09-23 07:40 autostashed the 62 uncommitted files and
never restored them (only the staged fixture renames reached the commit). The
stash commit was still reachable, so the work was recovered with
`git stash apply` onto the merged HEAD and its three conflicts resolved —
`lambda-eval.cpp` and `transpile-mir.cpp` took both sides (upstream's certified
spine fast path with P3's `mir_box_slot_value`), and the two MIR budgets were
re-measured on the merged base, where P3 now costs +139 (`deltablue`) and +106
(`prettier_ast`) module instructions rather than +43/+93: upstream's new inline
admission arms added store sites that each carry the inline list test. The
recovered tree gated at 5821 passed with only those two budgets failing. The
recovery branch `p23-recovery` holds the stash commit.

*`*` is a value, not a mark (S12.3.5v2, §1.8).* `item_spread` is deleted.
`*x` builds the list of x's items — `seq_push_spread` splices a list, an array
(generic or packed) or a range, materialized, appends nothing for `null`, and
appends every other value, text included, as one item — and the result
finishes like any other list producer, so `AST_NODE_SPREAD` joined
`mir_is_list_producer` / `interp_is_list_producer` and the two entry points
`seq_spread_item` / `seq_spread_value` pick the item-position or value finish
(S2.5.5v2). Nothing else in either emitter changed: the produced list splices
through the appends that already splice by the kind bit.

The alternative — per-item spread intent carried from the emitter to a
splicing append, which is what §1.8 proposed — was built first and rejected. It
leaves `*` meaning nothing outside a collection, and the ruling is explicit
that `*x` **is** the list of x's items, which `test/lambda/len_iter_law.ls`
already pinned (`let b = *[2, 3]; len([1, b, 4])` is 4). One mechanism also
keeps the two tiers honest. The cost is one array per spread evaluation; no
benchmark uses the operator.

*Text walks as a sequence (S2.5.8, §1.9).* `is_sequence_text_type_id` names the
three kinds S2.5.8 walks, and one macro, `VECTOR_TEXT_ITEMS`, re-dispatches a
text argument over its code points (its bytes, for a binary) exactly as
`VECTOR_NDIM_ROWS` re-dispatches an N-D array over its rows, keeping the
characters rooted for the whole call. `reverse`, `sort`, `sort2` and `unique`
use it; `take`, `drop` and `slice` keep their `fn_substring` fast path for a
string or symbol and use the macro for a binary, which they had no arm for at
all. `seq_finish_text_kind` rebuilds the source's kind when every result item
belongs to it — a selection's items always do, a mapping's may not — and gives
an array otherwise; an empty result is the kind's own empty value, which is
what makes a filter that keeps nothing `""`. For a binary, "belongs to it"
means a byte-valued integer or a whole binary run. `pipe_end` routes a text
source through the same finish.

The checker had to follow: `pipe_collection_result_type` typed a filter or
mapping over text as `array`, and with a string result the JIT folded
`("abc" that ~ == "z") == ""` to **false** while the interpreter answered true —
an S1.6 divergence the fixture caught. A filter over text is now typed as the
source's canonical kind (not the source type: a filtered literal is a different
value) and a mapping as open (`ANY_PIPE`).

`in` is code-point membership: only a one-code-point text can equal one of the
items, so the walk is skipped for everything else and `contains` keeps the
substring test. `item_at_empty_string` is deleted — an out-of-range text
subscript is `null` like every other sequence's (S7.2.1), and `""` stays the
empty *result* of a text operation (S7.10.1v2).

*`++` follows its table (S10.6.1, §1.10).* A sequence operand now decides the
result before any text arm is consulted, which is the whole fix: `[1] ++ "ab"`
is `[1, "ab"]`, not `"[1]ab"`. `join_operand_is_sequence` and
`join_operand_is_scalar` split the cases, `join_push_operand` contributes one
operand's items or the operand itself, and the two-sequence arm moved into
`fn_join_sequences` so the kind decision reads once: both sequences take
S2.5.7's rule through `seq_operands_are_lists`, and a sequence beside a scalar
keeps the sequence's own kind. `null` is the identity for every operand type,
and a map or element is rejected. LR05-9 closed with it: the packed-lane
shortcut required only that both sides be `ARRAY_NUM`, then copied the right
payload under the **left** element type (`[1, 2] ++ [3.5]` was `[1, 2, inf]`);
it now also requires the same element type and a mixed pair takes the generic
item path.

*Migration.* Nine procedural fixtures used `"label=" ++ container` as string
interpolation — the idiom S10.6.1 retires — and now call `string(…)`, which
reproduces their goldens byte for byte. Three fixtures pinned retired rules and
were rewritten with the ruling cited: `string_index_oob` (out of range is
`null`, not `""`), `string_indexable` (the "passthrough functions" section is
now select/reorder, with five golden lines), and `in_container` (`"ell" in
"hello"` is false; the case now shows `contains` beside it). `cross_type_concat`
in the std suite had `42 ++ [1]` and `[1, 2] ++ 3` as errors; both are ruled
values.

**Gate.** Official `make test-lambda-baseline`: 5826 passed, **0 failed** —
`test_lambda_gtest` 991/991, std 105/105, MathLive 921/921, JS 476/476,
forced-GC stress 220/220, MIR ratchet 20/20 (P4 moved no budget: the spread
call site kept its one-call shape). The two flakes §12 recorded did not fire.
`make test-radiant-baseline`: 3680 passed, 3 failed — the layout baseline
70/70 and 2934 cases with 0 failures, UI automation 119/119, DOM UI 125/125,
the view command 55/55, render, WPT and fuzz suites green. The three failures
are the P1 run's own (`temp/p1_radiant2.log`): the two `radiant_view_pdf*`
view fixtures, and the page suite's stale snapshot, whose four per-page drops
carry the same numbers as P1 to the tenth of a percent (`zengarden`, P1's
fifth, now passes).

**Performance (§4).** The pre-P1 control is no longer a usable baseline for a
P4-only reading: the 2026-09-23 upstream merge is between them, and against it
`prettier_ast2` runs 5x and `havlak2` 3.4x faster, which buries a percent. A
second release worktree therefore holds the **pre-P4** tree — the same HEAD
with P2 and P3 applied and P4 absent — and the A/B alternates the two run by
run, `LAMBDA_TIER=jit`, the median of 5. Every row's output is identical and
every ratio is noise:

| Row | pre-P4 ms | P4 ms | P4/ctl |
|---|---:|---:|---:|
| awfy/json2 | 2.0 | 1.9 | 0.96 |
| awfy/havlak2 | 27.9 | 27.8 | 1.00 |
| awfy/deltablue2 | 14.6 | 14.6 | 1.00 |
| awfy/richards2 | 337.3 | 341.9 | 1.01 |
| awfy/cd2 | 163.1 | 162.1 | 0.99 |
| jetstream/deltablue2 | 20.5 | 20.5 | 1.00 |
| jetstream/richards2 | 336.8 | 339.9 | 1.01 |
| jetstream/splay2 | 317.4 | 319.3 | 1.01 |
| text/fast_diff2 | 57.4 | 57.1 | 0.99 |
| text/microdiff2 | 59.9 | 60.5 | 1.01 |
| text/log_pipeline2 | 2072.2 | 2064.3 | 1.00 |
| text/text_search2 | 1783.1 | 1781.8 | 1.00 |
| text/three_way_merge2 | 2233.4 | 2245.3 | 1.01 |
| text/prettier_ast2 | 523.7 | 522.4 | 1.00 |

The text rows are the ones to watch, because `++` now tests both operands for
sequence-ness before reaching its text arm and they use the operator heavily
(`log_pipeline2` 21 times): `fast_diff2` and `three_way_merge2` read +4.1% and
+3.1% against the *pre-P1* control and 0.99/1.01 against this one, which is
what the control is for.
