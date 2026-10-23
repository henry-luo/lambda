# Lambda Type: String and Symbol — Design

**Revision:** 2 (2026-10-23) — consolidation of the string design record.
**Status:** design record; every point below is either ratified in the formal spec or implemented. Unratified or unbuilt items are listed as such in §8.
**Formal linkage:** text scalars and the sequence walk S2.5.8, S8.3.1v3; empty and solid values S2.2.1, S2.2.2v2, S2.2.3; truthiness S3.1v2; content normalization S2.6.2, S2.6.4; positional reads S7.1.1v3, S7.1.2, S7.2.1, S7.2.2; the sys-func return contract S7.4.5, S7.10.1v3, S7.10.3; ordering S6.1.1; concatenation S10.6.1; the pipe and filter S10.1.2v4, S10.1.5v3, S10.1.6; the single-quote lexical rule S16.8.7; name keys S8.2.2v6; member-call form S12.3.3v2; the string library S17.1.1, S17.4.1, S17.6.1, S17.7.1, S17.7.2, S17.8.1; representation D2.1.3, D4.6.1v4, D6.4.1.
**Scope:** the `string` and `symbol` value types — their model, positional access, the sequence view, operators and the system-function surface — and the design history behind them. String *patterns* (`\( … )`) and the pattern-aware `find`/`replace`/`split` are in [Lambda_Type_String_Pattern.md](Lambda_Type_String_Pattern.md).
**Consolidates:** `Lambda_Type_String.md` rev 1 (2026-01-24, the original proposal), `Lambda_Type_String2.md` (2026-04-02, native return-type proposal, never adopted) and `Lambda_Type_String_Iterable.md` (2026-02-18, string as indexable-not-iterable, since reversed). Their superseded positions are kept in Appendix S; their implementation detail and examples are not carried.
**Related:** [Lambda_Semantics_Formal.md §C1](Lambda_Semantics_Formal.md) (the `""` decision), [Lambda_Design_Syntax.md §7.27](Lambda_Design_Syntax.md) kind rulings 16–26 (text as a sequence), [Lambda_String_Func_Tuning.md](Lambda_String_Func_Tuning.md) (performance), user docs `doc/Lambda_Data.md` and `doc/Lambda_Sys_Func.md` §String Functions.

---

## 1. The text model

### 1.1 Two text kinds, one storage form

Lambda has two text scalars, `string` and `symbol`, with `binary` as their byte-level sibling. They differ in purpose, not in encoding:

| Aspect | `string` | `symbol` |
|---|---|---|
| Literal | `"text"` | `'text'` (S16.8.7) |
| Purpose | content: user text, document text, data values | names: keys, tags, enum-like values, format and option identifiers |
| Empty value | `""` is a genuine value (S2.2.1) | solid: no zero-length symbol except `symbol.empty` (S2.2.2v2) |
| Identity | its characters | a `NameKey`: resolved namespace plus normalized characters (S8.2.2v6) |
| `<`, `>` | dictionary order (S6.1.1) | not comparable: `error()` (S6.1.1) |

Both store UTF-8 and present **code points** as their unit. Length, indexing, slicing, iteration and every reported position (`find`'s `index`, `index_of`) count code points; UTF-8 byte offsets are internal only (S2.5.8, S8.3.1v3, S17.4.1). Single quotes are not an alternate string syntax: `'name' == "name"` is false, and crossing the boundary is explicit with `string(sym)` and `symbol(str)`.

*Why two kinds:* content and names have different lifecycles and different identity. A name is compared, hashed and looked up far more often than it is sliced, and it may carry a namespace; content is built, searched and transformed. Keeping them distinct lets the type say which one an API wants, and keeps map keys, element tags and option names out of the content algebra (no empty name by accident, no `<` over identifiers).

Interning, pooling and pointer comparison are implementation strategies (S1.6). Symbol identity is the `NameKey`, never an address; pointer equality is not a textual-symbol comparison (D4.6.1v4). String, symbol and binary stay tagged leaf pointers in the `Item` encoding — a measured ruling, since raw header pointers regressed the benchmark geomean by 11.1% (D2.1.3).

### 1.2 Empty and solid values

- **`""` is a string.** It has `len 0`, `"" is string` is true, `"" == null` is false (S2.2.1), and it is falsy: the falsy set is exactly `null`, `false`, `""` (S3.1v2). A string-valued function with no content to return returns `""`, not `null` (S7.10.1v3) — `trim("   ")` is `""`.
- **Symbols and binaries are solid.** Every value has `len ≥ 1`. The literals `''` and `b''` do not exist — writing them is a compile error — and a runtime operation that would make a zero-length symbol or binary yields `null` (`symbol("")` is `null`). The one exception is the predefined `symbol.empty`, the empty *name*, which a map walk yields as the key of an empty-named entry (JSON `{"": 1}`, a `.properties` line `=value`) and which prints by its name (S2.2.2v2, S8.2.2v3).
- **Content normalizes, values do not.** Building element content or a script body drops `null` and `""` and merges adjacent strings (S2.2.3, S2.6.2, S2.6.4): `<e "">` is `<e>`. This is a tree-construction rule, independent of string equality; collections never normalize, so `[1, null, "", 2]` keeps four items (S2.6.1v2).

*Why `""` is a string (C1, 2026-07-03).* The original design identified `""` with `null` (Appendix S, S-1): one absence value, one null check, solid strings inside Lambda. It failed for three reasons. It could not be uniform — data sources deliver real empty strings, so either every input boundary corrupts `{"name": ""}` into `null`, or two kinds of empty string coexist; both happened (literal `""` was null while runtime empties were strings, and JSON `{"": 1}` round-tripped as `{"''": 1}`). Strings are sequences, and `""` is a sequence's zero — the identity of concatenation and the result of an empty slice — as `0` is int's and `[]` is array's, not its null. And the optional-type machinery exists to tell *absent* from *present-empty* (HTML `alt=""` against a missing `alt`). What decided it was interop: JS, Python and every language Lambda hosts or models treat `""` as a string. The ergonomic case for the old design is kept by falsiness: `if (s)` and `s or default` still need one test.

*Why symbols stay solid.* A symbol is a name, and the empty name names nothing; banning the `''` spelling (C1.6a, 2026-07-04) makes the invariant visible, since `''` silently meaning `null` next to a real `""` was a trap. `symbol.empty` (C1.6b, 2026-09-30) was added only because the `len`/walk law (S8.3.1v3) needs a key to hand out for an empty-named map entry; yielding `null` would look like an absent key, and yielding the string `""` would make keys change kind in one case.

### 1.3 No character type

A character is a one-code-point string (or, from a symbol, a one-code-point symbol). There is no `char` type. *Why:* fewer types; `s[i]` returns the source's own kind, so it concatenates and compares with no conversion; and "character" is ambiguous in Unicode — code point or grapheme cluster — so a dedicated type would have to pick one and be wrong for the other use. Code points are converted with `ord` and `chr`.

### 1.4 Unicode levels

| Level | Role in Lambda |
|---|---|
| Code point | The unit of `len`, indexing, slicing, iteration and positions. |
| Grapheme cluster | Not a unit. A family emoji or a decomposed accent is several code points. Grapheme functions are deferred (§8). |
| Byte | Internal for text; observable only through `binary`. |

No operation normalizes implicitly. Key identity uses the characters as written, with no Unicode normalization or case folding (S8.2.2v6); case-insensitive matching folds but does not normalize (S17.7.1); `normalize(s, form)` is the explicit tool (NFC, NFD, NFKC, NFKD).

## 2. Positional access

Positional access is the same operation on text as on any sequence, at code-point granularity, and the result keeps the source's kind: indexing a symbol gives a symbol.

- **Indexing** is 0-based and a position, not a direction. A negative index carries no meaning: `s[-1]` is `null`, exactly like `s[len(s)]` (S7.2.1). Reaching from the end is the reserved word `last` (`s[last]`, `s[last - 1]`, `s[2 to last]`, S7.2.2). Every invalid read is `null`, never an error (S7.1.1v3).
- **Range subscript** `s[i to j]` is inclusive at both ends, like any range.
- **`slice(s, i, j?)`** is half-open `[i, j)` and **clamps**: out-of-range offsets clamp to the bounds and an empty selection is `""`. Clamping is symmetric — a negative offset clamps to 0, it never wraps (`slice("hello", -2, 3)` is `"hel"`). An absent source or absent offset makes the result `null`, so a missed search degrades end to end instead of returning a plausible wrong prefix; a non-integral offset is an error (S7.1.2).
- **`take(s, n)` and `drop(s, n)`** are the prefix and suffix forms and keep the kind.

*Why no negative indices (C15).* Negative indexing was kept in view in rev 1 and planned again in the 2026-02 indexable proposal (Appendix S, S-4). It was rejected: with negative wrap, both failure directions of a computed index stop being symmetric — `i - 1` running below zero silently reads the end instead of being absent. `last` gives end-relative reach with ordinary arithmetic and no special case for an empty sequence (`last` is `-1`, so `s[last]` is `null`).

## 3. Text is placed as one value and walked as a sequence

**Ruling (S2.5.8, USER 2026-09-21/22, kind ruling 22).** Text is placed as one value and walked as a sequence, exactly like an array. Iteration, `len`, indexing, `in`, the pipe `|>`, the filter `|:` and every sequence operation see a string or symbol as its code points and a binary as its bytes. The `len` law follows: `len(x)` is the number of iterations `for (i in x)` performs (S8.3.1v3).

Consequences:

- `"a" in "cat"` is **character membership**; substring search is `contains`.
- `reverse`, `sort`, `unique` and the other sequence functions work on the characters: `reverse("abc")` is `"cba"`.
- **Result kind.** A sequence operation over a text value yields that text kind when every result item is of that kind, concatenated, and an array otherwise; an empty result is `""`. So `"abc" |> upper(~)` is `"ABC"`, `"abc" |: ~ != "b"` is `"ac"`, and `"abc" |> ord(~)` is `[97, 98, 99]` (S2.5.8, S10.1.2v4, S10.1.6). The kind depends on the body's results; that was accepted as the only reading that gives `"ABC"` while keeping `ord` usable.
- **Placement treats text as one item.** For `++` and for item positions a text value is a scalar: `[1] ++ "ab"` is `[1, "ab"]` (S10.6.1), and a string placed in content is one child (which may then merge with neighbouring strings, S2.6.4).
- **`that` is single-value.** `s that p` tests the whole string, never its characters (S10.1.5v3); filtering characters is `|:`.
- **Navigation does not explode text.** To the member walk `.*`/`.**` text is a scalar, so `.**` over a document never descends into characters ([Lambda_Expr_Query.md](Lambda_Expr_Query.md) §4).
- Explicit decomposition is ordinary iteration: `[for (c in s) c]`. The `chars()` function was removed on 2026-03-09 and is not needed.

*Why the reversal.* The 2026-02 indexable proposal (Appendix S, S-5) made strings singular in iteration, pipes and predicates — `for s in "hello"` ran once, `reverse("hello")` returned it unchanged — because `that` then bound `~` to the whole item and a walking pipe would have been inconsistent with it. The 2026-09 kind survey (562 probes) found every sequence operation choosing its output kind by how its input was built, with no rule at all; the kind rulings replaced that with one law. Once `that` was ruled single-value by definition (S10.1.5v3) and filtering moved to `|:`, the consistency argument for singular strings disappeared, and the singular model's cost remained: text had a `len` and indices but no walk, breaking the `len`/`for` law that every other sequence obeys. Passthrough functions that silently returned their input unchanged were also a correctness trap.

## 4. Operators

- **`++` concatenates.** Two values of one text kind keep it (`"a" ++ "b"` is `"ab"`, `'a' ++ 'b'` is `'ab'`); otherwise both operands render as text and the result is a string (`"n: " ++ 42` is `"n: 42"`, `1 ++ 2` is `"12"`). `null` is the identity (S10.6.1).
- **No repetition operator.** `"ab" * 3` is an error value, not `"ababab"`. *Why:* `*` stays purely numeric, so `3 * x` never depends on whether `x` is text, and the overload buys little. A dedicated function was proposed and is not built (§8).
- **Ordering.** `< <= > >=` compare strings in dictionary order; symbols are names and are not ordered (S6.1.1).
- **Equality** compares characters; a string never equals a symbol.
- **`|`, `&`, `!` are type operators**, not text operators: `"GET" | "POST"` is the type admitting either string — a string enum — never a concatenation or a character set (S10.1.1v4). String enums and string patterns are distinguished in [Lambda_Type_String_Pattern.md §2](Lambda_Type_String_Pattern.md).

## 5. The system-function surface

### 5.1 Call style

String functions are global system functions taking the text as their first argument, consistent with `len`, `type` and the rest of the library. The member-call form `s.upper()` reaches the same builtins: after a receiver's own members, a member call falls through to a method-eligible builtin of that name (S12.3.3v2). Rev 1 deferred case and classification functions "until member-function syntax exists"; that deferral is void — `upper`/`lower` shipped as globals and the member form arrived through S12.3.3v2.

### 5.2 Families

| Family | Functions | Notes |
|---|---|---|
| Conversion | `string`, `symbol`, `name`, `format` | `symbol("")` is `null` (S2.2.2v2) |
| Search | `contains`, `starts_with`, `ends_with`, `index_of`, `last_index_of`, `find` | `contains`, `index_of` and `last_index_of` also work on collections; `find` takes a pattern or a string and options (pattern doc §3) |
| Transform | `upper`, `lower`, `trim`, `trim_start`, `trim_end`, `replace`, `normalize`, `slice`, `take`, `drop` | keep the source's text kind |
| Split and join | `split`, `join` | `split` returns an array of strings; `join` returns a string |
| Code points | `ord`, `chr` | `ord` is `int | null` |
| URL and hashing | `url_encode`, `url_resolve`, `hash` | string only |

### 5.3 Contract rules

- **Failures are values.** Absence in, or no answer, gives `null` — or `""` for a string result; a present but invalid input gives `error()` (S7.4.5, S7.10.1v3, S7.10.2).
- **No in-band sentinels.** `index_of`, `last_index_of` and `ord` return `int | null`, never `-1` (S7.10.3). A found test `idx >= 0` stays valid; `idx < 0` silently breaks, so absence is tested with `is null`.
- **Kind preservation.** A transform of a symbol returns a symbol (`upper('abc')` is `'ABC'`, S17.7.2). `split` and `join` produce content, so they produce strings. A transform whose result would be an empty symbol yields `null` (S2.2.2v2).
- **Case.** `lower` and `upper` use Unicode full case mapping, the same in every locale, as ECMAScript does — a mapping may change length (`upper("straße")` is `"STRASSE"`) and a final capital sigma lowercases to `ς` (S17.7.2). Case-insensitive *matching* uses simple case folding instead (S17.7.1); mapping is not folding, so `lower(a) == lower(b)` is not the case-insensitive comparison. The two rulings and their reasons are in the pattern doc §4.4.
- **Splitting** follows ECMAScript `String.prototype.split` (S17.1.1); `split(s, null)` keeps the Python-shaped whitespace form. **Replacement** follows `replaceAll` (S17.6.1). **Option maps** reject unknown names at compile time where the map is a literal (S17.8.1).
- **Character classification** (`is_numeric`, `is_alpha`, rev 1) is not a function family: a string pattern answers it (`s is \("\d"+)`).

*Why global functions first, with collections later.* Rev 1 noted that `index_of`, `contains` and `replace` read naturally on arrays too; `contains`, `index_of` and `last_index_of` were made polymorphic, which is why they kept `Item` signatures when other string functions were considered for native ones (Appendix S, S-9).

## 6. Representation notes that shape the design

These are implementation facts that the design relies on; they are informative, not rulings.

- Strings carry an ASCII flag, so length, indexing and slicing are O(1) for ASCII text; non-ASCII text walks UTF-8. A code-point position cache for non-ASCII hot paths is proposed and not built (§8).
- Single-character ASCII strings are pre-allocated, so a character walk over ASCII text allocates nothing.
- Public system functions return boxed `Item`s (D6.4.1). That is what lets a string function return an error value instead of a fake string, and it is why the native-pointer return convention of `Lambda_Type_String2.md` was not adopted (Appendix S, S-9).

## 7. Cross-language position

| Operation | Lambda | JavaScript | Python |
|---|---|---|---|
| Length unit | code point | UTF-16 unit | code point |
| `s[-1]` | `null`; use `s[last]` | `undefined` (`at(-1)` wraps) | wraps |
| Empty string | a string, falsy | a string, falsy | a string, falsy |
| Substring test | `contains(s, t)`; `in` is character membership | `includes` | `in` |
| Split | ECMAScript rules | ECMAScript | different empty-delimiter and zero-width rules |
| Case mapping | Unicode full, locale-free | same | same (`str.upper`) |
| Not found | `null` | `-1` | `-1` or exception |

## 8. Deferred, not built, and open

| Item | State | Home |
|---|---|---|
| String interpolation | open; note the `$` collision with quote splices | SO29 |
| `text` refinement type, `string that (len(~) > 0)`, for APIs that want solid strings | considered in C1.6, not decided | [Semantics_Formal §C1.6](Lambda_Semantics_Formal.md), C1–C4 follow-ups |
| Raw string literals (no escape processing) | proposed in rev 1, not built, not ruled | — |
| Grapheme-cluster functions | deferred | — |
| `pad_start` / `pad_end`, a repeat function, counting substring occurrences | kept in view, not built | — |
| `replace_first` / `replace_last` | superseded by `replace`'s `limit` and `last` options (S17.6.1) | — |
| `split_lines` | not needed: `split` with a pattern | — |
| UTF-8 position cache and other P5 string tuning | not started | [String_Func_Tuning](Lambda_String_Func_Tuning.md), impl record |
| `<` accepts symbols and uses a NUL-unsafe compare | conformance gap | S6.1.1 footnote |
| Formatter size caps drop large text from `format()` | open, needs a decision | [LR09-31](Lambda_Issue_Ledger.md#lr09-31) |
| Symbol-pooling claims (code comment, user doc) disagree with the allocator | open | [LR11-7](Lambda_Issue_Ledger.md#lr11-7) |
| utf8proc buffers cross the allocator boundary | open | [LR05-7](Lambda_Issue_Ledger.md#lr05-7) |

---

## Appendix S — Superseded positions

Each entry gives the position as it stood and what replaced it.

- **S-1.** ~~Lambda normalizes every empty string to `null`: `"" == null` is true, `'' == null` is true; there is no `is_empty()` because the null test covers it.~~ **Replaced by S2.2.1 and S3.1v2** (C1, 2026-07-03): `""` is a falsy string, distinct from `null`.
- **S-2.** ~~JSON `""` values read as `null` and do not round-trip; an empty JSON key reads as the two-character key `''`; HTML `attr=""` reads as `null` and writes back as `""`.~~ **Replaced** by the same ruling: `""` round-trips as itself, and an empty key is the empty name, walked as `symbol.empty` (S2.2.2v2, S8.2.2v3).
- **S-3.** ~~`''` is the empty-symbol literal, normalizing to `null`.~~ **Replaced by C1.6a (2026-07-04):** `''` and `b''` are compile errors.
- **S-4.** ~~Negative indexing (`s[-1]`, `s[0 to -2]`) is kept in view; `slice` accepts negative offsets counted from the end (`slice(s, 0, -1)` drops the last character).~~ **Replaced by S7.2.1 and S7.1.2:** a negative index is out of range (`null`), a negative slice offset clamps to 0, and end-relative reach is `last` (S7.2.2).
- **S-5.** ~~String is indexable but not iterable: `for s in str` runs once with the whole string, the pipe and `that` apply to the whole string, and `reverse`, `sort`, `unique`, `min`, `max` return a string unchanged.~~ **Replaced by S2.5.8** (2026-09-21/22): text walks as a sequence of code points. The proposal's other parts survive: `take`/`drop` as substring, `concat` of two strings, decomposition by indexing (now by `for`).
- **S-6.** ~~Indexing returns a single byte (`item_at` byte-indexed while `len` counted code points), and the fix wraps negative indices by adding the character count.~~ The byte defect was fixed (2026-02, and for non-ASCII symbols LR05-15, 2026-09-24); the negative wrap was never adopted (S-4).
- **S-7.** ~~Case and classification functions (`upper`, `lower`, `is_numeric`, `is_alpha`) wait for member-function syntax; case mapping should use ICU or utf8proc.~~ **Replaced:** `upper`/`lower` are global functions using Unicode full case mapping through Lambda's own `utf8_case_map` (S17.7.2); member calls reach builtins (S12.3.3v2); classification is a pattern test.
- **S-8.** ~~Symbols are pooled when 32 characters or shorter and compared by pointer.~~ **Replaced by D4.6.1v4:** identity is the `NameKey`; pointer equality is not a symbol comparison; pooling is invisible (S1.6). The stale claim is [LR11-7](Lambda_Issue_Ledger.md#lr11-7).
- **S-9.** ~~(`Lambda_Type_String2.md`, 2026-04-02, proposal.) String-returning functions get native `String*`/`Symbol*` variants (`fn_upper_str`, `fn_upper_sym`, …) dispatched by static type, registered with `LMD_TYPE_STRING_OPT`/`LMD_TYPE_SYMBOL_OPT` annotations, returning C `NULL` for both an error and an empty result.~~ **Not adopted, and since contradicted.** `NULL` cannot mean empty once `""` is a value (S2.2.1); errors must stay `Item`s, so `format` itself moved from `String*` to `Item` (D6.4.1, LR07-18, 2026-09-25); and `contains`/`index_of` became polymorphic. The registry's per-row result contract (`SYS_RESULT_TEXT_SAME_AS_ARGUMENT`) now carries the kind information the proposal wanted for type inference.
- **S-10.** ~~`trim` of an all-whitespace string returns `null`.~~ **Replaced by S7.10.1v3:** string-valued no-content is `""`.
- **S-11.** ~~`index_of`/`last_index_of` return `-1` when not found.~~ **Replaced by S7.10.3:** `null`.

## Appendix A — Implementation history

| Date | Change |
|---|---|
| 2026-01-24 | Rev 1 proposal; the core functions (`starts_with`, `ends_with`, `trim*`, `split`, `join`, `index_of`, `last_index_of`, `replace`, `take`, `drop`) implemented as globals. |
| 2026-01-27 | Empty-string handling under the original `"" ≡ null` model. |
| 2026-02-18 | Indexable-not-iterable model; UTF-8 code-point indexing in `item_at`. |
| 2026-02-27 | String performance tuning: ASCII flag, combined bounds and offset scan, interned one-character ASCII strings, pointer fast path in equality ([Lambda_Tune_String (done)](<impl/Lambda_Tune_String (done).md>)). |
| 2026-03-09 | `chars()` removed. |
| 2026-04-02 | String sys-func clean-up; the native return-convention proposal written, not built. |
| 2026-07-03/04 | C1: `""` is a string; `''` and `b''` banned. |
| 2026-09-21/23 | Kind rulings 16–26 (S2.5.6–S2.5.8, S10.6.1); text walks as a sequence; LR05-11 fixed. |
| 2026-09-24/25 | String function tuning P0–P5d: correctness fixes LR05-14, LR05-15; shared `lib/` kernels; parser and formatter loops; HTML parse peak 562 → 233 MB ([impl record](impl/Lambda_Impl_String_Func_Tuning.md)). LR07-18: string functions declare their error lane. |
| 2026-09-27 | S17.7.1 and S17.7.2: simple folding, full case mapping (`utf8_case_map`, shared with LambdaJS). |
| 2026-09-30 | `symbol.empty` (C1.6b). |

Key files: `lambda/runtime/lambda-eval.cpp` (string builtins), `lambda/runtime/lambda-data-runtime.cpp` (`item_at`), `lambda/runtime/lambda-vector.cpp` (sequence functions), `lambda/core/utf_string.cpp` (case mapping, normalization), `lib/str.c` (scan kernels), `lambda/runtime/sys_func_registry.c` (registry rows and result contracts).
