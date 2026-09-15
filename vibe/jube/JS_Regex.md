# JavaScript RegExp: Structural Routing Without an AST

**Date:** 2026-09-15

**Status:** implemented on 2026-09-15. The production scanner now applies the
streaming-summary rules below; the AST POC remains a test-only comparison
implementation. Focused route, Node-equivalence, backtracker, and explicit
resource-failure gates pass (§10, Appendix A.4).

**Scope:** choose between direct RE2 and `js_bt_regex` while preserving
JavaScript match selection and capture participation, retaining common RE2
cases, and using a single scan with a group stack.

**Spec linkage:** [Formal Design](../../doc/Lambda_Formal_Design.md) **D1.3**
preserves guest-language semantics across shared
runtime facilities; **D1.4v3** and **D8.4.3v2** govern explicit failure
propagation. No existing `S#` or `D#` specifies this routing algorithm, and
this document changes no formal ruling. Decisions 1.1–1.5 extend the original
RegExp matcher-routing decision without introducing another decision series.

**Related issue:** **OI-4**, [Lambda Issue Ledger](../Lambda_Issue_Ledger.md).

## 1. Outcome and design decisions

Keep one summary per open group, reduce completed groups into their parents,
and discard their frames. The router constructs no nodes, retains no child
lists, and needs no second tree walk. The backtracker may still build its
existing execution AST after it has been selected; this design removes the
need for a separate *routing* AST.

| Decision | Design |
|---|---|
| **1.1 — Streaming summaries** | Track whether an expression can match empty, contains captures, or can skip one of those captures. Parse complete tokens and quantifier bounds. |
| **1.2 — Two repetition hazards** | Require the backtracker for an optional iteration of a nullable body, or multiple iterations of a body that can skip a capture. Preserve independent assertion/backreference gates. |
| **1.3 — Required means required** | An uncertain analysis or a required backtracker compilation failure must never silently become direct RE2 execution. Distinguish actual no-match from execution failure. |
| **1.4 — One decision for every consumer** | Store the route with the compiled pattern; `exec`, `test`, replacement, splitting, iteration, and indices consume the same engine semantics. |
| **1.5 — Evidence before admission** | Validate result equivalence independently of route agreement. The old POC is a comparison implementation, not a correctness oracle. |

This is a selective design for the identified repetition differences. It is
not a claim that three facts establish complete RE2/ECMAScript equivalence.
Character semantics, flags, normalization, syntax admission, and the selected
matcher's correctness remain separate obligations under **D1.3**.

## 2. Hybrid execution architecture

LambdaJS uses RE2 for the large regular subset because it runs in linear time.
RE2 cannot implement ECMAScript backreferences, captured lookbehind evaluated
right-to-left, or capture state that changes during backtracking. The runtime
therefore selects one compiled engine for each RegExp and preserves the engine
choice in the compiled object and cache.

```text
JS pattern + flags
        |
frontend validation and shared normalization
        |
route analysis
  |             |                 |
literal/property  direct RE2 /       js_bt_regex
fast path          RE2 wrapper        (spec matcher)
  \______________     |     __________/
                 \    |    /
                  shared exec/test/replace/split consumers
```

`js_create_regex` owns that selection. `JsRegexData` carries the selected
native engine and flag state. `js_regex_match_internal` then supplies one
capture-span contract to all public RegExp and String methods: group zero is
the full match; a negative span is a non-participating group. `exec` turns
those spans into result entries, named groups, and `/d` indices; `test`,
`match`, `matchAll`, `search`, replacement, and splitting consume the same
match state.

### 2.1 What each engine owns

| Engine | Intended patterns | Semantics responsibility |
|---|---|---|
| Literal/property fast paths | Independently proven literal and simple property-repeat forms | Must prove the same match selection and capture contract before bypassing a regex engine. |
| Direct RE2 | An analyzed ECMAScript subset | Supplies efficient ordinary matching only when the router has established that its observable result agrees. |
| RE2 wrapper | A compatible pattern needing a supported rewrite or post-filter | Rewrites a wider RE2 pattern and verifies/trims it through the wrapper's capture remapping and filters. |
| `js_bt_regex` | Backreferences, assertions and repetition shapes that need ECMAScript capture state | Supplies matcher ordering, per-iteration capture clearing, lookaround state, and non-participation directly. |

The wrapper stays a separate compatibility mechanism. It cannot recover an
ECMAScript capture decision once RE2 has discarded the required iteration
state. The structural router selects `js_bt_regex` for that class of pattern.

### 2.2 Backtracker design

`js_bt_regex` is the compact ECMAScript matcher. It parses the pattern it is
given into an execution AST; this is distinct from the routing scanner, which
constructs no AST. Its compiled forms cover characters, character classes,
any-character, anchors, captures and noncapturing groups, alternatives,
concatenation, greedy/lazy bounded quantifiers, numeric/named backreferences,
and positive/negative lookahead and lookbehind.

Matching follows the ECMAScript Matcher/Continuation model:

```text
MatchState = (input position, capture-start[], capture-end[])
Matcher    = node + continuation + direction
direction  = +1 normally, -1 while evaluating lookbehind
```

The continuation permits greedy/lazy retry in source order. Captures are saved
and restored on each retry. A quantifier clears its body's capture range before
an iteration, and rejects a zero-width optional repetition once its minimum
has been satisfied, as required by ECMAScript `RepeatMatcher`. Backreferences
compare the saved span, including case canonicalization when applicable.
Lookaround forks/restores the input position and capture state according to
its polarity. Matching lookbehind with `direction = -1` avoids an incorrect
substring-slice approximation for captures and word boundaries.

The public native seam remains:

```text
js_bt_compile(pattern, length, flags, pool) -> JsBtRegex*
js_bt_exec(bt, input, length, start, anchored, starts, ends, group_count)
  -> JsBtExecResult { MATCH, NO_MATCH, RESOURCE_EXHAUSTED, ALLOCATION_FAILURE }
```

`js_regex_match_internal` maps the latter two outcomes to an explicit runtime
error. Its public callers (`exec`, `test`, `search`, global `match`, and
replacement) propagate that error before no-match state updates.

### 2.3 Reuse, ownership, and integration

RE2 remains the default for eligible patterns. The backtracker reuses the
first-party RegExp frontend for flags, source identity, named-group admission,
class/property normalization where applicable, Unicode/case-fold data,
compiled-object lifetime, and public result construction. It does not reuse
RE2's parser because RE2 rejects constructs such as backreferences and
lookaround that the fallback must understand.

The backtracker and streaming scanner use existing `lib/` containers and
allocation helpers. The backtracker's parser/compiled state lives in the
RegExp's pool lifetime. Native interop with `re2::RE2` stays at the existing
RE2 boundary; the matcher itself does not introduce `std::` containers.

No subtree-level RE2 delegation is part of this design. It can be considered
only after a separate proof that the delegated subtree preserves continuation
and capture-state behavior.

### 2.4 Mandatory routing reasons

The streaming repetition reasons defined below combine with the established
reasons below. A reason is monotonic: once present, a later parent reduction
cannot return the pattern to direct RE2.

| Source feature | Required engine action |
|---|---|
| Numeric/named backreference outside a class | `js_bt_regex` |
| Lookahead or lookbehind | Preserve the current scanner route to `js_bt_regex`. |
| Multiline anchor | Preserve the current scanner route to `js_bt_regex`. |
| Optional empty iteration | `js_bt_regex` (§6.2). |
| Repeated body that can skip a capture | `js_bt_regex` (§6.2). |
| Unsupported structural analysis | Do not certify direct RE2 (§9.1). |

## 3. The two semantic differences

ECMAScript's [RepeatMatcher](https://tc39.es/ecma262/multipage/text-processing.html#sec-repeatmatcher)
rejects an additional empty iteration once the required count is satisfied,
and clears the quantified body's captures before each attempted iteration.
Those requirements motivate the two routing conditions below.

### 3.1 Empty optional iterations

“Optional iteration” means an iteration beyond the quantifier's minimum,
including the sole iteration of `?`. It does not require a `*` or multiple
iterations, and it does not require a capturing group.

```js
/(a?)?/.exec('')       // ["", undefined]
/(a{0,3})*/.exec('')   // ["", undefined]
/(?:|a)*/.exec('a')    // ["a"]
```

The last example changes the whole match. Treating every difference as an
empty-capture formatting problem misses it.

### 3.2 Captures skipped in later iterations

A capture that participated earlier must not survive a later iteration
that omits it:

```js
/(?:(a)|b)+/.exec('ab')  // ["ab", undefined]
```

The body always consumes input, so nullability alone cannot detect this
hazard. Alternation and optional captured subexpressions can both cause it.

### 3.3 Empty strings are valid captures too

```js
/(a?){1}/.exec('')     // ["", ""]
/((a?)b)*/.exec('b')   // ["b", "b", ""]
```

The `(a?)` group in the second example always participates when its parent
iteration succeeds. Its *contents* may be empty. Replacing every empty
capture with `undefined` would corrupt both results.

## 4. Scanner input and lexical contract

The scan belongs at the existing early routing boundary in `js_create_regex`:
after validated flag/name admission and the shared Unicode-set class rewrite,
but before RE2-specific preprocessing can remove backreferences or alter
group structure. The matcher still receives its existing normalized input.
The route must describe the same semantics that those later rewrites compile.

The scanner needs the relevant flags, including Unicode/Unicode-sets mode;
the current `multiline`-only interface does not describe every lexical choice.
Reuse existing frontend facts where available. Keep these responsibilities
explicit:

- Consume escaped atoms as complete tokens. Braces inside `\u{...}` or
  `\p{...}` are not repetition bounds. An escaped `?`, `(`, or `|` is not
  structure.
- Consume a normalized character class as one atom, respecting its escapes.
  `\b` inside a class is a character; outside a class it is an assertion.
- Recognize plain captures, `(?:...)`, named captures, and assertion prefixes.
  The `?` in a group prefix is not a quantifier.
- Consume the lazy suffix of a quantifier together with that quantifier.
  The `?` in `a+?` does not give the atom a zero minimum.
- Preserve the existing routes for lookahead/lookbehind, backreferences, and
  multiline anchors. Classify decimal escapes using capture/mode information
  from admission where possible; legacy identity/octal escapes are not all
  backreferences. Unresolved escape semantics are analysis-unsupported.
- Understand any group modifiers admitted by the frontend, including scoped
  flags, or report analysis-unsupported. Do not guess their effect.
- `/v` string sets may include strings rather than individual characters.
  Analyze their normalized group/alternative form. If a rewrite leaves an
  opaque potentially empty string set, do not assume it consumes a character.

The scanner is a structural analyzer, not a substitute syntax validator.
Malformed syntax must still reach the appropriate frontend diagnostic even
when a routing reason is found early. Unsupported valid syntax and invalid
JavaScript are different outcomes.

## 5. Summary facts

Each atom or completed expression has the following summary:

| Fact | Meaning |
|---|---|
| `nullable` (`N`) | May succeed without advancing the input position. |
| `has_capture` (`H`) | Contains a syntactic capture, including the expression's own capture when applicable. |
| `may_skip_capture` (`K`) | A successful path may leave at least one contained capture unvisited. |
| `reasons` (`R`) | Accumulated routing reasons from this expression and its children. |

`N`, `H`, and `K` are the three semantic facts. `R` is diagnostic/dispatch
bookkeeping, not another match analysis. A true `N` or `K` may conservatively
include an infeasible path; a false value must exclude the hazard throughout
the syntax the scanner understands. No input-dependent reachability analysis
or per-capture bitset is needed.

### 5.1 Atoms and empty sequences

| Expression | N | H | K |
|---|---|---|---|
| Literal, `.`, consuming escape, ordinary normalized character class | false | false | false |
| Empty sequence, including an empty alternative | true | false | false |
| Supported zero-width assertion | true | false | false |

An impossible character class has no successful empty match, so classifying
it as non-nullable is safe. Context-sensitive assertions may be approximated
as nullable; their existing independent engine/flag gates still apply.
Lookarounds and backreferences need not be analyzed internally for RE2
eligibility once their mandatory routing reason is recorded.

### 5.2 Concatenation

For `AB`:

```text
N = A.N && B.N
H = A.H || B.H
K = A.K || B.K
R = A.R | B.R
```

Both pieces execute on a successful concatenation. An optional piece has
already recorded its possible capture omission in its own `K`.

### 5.3 Alternation

For two actual branches `A|B`:

```text
N = A.N || B.N
H = A.H || B.H
K = A.K || B.K || A.H || B.H
R = A.R | B.R
```

A capture syntactically inside one branch is unvisited when the other branch
wins. Capture indices belong to source occurrences; matching text that looks
the same in both branches does not make their captures the same occurrence.
Apply this merge only when a real second branch exists, not when initializing
an empty accumulator.

The enclosing capture of `(a|b)` is added *after* merging the branches. It is
always visited when that group succeeds and must not become conditional just
because its body contains `|`. Thus `(a|b)+` can remain on RE2, whereas
`(?:(a)|b)+` requires the backtracker.

### 5.4 Group completion

A noncapturing group inherits its body's facts. A capturing group also sets
`H = true`, leaving `N` and `K` unchanged. Its own capture is visited whenever
the group executes successfully, even if it captures an empty string.

The outer quantifier is processed only after this step, so `(a)?` correctly
acquires a skippable capture, while `(a?)` does not.

## 6. Quantifiers and routing

### 6.1 Read actual bounds

| Token | Minimum | Maximum |
|---|---|---|
| `?` | 0 | 1 |
| `*` | 0 | unbounded |
| `+` | 1 | unbounded |
| `{n}` | n | n |
| `{n,}` | n | unbounded |
| `{n,m}` | n | m |

Store unbounded explicitly; a `-1` representation must be handled before
ordinary comparisons. Parse decimal bounds with overflow checks, preserving
whether `max > min`. Saturating both bounds to a small category would wrongly
collapse `{2,3}` into an exact count. An unrepresentable bound must not wrap;
report unsupported/resource status and retain the original source for the
authoritative compiler to diagnose.

Malformed brace text is treated according to the admitted JavaScript mode,
including legacy literal-brace forms. Share token helpers with existing
first-party parsing where practical; do not create another unchecked copy of
the current backtracker's brace parser.

### 6.2 Examine the unquantified body

For body `B` and quantifier `q`:

```text
has_optional_iteration = q.unbounded || q.max > q.min
can_repeat             = q.unbounded || q.max > 1

empty_iteration_hazard = has_optional_iteration && B.N
capture_reset_hazard   = can_repeat && B.K

R = B.R
    | (empty_iteration_hazard ? EMPTY_OPTIONAL_ITERATION : 0)
    | (capture_reset_hazard   ? CAPTURE_RESET            : 0)
```

Use the body's facts **before** making the quantified expression nullable.
Otherwise `a*` would be misclassified merely because zero repetitions are
allowed. This rule applies to every quantifiable atom, not just capturing
groups, and includes `?` and `{0,1}`.

Greediness is parsed but does not weaken either hazard. Surrounding text may
force a lazy quantifier to expand. There is no “body contains an unbounded
quantifier, so keep RE2” exception: `(a*)*` reproduces a capture difference.

### 6.3 Publish the quantified summary

For `q.max != 0`:

```text
N = q.min == 0 || B.N
H = B.H
K = B.K || (q.min == 0 && B.H)
```

For `{0}`, the result is `N = true`, `H = B.H`, `K = B.H`: capture numbering
survives even though none of those captures executes. For simplicity, retain
child routing reasons even under `{0}`; this may route dead syntax more
broadly, but does not invent a result. Pruning unreachable syntax is outside
the initial design.

## 7. The group stack

Each frame retains a current sequence summary, a completed-alternatives
summary, a bit saying whether an alternative has been completed, and the
group's capture/modifier information. A synthetic noncapturing root frame
handles the top-level expression. No frame retains its closed children.

| Input event | Action |
|---|---|
| Consuming or zero-width atom | Form its summary, read an applicable trailing quantifier, then merge into the current sequence. |
| `(` and group prefix | Push a frame with the empty-sequence identity. |
| `|` | Fold the current sequence into the alternatives accumulator, then reset the sequence. |
| `)` | Fold the last branch, add the group's own capture fact, pop, read its trailing quantifier, and merge into the parent sequence. |
| End of source | Finalize the root; unclosed groups or leftover tokens are not RE2 eligibility. |

A trailing quantifier is consumed immediately when an atom or group finishes;
it cannot accidentally apply to an already merged sequence. Escape and class
token readers advance the same cursor, without restarting scans over bodies.

For pattern length `n` and maximum nesting `d`, this gives **O(n) scan time**
and **O(d) frame storage**. Use a small inline frame capacity with checked
growth through existing `lib/` allocation facilities. The current
pattern-length-sized scratch allocation can be replaced by depth-based growth;
the O(d) claim depends on doing so. No silent depth cap, C++ recursion over
untrusted nesting, or allocation failure interpreted as successful analysis.

## 8. Expected routing and deliberate approximations

These decisions assume no independent backreference, assertion, flag, or
unsupported-syntax reason. “RE2 candidate” means the repetition analysis adds
no reason to leave RE2; the other admission checks still apply.

| Pattern | Proposed route | Reason |
|---|---|---|
| `a*`, `[ab]+` | RE2 candidate | The repeated atom consumes input. |
| `(ab)*` | RE2 candidate | Consuming body; its capture is always visited. |
| `(a\|b)+` | RE2 candidate | Alternatives are inside one unconditional capture. |
| `((a)b)+` | RE2 candidate | Both captures are visited each iteration. |
| `((a?)b)*` | RE2 candidate | `b` forces consumption; `(a?)` participates even when empty. |
| `(a?){1}`, `(a?){2}` | RE2 candidate | All outer iterations are mandatory; the body has no skippable capture. |
| `(?:a{0,3}b)*` | RE2 candidate | The body cannot complete without consuming `b`. |
| `(a?)?`, `(a?){0,1}` | Backtracker | Optional empty iteration. |
| `(a{0,3})*`, `(a*)*` | Backtracker | Optional empty iteration, regardless of inner bounds. |
| `(?:\|a)*` | Backtracker | Optional empty alternative affects match selection. |
| `(?:(a)\|b)+` | Backtracker | An alternative can omit `(a)` in the final iteration. |
| `((a)?b)*` | Backtracker | The inner capture can be skipped. |
| `(?:(a)\|b){2}` | Backtracker | Exact counts still require per-iteration capture reset. |

The design deliberately does not prove whether an alternative is reachable,
whether surrounding text forces an optional capture to participate, or
whether a nullable body only ever matches empty with no observable captures.
Those cases can be routed unnecessarily. Common consuming groups and captures
that always participate remain on RE2, avoiding the much broader “every
repeated capture” policy without adding a full equivalence solver.

## 9. Compilation, execution, and failures

### 9.1 Preserve the decision

The analysis result must distinguish a fully analyzed RE2 candidate, a
required backtracker route with reason bits, and unsupported/incomplete
analysis. A failed scan is never a false boolean meaning “RE2 is safe.”

For a required or unsupported route, use a matcher that can handle the
admitted syntax or return an explicit failure. If `js_bt_compile` cannot
compile a required pattern, falling through to an approximate RE2 pattern
violates the route. Invalid JavaScript keeps its syntax diagnostic; a valid
pattern beyond an engine's support is not automatically a JavaScript syntax
error. The specific support/resource error class remains an implementation
policy to settle before shipping.

Keep original source, canonical flags, capture/name mapping, route, and any
diagnostic reason together in compiled state and caches. A cache hit must
not bypass analysis with an entry admitted under a different policy. Existing
literal/property/class fast paths must prove their eligibility independently;
the number of requested captures is not an eligibility test.

### 9.2 Preserve match outcomes

The internal matching API needs distinct outcomes for `MATCH`, `NO_MATCH`,
and failure such as budget/depth/allocation exhaustion. Error propagation at
the JS/host boundary follows **D1.4v3** and **D8.4.3v2** using explicit returned
completions; no pending exception flag or non-local unwind is introduced.

The existing backtracker returns `0` both for no-match and for exhausted
resources. Broadening routing without separating those outcomes can replace
one incorrect result with another. An explicit resource error prevents a
fabricated no-match; it does not promise Node's answer for every input under
a finite budget. Total successful matching would also require addressing
the engine's resource limits.

Every consumer must observe the same outcome and capture participation:
`exec`, `test` and its legacy static updates, `match`, `matchAll`, `search`,
`split`, `replace`/`replaceAll` including callbacks, named `groups`, and `/d`
indices. Preserve each API's own `lastIndex` and empty-match advancement
rules. Do not publish partial captures or perform the ordinary no-match
state updates when the engine failed.

## 10. Validation and acceptance

### 10.1 Separate routing from semantics

Run three routing implementations over the same patterns: the recorded
production scanner, the earlier AST POC, and the proposed scanner. Report
reason counts and route differences. Agreement with the POC is not a gate:
its current feature rule is different and misses some of the examples above.

For semantic checks, compare complete results against Node and adjudicate
disagreements using the ECMAScript specification. For supported patterns,
also compare forced RE2 and forced backtracker execution in a diagnostic
harness, so a runtime cleanup cannot conceal an engine difference. Keep
forced-engine controls out of normal user execution.

Capture participation must have its own marker. Plain `JSON.stringify` of
an array changes `undefined` entries to `null` and cannot distinguish those
values. Compare exact structured records; hashes may summarize a run but
must not be the sole acceptance evidence. Save minimized failing pattern,
flags, subject, route/reasons, and both results under `./temp/`.

### 10.2 Corpus dimensions

- Plain/named/noncapturing groups; nested alternatives; empty branches in
  first, middle, and last position; escaped punctuation and class contents.
- `?`, `*`, `+`, `{0}`, `{0,1}`, `{1}`, `{1,1}`, `{2}`, `{1,2}`, `{2,4}`,
  `{2,}` and lazy forms; malformed/overflowing bounds and legacy brace text.
- Captures outside alternatives, inside alternatives, always visited with
  empty contents, skipped through optional groups, and reset through fixed
  as well as variable repetition counts.
- Nullable bodies with bounded and unbounded children, including expressions
  without captures and patterns followed by consuming text or anchors.
- All admitted flags, Unicode and legacy escapes, rewritten `/v` string
  alternatives, astral characters, lone surrogates, and JS line terminators.
- Empty/nonempty matches, failures, match index and text, every capture and
  name, `/d` ranges, `lastIndex`, replacement callbacks, and split output.
- Deep nesting, large capture counts, allocation failure, compilation
  failure, and matcher budget/depth exhaustion; distinguish errors from
  successful no-match results.
- Core matcher coverage: literal/class/anchor/group/alternative/quantifier
  execution, lookbehind captures and alternation order, numeric/named
  backreferences, quantified/nested lookarounds, `\b` at a lookbehind edge,
  and nullable quantifiers. Retain the focused `test_js_bt_regex_gtest` gate
  alongside integrated RegExp/Test262 coverage.

### 10.3 Exit conditions

1. Retain the existing differential corpus and reproduce its known failures
   before changing routing. Add the independently confirmed cases in Appendix A.
2. Assert exact Node results for regression fixtures and generated subjects
   after implementation. Historical tests that deliberately assert a
   divergence exists are not passing conformance tests.
3. Keep the RE2-candidate examples in §8 on RE2 unless another documented
   feature requires otherwise; evaluate real pattern corpora for route share.
4. Run focused RegExp/String regressions and relevant Test262 coverage,
   preserving the test harness and investigating runtime failures. Run the
   repository-required Lambda baseline when runtime changes land.
5. Exercise error propagation through every match consumer before expanding
   production routing. Required backtracker compilation failures must not
   downgrade, and exhaustion must not appear as no-match.
6. Measure scan/compile cost, route share, and representative matching cost
   with a release build. A small scanner does not make newly routed
   backtracking matches linear-time.

## Appendix A — Evidence and implementation inventory

### A.1 Earlier POC experiment

The recorded 2026-09-15 report in `temp/js_regex_router_poc.xml` contains:

| Measurement | Recorded result |
|---|---:|
| Parser-accepted pattern/flag cases | 4,260 |
| Same scanner/POC route | 3,252 |
| Scanner-only backtracker routes | 50 |
| POC-only backtracker routes | 958 |
| Representative POC-only patterns evaluated | 479 |
| Subject entries per pattern | 765 |
| `exec` calls per engine | 366,435 |
| Pattern instances with differing result digests | 31 |

The 31 instances represent 30 distinct pattern texts; `(a{0,3})*` occurs
twice. They are not 31 failing executions and do not characterize all
production routing failures. The matching 448 pattern digests establish only
agreement on that corpus. The paired multiline cases were omitted from the
semantic run because those POC-only patterns contained no unescaped anchors.

The POC computes `may_be_empty` but its routing predicate actually tests
`has_bounded_zero_min && !has_unbounded_quantifier` under a group whose maximum
count exceeds one. It neither uses nullability directly for that decision
nor models capture participation. `NULLABLE_DISCARD` was a feature label for
that approximation, not a proof that the body's complete expression is
nullable. This explains both over-routing of `((a?)b)*` and missed cases such
as `(a?)?` and `(?:|a)*`.

### A.2 Direct checks for this design

On 2026-09-15, source was inspected at `89a4c50f5`; Node `v24.7.0` and the
available `./lambda.exe` were used for these direct checks. The existing
binary was not rebuilt for this documentation change. Arrays below show
the indexed match entries only, with `undefined` preserved explicitly.

| Pattern | Subject | Node result | Available Lambda result |
|---|---|---|---|
| `(a?)?` | `''` | `["", undefined]` | `["", ""]` |
| `(a{0,3})*` | `''` | `["", undefined]` | `["", ""]` |
| `(?:\|a)*` | `'a'` | `["a"]` | `[""]` |
| `(?:(a)\|b)+` | `'ab'` | `["ab", undefined]` | `["ab", "a"]` |
| `(a*)*` | `''` | `["", undefined]` | `["", ""]` |
| `((a?)b)*` | `'b'` | `["b", "b", ""]` | `["b", "b", ""]` |
| `(a?){1}` | `''` | `["", ""]` | `["", ""]` |

### A.3 Implementation seams

Line numbers refer to the inspected source and can drift; symbols are the
stable lookup keys.

| File / symbol | Role |
|---|---|
| [`js_regex_router_scanner.cpp`](../../lambda/js/js_regex_router_scanner.cpp) — `js_regex_scanner_analyze` | Production streaming-summary analysis and reason mask; retains only an open-group stack. |
| [`js_regex_router_poc.cpp`](../../test/js_regex_router_poc.cpp):287 — `regex_ast_analyze` | Earlier test-only comparison implementation. |
| [`test_js_regex_router_poc_gtest.cpp`](../../test/test_js_regex_router_poc_gtest.cpp) — `ScannerAndAstRouteAudit` | Comparative routing corpus, exact-shape cases, Node digest gate, and explicit resource-error gate. |
| [`js_runtime.cpp`](../../lambda/js/js_runtime.cpp) — `js_create_regex` | Runs analysis after normalization; a required backtracker compile failure is an explicit error, never RE2 fallback. |
| Same runtime file — `js_regex_match_internal` | Shared matching dispatch that distinguishes no-match from backtracker resource failure. |
| Same runtime file:16036 — `js_regex_reset_stale_repeated_captures` | Post-match span cleanup; not a proof of capture participation and not a routing substitute. |
| Same runtime file:18474 — `js_regex_exec` | Public capture materialization, indices, and state publication. |
| [`js_bt_regex.cpp`](../../lambda/js/js_bt_regex.cpp) — `js_bt_exec` | Matcher results and resource budgets, including explicit exhaustion/allocation outcomes. |

Keep runtime code changes in the first-party JS subsystem, use existing
`lib/` containers and allocation helpers, and share lexical helpers rather
than copying another parser's implementation. No vendor changes are needed.

### A.4 Implementation verification

The implemented scanner's comparison run contains 4,260 parser-accepted
pattern/flag cases: 3,230 scanner/POC route agreements, 164 production-only
backtracker routes, and 866 POC-only routes. The latter are expected because
the POC remains deliberately broader than the refined summary rule.

`StructuralRoutesPreserveNodeExecResults` compares 13 targeted routed and
RE2-retained patterns against 765 subjects each, with zero divergent result
digests. `ResourceExhaustionIsNotReportedAsNoMatch` drives a routed
backreference pattern beyond the backtracker budget and verifies that JS exits
with the explicit resource error rather than printing a fabricated result.

## Appendix B — Brief implementation history

- **2026-05-23 / 2026-05-24 — initial backtracker proposal and landing.** The
  original design proposed a compact ECMAScript continuation matcher after
  RE2 and post-filters reached their semantic limit. It targeted roughly 16
  of 17 then-observed RegExp Test262 failures, including captured lookbehind,
  backreferences, nested lookarounds, and nullable quantifiers. Commit
  `a854e5ff9` introduced the initial implementation.
- **2026-06 through 2026-09 — integration and maintenance.** The matcher
  gained build/test integration, parser and runtime fixes, and subsequent
  structural cleanup. It remains a selected fallback rather than replacing
  RE2; `test_js_bt_regex_gtest` exercises its direct contract.
- **2026-09-15 — routing audit.** The `RegexAst` POC compared the production
  lexical route with an independently structured analysis, then compared
  routed cases with Node. It exposed 31 divergent representative results and
  showed that the old token-presence predicate both missed real semantic
  differences and over-routed safe cases.
- **2026-09-15 — this consolidation.** The former proposal is folded into
  this document. Its retained architecture appears in §2; its narrow routing
  policy and its “budget exhausted means no match” behavior are superseded by
  Decisions 1.1–1.5 and §9.
- **2026-09-15 — structural router implementation.**
  `js_regex_scanner_analyze` landed with a depth-bounded frame stack and the
  `nullable`/`has_capture`/`may_skip_capture` reductions. Required
  backtracker compilation and execution resource failures became explicit
  errors, and the focused Node/production regression gates were updated to
  require equivalence.

## Appendix S — Earlier proposals and why they were revised

- ~~Convert RE2 empty-string captures to `undefined`.~~ Rejected: legitimate
  empty captures must survive, and a whole-match difference cannot be repaired
  by capture formatting (§3).
- ~~Route every repeated group containing a capture.~~ Refined by Decision 1.2:
  it routes ordinary `(ab)*` unnecessarily and misses both a single optional
  nullable group and capture-free empty alternatives.
- ~~Route only repeated bodies containing a bounded optional token, excluding
  bodies containing unbounded quantifiers.~~ Replaced in this proposal by the
  summary rules in §5–§6. Token presence is not whole-body nullability, and
  `(a*)*` demonstrates that the unbounded exception is unsound.
- ~~An AST is necessary for structured routing.~~ Rejected for this scope:
  the required facts compose as groups close. A retained routing AST adds
  storage without changing these analyses.

The initial proposal treated budget exhaustion as no-match and limited routing
to a narrow target-test set. Those historical choices are not correctness
guarantees for expanded routing. The implementation now preserves explicit
failures and the independent assertion/backreference gates described in §9.
