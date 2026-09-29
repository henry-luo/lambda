# JS, MVP, and untyped Lambda reference survey

See the [analysis and next proposal](../../../../vibe/impl/JS_MVP_Untyped_Lambda_Tuning_20260929.md).

These are port timings, not an attribution of compiler speedup. Three rotating rounds use the same archived O3/LTO/NDEBUG release-profile host, explicit JS MIR and Lambda JIT. JS and MVP are checked against Node 22.13.0 output; Lambda against each source's golden file. Instrumented runs are separate.

| Workload | Full JS ms | MVP ms | Lambda untyped ms | JS/MVP | JS/Lambda port |
|---|---:|---:|---:|---:|---:|
| r7rs/fft | 3.897 | 0.967 | 0.288 | 4.03 | 13.53 |
| r7rs/mbrot | 2.196 | 0.868 | 0.734 | 2.53 | 2.99 |
| awfy/bounce | 4.313 | 2.349 | 0.069 | 1.84 | 62.50 |
| awfy/mandelbrot | 48.110 | 31.827 | 39.701 | 1.51 | 1.21 |
| awfy/nbody | 669.000 | 405.060 | 6.958 | 1.65 | 96.15 |
| awfy/richards | 940.518 | 566.485 | 377.726 | 1.66 | 2.49 |
| awfy/havlak | 13696.836 | 5953.251 | 72.429 | 2.30 | 189.11 |
| awfy/cd | 4176.058 | 1791.150 | 591.341 | 2.33 | 7.06 |
| kostya/base64 | 482.296 | 212.788 | 15.997 | 2.27 | 30.15 |
| larceny/triangl | 6253.830 | 2952.890 | 235.118 | 2.12 | 26.60 |
| text/fast_diff | 1983.515 | 895.117 | 243.701 | 2.22 | 8.14 |
| text/microdiff | 816.449 | 150.726 | 47.806 | 5.42 | 17.08 |
| text/text_search | 34331.882 | 19302.373 | 1500.480 | 1.78 | 22.88 |
| text/three_way_merge | 9849.158 | 5614.443 | 2949.990 | 1.75 | 3.34 |
| text/log_pipeline | 13315.365 | 11923.606 | 4664.830 | 1.12 | 2.85 |

## Audited work differences

Havlak's canonical Lambda port repeats loop finding once, versus 50 times in bundled JS. A diagnostic copy changes only that argument. Text search's Lambda bad-character table omits the final pattern character; its diagnostic copy includes it as JS does. Neither probe changes canonical benchmark sources. These probes correct named differences, not every possible port difference.

- awfy/havlak: corrected Lambda **1922.310 ms**, JS/Lambda **7.13×**.
- text/text_search: corrected Lambda **1965.770 ms**, JS/Lambda **17.46×**.

NBody and Bounce use flat numeric arrays in Lambda and objects/methods in JS. CD uses different tree/node layouts. Triangl uses Boolean/numeric Lambda arrays versus Uint8Array/Int32Array in JS. The registry's untyped text-search file still declares three outer scalar locals as `int`; its search functions are unannotated. No aggregate JS/Lambda compiler-speedup ratio is computed.

## Reproduction

Run from the repository root, serially:

```sh
python3 test/benchmark/js_mvp/untyped_reference_20260929/measure_reference.py --binary temp/js-tune-20260929/overall-after-profile.exe --output test/benchmark/js_mvp/untyped_reference_20260929/reference.json
python3 test/benchmark/js_mvp/untyped_reference_20260929/diagnose_reference.py --reference test/benchmark/js_mvp/untyped_reference_20260929/reference.json --output test/benchmark/js_mvp/untyped_reference_20260929/diagnostics.json
python3 test/benchmark/js_mvp/untyped_reference_20260929/summarize_reference.py
```

`reference.json` contains all 135 timing samples and provenance. `diagnostics.json` contains six work-corrected timings, eight instrumented runs, exact source edits, MIR hashes and raw trace text. MIR/probe files are under `temp/js-untyped-reference-20260929/`; the diagnostic harness regenerates them. `summary.json` is derived by the script above. Static MIR counts include wrappers and cold fallback bodies; trace counters count events rather than CPU time.

An initial survey used raw AWFY module files instead of the canonical bundles. It was stopped, excluded, and retained only under `temp/js-tune-20260929/untyped-reference-raw-awfy-rejected.*`. The recorded survey uses `js_benchmark_manifest` sources.
