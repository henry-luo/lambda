# Proposal: Compact ES-Spec Backtracking Matcher for JS RegExp

**Date**: 2026-05-23
**Status**: proposal / design
**Author context**: follow-up to `Transpile_Js262.md` (Part II, Tier 2a). After Tiers
1–2b (named-group unicode, groups-object, `iu` case folding, lookbehind
post-filter), the test262 regex surface is at the RE2 ceiling. The remaining
failures are *backtracking-dependent* and cannot be reached by RE2 + post-filters.

## 1. Goal

Add a **minimal**, spec-faithful ECMAScript regex **backtracking matcher** that
handles the handful of corner cases RE2 cannot, so the remaining `built-ins/RegExp/*`
and `language/literals/regexp/*` test262 failures pass.

- **Correctness over speed.** This engine only runs for patterns that use features
  RE2 lacks (a tiny fraction of real patterns and ~16 tests). Performance is a
  non-goal beyond a hard anti-DoS step budget.
- **Minimal code.** Implement the spec's matcher pseudocode (ECMA-262 §22.2.2)
  almost verbatim; reuse everything already in the codebase.
- **Reuse RE2** wherever it helps (see §5). RE2 stays the default engine for the
  ~3,400 passing regex tests — this matcher is a *fallback for routed patterns only*.

## 2. Scope

### Target failures (current `temp/js262_failures.tsv`, regex subset = 17)

A real backtracking matcher fixes **~16 of 17**:

| Group | Count | Tests |
|-------|-------|-------|
| Pure backtracking semantics | 4 | `lookBehind/do-not-backtrack`, `lookBehind/greedy-loop`, `nullable-quantifier`, `lookBehind/mutual-recursive` |
| Backreferences (`\N`, `\k<name>`) | 6 | `lookBehind/back-references`, `lookBehind/back-references-to-captures`, `lookBehind/sliced-strings`, `named-groups/non-unicode-references`, `named-groups/unicode-references`, `lookBehind/misc` (`\1` self-ref) |
| Reverse-direction lookbehind captures | 3 | `lookBehind/captures` (`(\w){3}`→`"a"`), `lookBehind/alternations` (first-alternative), `named-groups/lookbehind` |
| Quantified / nested lookaround captures | 2 | `lookahead-quantifier-match-groups`, `lookBehind/nested-lookaround` |
| `\b` at lookbehind right edge | 1 | `lookBehind/word-boundary` (backward matching sees the char at `p`; the Tier-2b slice cannot) |

`lookBehind/sticky` (the 17th) is reachable *without* backtracking (forward
capture-merge) and is out of scope here — handle it in the RE2 wrapper if desired.

### Non-goals

- Replacing RE2. RE2 remains the default for all linear-time patterns.
- Performance / JIT. A bounded recursive backtracker is fine.
- Full Unicode-mode (`u`/`v`) corner cases beyond what the target tests need.
- The 2 `replaceAll` **CRASH_139** failures — those are a closure-in-loop memory
  bug, *not* regex (fix separately, ideally first; a crash is worse than a
  mis-match).

## 3. Why a backtracker (the RE2 ceiling)

RE2 is a DFA/NFA engine with **no backtracking by design** — that is exactly why
it is linear-time and DoS-safe. It structurally cannot do backreferences,
lookbehind with captures evaluated right-to-left, or backtracking-sensitive
quantifier semantics. Tiers 1–2b approximated some of this with "match wider +
post-filter"; the remaining cases need real backtracking with capture state.

## 4. Architecture — hybrid dispatch

```
                       js_create_regex (js_runtime.cpp)
                                   │
                 existing preprocessing pipeline (reused)
       (\u→\x{}, \s/\S expand, \p{} class expand, named-group, flags)
                                   │
                     ┌─────────────┴──────────────┐
            feature detection (extend js_regex_needs_backtrack)
                     │                            │
        RE2-able pattern                 needs backtracking
        (default, unchanged)        (backref / lookbehind-with-capture /
                     │                backtracking-sensitive / nested LA)
              re2::RE2 + wrapper                  │
                     │                  NEW: JsBtRegex (this proposal)
                     └─────────────┬──────────────┘
                          JsRegexData { re2, wrapper, bt }
                                   │
                 js_regex_exec / test / match / replace / split
                 (one extra branch: if rd->bt, call js_bt_exec)
```

The matcher is a **third backend** behind the existing `JsRegexData` struct,
selected at compile time. Match-time dispatch adds exactly one branch in the
same place the wrapper is dispatched today
(`js_runtime.cpp` ~`12774`, `js_regex_wrapper_exec`).

## 5. RE2 reuse (per the "reuse RE2 if possible" requirement)

RE2's own parser (`re2::Regexp::Parse`) **cannot** parse the features we need — it
rejects `\1`, `(?<=…)`, `(?=…)` outright — so we cannot reuse RE2's parser/AST for
routed patterns. Reuse therefore happens at these seams:

1. **RE2 stays the default engine** — the single biggest reuse. The backtracker is
   *only* compiled when feature detection trips. Zero impact on the 3,400 passing
   regex tests.
2. **Reuse the existing preprocessing** in `js_runtime.cpp` (`\uHHHH`/`\u{}` →
   `\x{}`, `\s`/`\S` expansion, `\p{}` property-class expansion via
   `js_regex_generated_property_tables.inc`, named-group bookkeeping, flag parsing).
   The backtracker consumes the *already-normalized* `processed_pattern`, so its
   parser handles a smaller surface (no `\p{}`, no `\u`).
3. **Reuse the Unicode + casefold tables** already in `js_runtime.cpp`
   (`js_regex_generated_ranges_*`, `casefold_seqindex`, the `iu` fold groups from
   Tier 1c) for character-class and `Canonicalize` evaluation inside the matcher.
4. **Optional speed seam (not required):** a backtracking node whose subtree is
   RE2-compatible *could* delegate to a cached `re2::RE2` for that span. Given
   "performance not important," skip this initially; note it as a future hook.
5. **Reuse integration plumbing:** `JsRegexCompiled`-style lifetime, the
   `match_starts[]/match_ends[]` output contract of `js_regex_wrapper_exec`, and
   the cache (`g_regex_compile_cache`).

## 6. The matcher — implement §22.2.2 almost verbatim

The ECMA-262 "Pattern Semantics" defines matching as **Matcher** closures taking a
**State** `(endIndex, captures[])` and a **Continuation**, with a **direction**
(+1 / −1). Implementing this directly is both minimal and exactly correct,
including the two hardest behaviors we need:

- **Lookbehind = matching with `direction = −1`.** Captures inside a lookbehind
  fall out naturally with correct right-to-left iteration (fixes `captures` `(\w){3}`
  → `"a"`, `alternations` first-alternative, `word-boundary` `\b`). This is the key
  reason a spec-faithful matcher beats any RE2 post-filter.
- **Backtracking quantifiers + backreferences** are the spec's `RepeatMatcher` and
  `BackreferenceMatcher` — a few dozen lines each.

### 6.1 Components

```
js_bt_regex.h / js_bt_regex.cpp   (new, self-contained)

  Parser:  recursive-descent over the normalized pattern → AST
           nodes: Char, CharClass, Any, Anchor(^ $ \b \B \A \z),
                  Group(capturing/non-capturing/named, index),
                  Alt, Concat, Quant(min,max,greedy),
                  Backref(index|name), Look(ahead/behind, negative)
  Compile: AST + flags (i, m, s, u, sticky) → JsBtRegex
  Match:   spec Matcher/Continuation, recursive, with `direction`,
           a `captures[]` array, and a global step counter.
  API:     js_bt_compile(pattern, len, flags) -> JsBtRegex*
           js_bt_exec(JsBtRegex*, input, len, start, anchored,
                      starts[], ends[], ngroups) -> int   // mirrors wrapper_exec
           js_bt_free(JsBtRegex*)
```

### 6.2 Matching model (minimal & correct)

Use the spec's continuation style, implemented as recursive C++ with a small
`MatchState { int pos; int* caps; }` and lambdas/function-pointers for
continuations — or, equivalently and even smaller, a **node-list + index +
recursion** backtracker:

```cpp
// returns new pos on success (matching `node` then the rest via `cont`), or -1
int m(const Node* node, int pos, Caps& caps, const Cont& cont, int dir);
```

`dir` is +1 normally, −1 inside a lookbehind body. `^`/`$`/`\b` consult
`input[pos-1]`/`input[pos]` with full context (no slicing → `\b` correct).
Quantifiers recurse with greedy/lazy ordering; backreferences compare the captured
span (honoring `Canonicalize` under `i`). Lookaround saves/forks state, runs the
sub-matcher, restores `pos`, keeps/zeroes captures per spec.

### 6.3 Anti-DoS (the one non-negotiable)

A single global **step budget** (e.g. 1e6 matcher invocations) per `exec` call;
on exhaustion, bail to "no match" and `log_debug`. This bounds catastrophic
backtracking without affecting correctness on the (tiny, well-behaved) routed set.

## 7. Feature detection / routing

Add `js_regex_needs_backtrack(pattern, len)` (sibling of
`js_regex_needs_wrapper`). Route to the backtracker when the pattern contains any
of:

- a backreference `\N` or `\k<name>` (outside a class),
- a lookbehind `(?<=` / `(?<!` **that contains a capture group, a backreference,
  a nested assertion, or a variable-length body** (pure fixed assertions stay on
  the Tier-2b fast path),
- a quantified lookahead whose captures are observed.

Per **Decision 1 (minimal routing)** and **Decision 3 (Tier-2b kept)**: pure
fixed-length lookbehind assertions stay on the Tier-2b RE2 fast path; everything
RE2 can already do stays on RE2. Only the feature set above (the cases the target
tests exercise) is routed. `sticky` lookbehind (Decision 4) is *not* routed.

Patterns not tripping this stay on RE2 exactly as today. Start **narrow** and widen
only if a specific target test demands it, validating 0 regressions at each step.

## 8. Integration points (concrete)

| Seam | File:symbol | Change |
|------|-------------|--------|
| Compile dispatch | `js_runtime.cpp` `js_create_regex` (~`14457`) | after the RE2/wrapper attempt, if `js_regex_needs_backtrack`, build `rd->bt = js_bt_compile(...)` |
| Struct | `JsRegexData` (`js_runtime.cpp` ~`11745`) | add `JsBtRegex* bt;` |
| Match dispatch | `js_runtime.cpp` ~`12774` (next to `rd->wrapper`) | `if (rd->bt) return js_bt_exec(...)` → fill `re2::StringPiece matches[]` like the wrapper branch |
| Group count | `js_runtime.cpp` ~`12737` | `rd->bt ? bt->group_count+1` |
| needs-backtrack | `js_runtime.cpp` (new, near `js_regex_needs_wrapper` ~`13063`) | feature scan |
| Cache | `g_regex_compile_cache` | store/free `bt` alongside `re2`/`wrapper` |
| Free | wherever `JsRegexData`/cache entries are freed | `js_bt_free(rd->bt)` |
| Build | `build_lambda_config.json` | add `lambda/js/js_bt_regex.cpp` |

The output contract is identical to `js_regex_wrapper_exec` (`match_starts[]`,
`match_ends[]`, group 0 = whole match, −1 = non-participating), so all callers
(`exec`, `test`, `@@match`, `@@replace`, `@@split`, `matchAll`) work unchanged.

## 9. Conventions

Per **Decision 2**: the backtracker uses **`lib/` types** (`StrBuf`, `ArrayList`,
`HashMap`, arena/pool allocation) for the parser scratch, compiled AST, and capture
state, per `CLAUDE.md`. The existing regex subsystem (`js_regex_wrapper.cpp`) uses
`std::string` for RE2 interop, but the backtracker **never touches RE2**, so it can
and should stay `std::`-free. `std::string` remains confined to the RE2 boundary in
the existing wrapper/compile code, unchanged by this work.

## 10. Incremental plan (test-driven, validate 0 regressions each step)

1. **Skeleton + dispatch**, routing only `(?<=fixed)` already handled by Tier 2b,
   producing identical results → proves plumbing with no behavior change.
2. **Core matcher**: literals, classes, `.`, anchors, groups, alternation,
   greedy/lazy quantifiers, `{n,m}`. Validate against a scratch corpus.
3. **Lookbehind with `direction=−1`** + captures → `lookBehind/captures`,
   `alternations`, `word-boundary`, `start-of-line` edge, `named-groups/lookbehind`.
4. **Backreferences** `\N` / `\k<name>` (with `i` Canonicalize) →
   `back-references*`, `named-groups/*-references`, `sliced-strings`, `misc`.
5. **Backtracking-sensitive quantifiers / nested lookaround** →
   `do-not-backtrack`, `greedy-loop`, `nullable-quantifier`, `mutual-recursive`,
   `nested-lookaround`, `lookahead-quantifier-match-groups`.
6. **Widen routing**, full `--batch-only` ×2, then `--update-baseline`.

Each step: run the lookbehind suite locally + a focused RegExp/String batch, then
a full release batch (the workflow already used this session).

## 11. Risks

- **Regressions on the shared path.** Mitigation: narrow routing; everything not
  routed is byte-for-byte unchanged RE2. Full-batch ×2 per step.
- **Catastrophic backtracking.** Mitigation: hard step budget (§6.3).
- **Semantic drift from spec.** Mitigation: implement §22.2.2 operations directly;
  keep the matcher tiny and auditable.
- **Capture numbering** across lookbehind/main must follow source `(` order — the
  parser assigns indices in parse order, so this is free (unlike the Tier-2b
  remap gymnastics).

## 12. Estimated size

~1,000–1,500 LOC in one new `js_bt_regex.{h,cpp}` (parser ~400, matcher ~450,
class/Unicode/casefold glue reusing existing tables ~200, integration ~150).
No new third-party dependency.

## 13. Decisions (settled)

1. **Routing breadth**: route **only the minimal feature set** the target tests
   need. Everything RE2 can handle (incl. fixed-length lookbehind, §3) stays on
   RE2/wrapper. Widen only if a specific target test demands it. (§7)
2. **Container convention**: use **`lib/` types** (`StrBuf`, `ArrayList`, …) for
   the parser, AST, and capture state per `CLAUDE.md`. `std::string` is allowed
   *only* at the RE2 boundary — and the backtracker never touches RE2, so it stays
   `std::`-free. (§9)
3. **Tier-2b lookbehind**: **keep** the RE2 post-filter as the fast path for pure
   fixed-length assertions; route **only the hard lookbehinds** (capture / backref
   / nested / variable-length) to the backtracker. Tier-2b is not retired. (§7)
4. **`sticky` lookbehind** (forward capture-merge, not backtracking): **out of
   scope** — left to the RE2 wrapper, not routed to the backtracker.
5. **`replaceAll` CRASH_139**: **out of scope** — a separate closure-in-loop
   memory bug, fixed independently of this work.

## 14. Test plan

- Local: the 17 `built-ins/RegExp/lookBehind/*` + `named-groups/*references` +
  `nullable-quantifier` + `lookahead-quantifier-match-groups`, run with
  `sta.js assert.js compareArray.js`.
- Regression: focused RegExp+String batch (≈3,395 tests) must stay 100%.
- Full: `--batch-only` ×2 (0 regressions), then `--update-baseline`.
- Anti-DoS: a `(a+)+$` × long-non-matching-input case must terminate via the step
  budget, not hang.
