# LambdaJS numeric tuning, 2026-09-28

The first two numeric-path items in the [MVP comparison proposal](../../../../vibe/impl/JS_MVP_Release_Profile_Comparison_20260928.md) are implemented in the ordinary LambdaJS MIR compiler. **S1.11** requires hosted JavaScript semantics; **D3.3.2v2** makes Number inference an implementation fact, not a source contract. The existing boxed entry remains the fallback when an exact Number guard fails. No normative ruling changed.

## Change and mechanism

- A read-only formal used in `r + x * step` can now become a guarded Number candidate when the arithmetic peer has only Number literals and guarded formals. A write to any formal or a shared closure environment declines this new admission. String concatenation, object coercion, and BigInt remain on the boxed entry.
- Local Number proof first checks declaration initializers without following loop writes, rejecting initializer cycles. It then checks the initializer and every write with the initialized fact active. This admits valid mutually dependent loop state while a non-Number write still widens the binding.

The [finalized MIR summary](mir_summary.json) compares the *ordinary release* control and final binaries. `r7rs/mbrot`'s `count_n` changes its first two parameters from `i64` Item to `d` Number. Its static `js_add`/`js_subtract`/`js_multiply`/`js_cmp_raw` calls change from **5/1/4/1** to **0/0/0/0**. `awfy/mandelbrot`'s native method retains its Number parameter; the same calls change from **3/1/4/1** to **0/0/0/0** after the loop-binding proof. Both bodies now use native double operations for those sites. This addresses the specific helper traffic measured in the [MVP profile](../profile_20260928/diagnostics.json).

## Paired ordinary-release results

Control SHA-256: `ab5efd18a3f72c8014f3c6dc769dc5e71b73cd177814936a990c93e552111132`  
Final SHA-256: `b72a797657f32a4717c5a912406bd6ebad802fffb8fb5db7bb9fcb3769c55574`

Both archives are retained under `temp/js-tune-20260928/`; [validation provenance](validation.json) records their paths and build/check logs. A later `make release` after a test-only edit had identical `__text` bytes but a different Mach-O UUID and code-signature digest; the exact measured archive was restored as `lambda.exe`. Each row below uses the same source on both binaries, `JS_EXECUTION_BACKEND=mir`, alternating order, 21 pairs, and identical normalized stdout. Values are execution-time medians; the upper bound is the one-sided 95% paired-bootstrap bound on candidate/control.

| Workload | Control ms | Final ms | Final/control | Upper bound | Final wins |
|---|---:|---:|---:|---:|---:|
| r7rs/mbrot | 11.159 | 2.245 | **0.2012** | 0.2042 | 21/21 |
| awfy/mandelbrot | 485.800 | 47.455 | **0.0977** | 0.0980 | 21/21 |

[All paired samples](final-numeric-paired.json) support about **4.97×** and **10.24×** faster execution on these workloads, respectively. Startup/JIT-inclusive wall times are separate fields in the JSON.

## Correctness and regression screen

The runtime change passed `make test-lambda-baseline` **5,998/5,998** and `make test262-baseline` **40,261/40,261** with zero regressions. After a test-only temporal-dead-zone assertion was added, the optimizer contract suite passed again **79/79**. The two new contract cases check native MIR shape, numeric loop results, dynamic string/object/BigInt fallback, formal writes, captured-formal mutation, non-Number loop widening, and initializer-cycle rejection. The [Navier–Stokes post-timing density oracle](navier-oracle-paired.json) passed on both binaries (**S1.11**).

The [63-row canonical screen](final-canonical-screen.json) used one MIR-pinned pair per row: **63/63** completed and every normalized output matched. One pair is a correctness and triage screen, not evidence for small timing differences. The largest screen signal was `text/text_search` at 1.414× candidate/control; [three interleaved pairs](text-search-replay.json) instead gave **40.800 s vs 40.541 s** medians, two candidate wins, and a 1.131 upper bound. The reduced-round diagnostic retains the same search function bodies, and its finalized MIR differs only in relocated error-string addresses ([MIR summary](mir_summary.json)). The slowdown is **not confirmed**. The same MIR-only difference holds for canonical `three_way_merge`; [three pairs](three-way-merge-replay.json) observed a small 1.0122 ratio, insufficient for attribution. Screen signals for DeltaBlue and Cube3D likewise shrank to 1.0019 and 1.0005 in [21-pair replays](regression-controls-paired.json).

## Remaining work

This change completes guarded Number candidate inference and loop-carried local proof. The proposed guarded array-value/length route is still needed for `text_search`: its native search bodies retain boxed comparisons and array-related helpers. The successful property/call path and string/RegExp materialization proposals require separate cost attribution and exact release A/B work. **D8.4.1v2** still rules out mutable inline caches, while **D5.3** and **D8.4.3v2** continue to govern precise roots and completion handling. The current change does not alter ownership or call effects.
