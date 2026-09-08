# Lambda string/array micro-benchmarks

One hot loop each, `clock()` around the loop only (`clock()` returns **seconds**).
Used to isolate the per-operation costs analysed in
[`vibe/impl/Lambda_Impl_Tune22.md`](../../../vibe/impl/Lambda_Impl_Tune22.md)
§2.2 and to A/B its fixes (§5).

```bash
for f in test/benchmark/ls_micro/*.ls; do LAMBDA_TIER=jit ./lambda.exe run "$f"; done
```

Run against a **release** build (`make release`; note `make test-lambda-baseline`
overwrites `lambda.exe` with a debug build), and A/B against an archived binary
in `test/benchmark/exe/` rather than a rebuild. `LAMBDA_TIER=jit` is required:
the auto tier measures a different thing.

| Script | Isolates |
|---|---|
| `string_index.ls` | `s[i]` on a `join`-built string vs a `++`-built one (T22-1a: `fn_join` sets `is_ascii = 0`, so the joined form walks UTF-8 per index) |
| `split_call.ls` | `split(line, " ")` cost per call, `let`-bound vs used directly (T22-1b allocation; the declaration boundary is *not* the cost) |
| `literal_compare.ls` | an 8-arm `key == "literal"` chain, untyped vs `string` parameter (T22-1c inline literal compare) |
| `text_search_typed_params.ls` | `text/text_search.ls` with `int[]` on the four search-function parameters and nothing else changed (T22-2: what annotation alone buys, and what the typed lane still leaves behind) |

## Baselines (2026-09-09, `exe/lambda-v38-b23793e832`, quiet machine)

Ranges are two runs; the *ratios*, not the absolute cells, are the claim.

| Measurement | Lambda v38 | reference |
|---|---:|---:|
| index a 70 KB `join`-built string ×11,000 | 359–377 ms | same string built with `++`: **0.09 ms** |
| `split` of a 12-word line ×1,000,000 | 718–746 ms | Node `String.split`: **30 ms** |
| 8-arm literal compare ×2,000,000, untyped param | 135–184 ms | `string` param: 113–125 ms; Node: ~2 ms |
| `text_search` full workload | 15,222 ms | with `int[]` params: **7,148 ms**; Node: 782 ms |

The `text_search` row's remaining ~9x over Node after annotation is the typed
lane itself: the element compare lowers `i2d, i2d, deq` and `fn_len_a` stays a
call per iteration (Tune22 §2.2, T22-2a/2b).
