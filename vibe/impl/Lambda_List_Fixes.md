# Lambda List/Array Kind, Content, Text, `++`, and Type Families — Implementation Plan

> **Status:** DRAFT — not started (2026-09-22)
>
> **Date:** 2026-09-22
>
> **Last verified against the live tree:** 2026-09-22 (anchors below are from
> the tree at that date; re-resolve before editing)
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
| **verbatim** (today `array_push_argument`, `collection_runtime.cpp:365`) | argument and rest lists, `zip` pairs, and every single-value slot after the image conversion (§1.6) | store the item as one value whatever it is |

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

### P0 — Fixtures first

Turn the survey probes (`temp/spec_survey/listarray/`, `…/collections/`,
`…/strings/`, `…/types/`, `temp/ledger_repro/`) into `test/lambda/*.ls` with
`*.txt` goldens written **from the rulings**, not from today's output. They
fail on entry; each later phase turns its subset green. Suggested files:

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
| `proc/list_var_mutation.ls` | kind survives `push`/`splice`/`a[i]=` on a `var` list; COW detach keeps the bit |

Also add the JS guard: a `test_js_*` case that a Lambda list published into
JS is a plain array and that Arguments objects still iterate with a snapshot
length (the renamed bit).

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
| P0 fixtures | not started | — | — | write goldens from the rulings |
| P1 kind bit + appends + finish modes | not started | | | JS rename included |
| P2 transforms | not started | | | |
| P3 landing + content writes | not started | | | choose image strategy by measurement |
| P4 text, `++`, `*` | not started | | | LR05-9 first |
| P5 type families + migration | not started | | | `make generate-grammar` |
| P6 close-out | not started | | | Appendix A, ledger, rename file |
