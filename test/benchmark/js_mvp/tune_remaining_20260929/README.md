# LambdaJS release-profile follow-up, 2026-09-29

This follows the [MVP comparison and tuning proposal](../../../../vibe/impl/JS_MVP_Release_Profile_Comparison_20260928.md) and the [Number-entry/loop-state implementation](../tune_20260928/README.md). It adds guarded array consumers and lengths, a direct predicted-field publication check, a smaller exact-root call activation, lower RegExp bulk-loop materialization, and a bounded ASCII decimal parse. The fast paths keep the existing semantic continuation on a guard miss (**S1.11**, **D8.2.3**) and use immutable shape predictions rather than an inline cache (**D8.4.1v2**). No normative ruling changes.

## Change and mechanism

- A present own dense array element reaches `js_elements_get_number` without creating a root frame. Wide scalars retain the ordinary Get so their transient home remains valid (**D5.3.4**). A boxed Number key from an inner array read can now enter the existing guarded packed-array strict-equality lane; other keys, holes, descriptors, and changed receivers perform the original Reference/Get.
- The array-length guard has one shared native reader. Two candidate `.length` values can stay native through subtraction, and a proven Number can be compared with a guarded `.length` without boxing the successful read. Each guard is rechecked at the physical read. A miss performs the original Get and arithmetic/comparison in source order. The new tests exercise ordinary getters, throws, and the direct array path.
- A compile-predicted field now checks its per-instance constructor reservation bit inline after the exact shape guard. The old helper call is removed, but an unpublished field still misses to the property kernel. The strict direct-call activation roots its receiver in the already rooted active activation slot instead of making a duplicate native root (**D5.3.2**).
- The guarded builtin RegExp bulk loops update legacy last-match state once for the final successful match, including before an error exit. Literal replacement text with no `$` bypasses the replacement-template parser; template replacements retain it. Custom hooks stay on the existing generic path.
- `Number` now parses pure ASCII decimal strings of at most 15 digits directly. Those integers fit exactly in an IEEE double; signs, whitespace, radix prefixes, invalid text, and longer values retain the existing full grammar. This removes repeated string copying and `strtod` work from `log_pipeline`'s split numeric fields.

The [finalized MIR summary](mir_summary.json) shows the predicted-field microbench changing from two static `js_constructor_shape_field_is_initialized` calls to zero. The new array-length contract MIR contains `dsub` and `dlt` fast operations with `js_subtract` and `js_cmp_raw` fallback sites. These are mechanism checks, not execution counts.

## Paired ordinary-release measurements

The pre-follow-up control is the ordinary-release archive `temp/js-tune-20260928/final-release.exe` (SHA-256 `b72a797657f32a4717c5a912406bd6ebad802fffb8fb5db7bb9fcb3769c55574`). The final candidate is `temp/js-tune-20260928/ascii-decimal-release.exe` (SHA-256 `f684c7a9e967f74b527f7d94fe4294082a12e88adab35bf5de8782f774f6b561`). Every A/B below pins `JS_EXECUTION_BACKEND=mir`, alternates order, and validates normalized stdout. The ratios are candidate/control execution-time medians; upper bounds are one-sided 95% paired-bootstrap bounds. Each linked JSON retains both binary and source hashes, statuses, wall times, and exact sample order.

| Change and workload | Pair count | Candidate/control | Upper bound | Candidate wins |
|---|---:|---:|---:|---:|
| [RegExp literal loop](regex-literal-paired.json), `beng/regexredux` | 41 | **0.9939** | 0.9981 | 27 |
| [Own dense read](array-log-paired.json), `text/log_pipeline` | 3 | **0.9827** | 0.9866 | 3 |
| [Array-value equality](array-equality-search-paired.json), `text/text_search` | 5 | **0.9531** | 0.9561 | 5 |
| [Native length subtraction](array-length-search-paired.json), `text/text_search` | 5 | **0.9662** | 0.9728 | 5 |
| [Native length comparison](array-length-compare-search-paired.json), `text/text_search` | 5 | **0.9076** | 0.9109 | 5 |
| [Predicted-field microbench](property-hit-paired.json) | 21 | **0.9902** | 0.9976 | 15 |
| [Call-root microbench](call-root-paired.json) | 41 | **0.9932** | 0.9966 | 31 |
| [ASCII decimal microbench](ascii-decimal-micro-paired.json) | 21 | **0.9302** | 0.9320 | 21 |
| [ASCII decimal `log_pipeline`](ascii-decimal-log-paired.json) | 5 | **0.9888** | 0.9934 | 5 |

The staged `text_search` results use the exact preceding binary as each control, so their ratios should not be added. The two length changes were also screened on a shorter same-body search fixture in [21 pairs each](array-length-short-paired.json) and [21 pairs each](array-length-compare-short-paired.json); the canonical rows above are the acceptance evidence. The property microbench includes method calls, so the 0.7% call-root result is for that probe, not a claim about all calls.

The [63-row canonical screen](remaining-final-canonical-screen.json) compared the pre-follow-up control with the parser-inclusive final candidate: **63/63** rows completed and normalized output matched. Its single pair per row is a correctness and triage screen, not evidence for small timing differences. A five-pair [Havlak call-path isolation](call-root-havlak-paired.json) was effectively flat (1.0024, two candidate wins). Five-pair [pre-parser hit-path controls](remaining-hitpath-controls-paired.json) put `log_pipeline` at **0.9715** versus the pre-follow-up control, with all five candidate wins; the subsequent decimal change won its own five canonical pairs above.

The [final-archive 21-pair control replay](remaining-final-control-replay-paired.json) gives the following output-equal results against the pre-follow-up binary:

| Control workload | Final/control | Upper bound | Final wins |
|---|---:|---:|---:|
| `awfy/bounce` | 1.0080 | 1.0138 | 4/21 |
| `awfy/richards` | 1.0084 | 1.0123 | 5/21 |
| `awfy/cd` | 1.0012 | 1.0032 | 7/21 |
| `beng/knucleotide` | 1.0035 | 1.0073 | 7/21 |
| `jetstream/hashmap` | 1.0023 | 1.0029 | 7/21 |

Bounce's roughly 0.03 ms difference is far smaller than the earlier parser-free candidate's one-pair 1.595× signal; [its replay](bounce-replay-paired.json) had already shown that signal was not stable. The same applies to the earlier `knucleotide` 0.542 one-pair signal and [its replay](knucleotide-replay-paired.json). [Pre-parser 21-pair controls](remaining-control-replay-paired.json), [pre-length versus pre-parser final](length-control-isolation-paired.json), [subtraction-only](length-subtract-richards-paired.json), and [comparison-only](length-compare-richards-paired.json) locate the Richards slowdown in binary revisions that added the length paths. Richards's source has no `.length` read; the [finalized MIR comparison](mir_summary.json) differs only in relocated stack-overflow-error pointer literals between pre-length and final binaries. The executed-guard mechanism is therefore **not established**. We retain the much larger `text_search` gain and report the small control regressions rather than assigning them to a specific hot path.

## Correctness and limits

The changed optimizer cases pass with forced GC and freed-memory poisoning. The [Navier–Stokes post-timing density oracle](remaining-navier-oracle-paired.json) passes on both binaries (**S1.11**). The final aggregate Lambda and Test262 results, exact build provenance, and any known build-tool limitation are in [validation.json](validation.json).

This implements the measured guarded array read/equality/length slice, predicted-field and call-root cost reductions, guarded RegExp bulk-loop reduction, and bounded numeric parsing. Mixed array arithmetic and stores, broader call capability elimination, and `log_pipeline` split/slice allocation remain coverage targets; the current evidence does not justify a cache or a broader representation change. Runtime roots and returned completions remain governed by **D5.3.2** and **D8.4.3v2**.
