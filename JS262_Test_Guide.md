# JS262 Test Guide

This guide records the current mixed-mode Test262 harness configuration and
measured experiments. Absolute timings are release-build measurements from
2026-08-29 on the development host; they are comparison data, not a portable
performance guarantee.

## Mixed-mode configuration

The runner loads `test/js262/mir_list.txt` for the MIR exceptions. Synchronous
tests outside that list use the AST interpreter in 600-test batches. The
remaining tests use the existing MIR/JS-harness paths. The measured full run
used:

```text
./test/test_js_test262_gtest.exe --batch-only --jobs=8 --js-timeout=30
```

The run discovered 42,913 files, skipped 7,866, and executed 35,047 tests.

## Realm-template experiment

On 2026-08-29, the AST native-harness path was measured with and without a
pristine realm template. Both runs used the same release binary, test set,
worker count, 600-test AST batch size, and mixed-mode partitioning.

| Mode | Process wall time | Batched wall time | Peak RSS | Result |
|---|---:|---:|---:|---|
| Template enabled | 141.64 s | 138.2 s | 3,038.9 MB | 35,047/35,047 passed; one AST batch was SIGKILLed and recovered through the retry path |
| Template disabled | 59.62 s | 58.2 s | 430.2 MB | 35,047/35,047 passed; no retry or batch crash |

The template was approximately 2.38x slower and used approximately 7x more
memory. CPU time was nearly unchanged, so the regression came from retaining
per-test allocations in the reused heap. The template reset restored rooted
realm snapshots but did not reclaim unreachable test objects between tests.

This experiment was reverted. The current harness uses the ordinary AST realm
recycle path. The raw records are preserved in
[`temp/js262_realm_template_enabled.log`](temp/js262_realm_template_enabled.log)
and
[`temp/js262_realm_template_disabled.log`](temp/js262_realm_template_disabled.log).

The result is consistent with the ownership constraints in D4.3.1 (the
non-moving mark-and-sweep heap) and D5.3.3 (precise `RootFrame`/`Rooted`
ownership): a snapshot can preserve realm identity, but it does not by itself
define a reclaimable per-test allocation boundary.
