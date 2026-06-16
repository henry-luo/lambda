---
name: node-js-baseline
description: Current LambdaJS Node.js official-test baseline, how to measure it, and where the structural gaps are
metadata:
  type: project
---

LambdaJS Node.js compat: **1462 / 3521 official parallel tests pass (41.5%)** — baseline locked in on 2026-06-16 (fresh `make release` build). Stale 1,414 baseline preserved as `test/node/official_baseline.txt.stale-1414`. Re-baseline with `make node-update-baseline` (targets were renamed from `node-official(-update-baseline)` to `node`/`node-update-baseline`).

**How to measure:** official tests live in `ref/node/test/parallel/` (3938 files). Runner: `test/test_node_gtest.cpp` → `./test/test_node_gtest.exe`. `make node-baseline` rebuilds the runner and executes; `make node-update-baseline` rebuilds, runs, and writes the baseline. Per-module probe: `--modules=<prefix> --timeout=15000`. Crasher quarantine written to `temp/_node_official_crashers.txt`. The runner WON'T write the baseline if it detects regressions vs the existing baseline — to force a fresh write, move/delete the existing file first.

**The structural story (see [[node-js-stream-linchpin]]):** Node3 plan (`vibe/jube/Transpile_Node3.md`) landed out of order — crypto ciphers, Buffer, assert, TLS, module-resolution, process/os all advanced — but **Phase 7 (stream state machine) was never done**. `lambda/js/js_stream.cpp` is still ~841 LOC of map-based pseudo-state (no highWaterMark/backpressure, no Symbol.asyncIterator). http/net/zlib/crypto-cipher were each built standalone on libuv instead of on streams. Node4 proposal at `vibe/jube/Transpile_Node4.md`.

**26 crashers** concentrated in net (11, connect-error/autoselectfamily null-derefs), child_process spawn (3), tls (3). **dns = 0/29** (dns.lookup callback throws "TypeError: is not a function"). **https has no TLS** (delegates to plain http on :443 despite real `js_tls.cpp`). `util.promisify` is a literal stub. async_hooks/AsyncLocalStorage are stubs.
