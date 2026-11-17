# Isolated MIR compilation experiments

`mir-fast-l0.patch` is **not applied to the vendored source or normal builds**.
It is a user-requested POC from 2026-11-18, built and measured on macOS ARM64.
The normal L0/L2 paths remain available; no candidate met the performance goal.

The patch applies to the current, already-patched `lambda/mir` snapshot, not
directly to pristine upstream. `test/benchmark/build_mir_fast_l0.py` copies that
snapshot into `temp/`, applies this patch, builds an optimized MIR archive and
relinks existing release Lambda objects into a separate executable. It checks
that vendor source hashes did not change. Run `make build-release-compile`
first. The applied-vendor-patch verification wildcard does not include this
subdirectory.

Only the experimental executable reads `LAMBDA_MIR_FAST_L0=stack|static|compact`.
The switch applies to L0 on ARM64; tied-register functions retain the normal
allocator. Keep `LAMBDA_DISABLE_MIR_CACHE=1` when switching experimental modes.
`LAMBDA_MIR_FAST_PROFILE=<path>` writes per-function JSONL phase timings,
effective mode, allocation fingerprints and emitted byte counts; omit it during
execution timing. Its file I/O is outside the reported per-function timers.

See [measurements and qualification](../../test/benchmark/MIR_Fast_L0_POC_2026-11-18.md).
Behavioral equivalence and precise-root validation follow S1.6 and D8.6.2–D8.6.3;
the removal of the preceding AST-native experiment follows D8.3.1v3.
