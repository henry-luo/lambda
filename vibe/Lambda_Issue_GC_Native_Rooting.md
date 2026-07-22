# Native-code GC rooting: sizing, root cause, and severity

**Status:** INVESTIGATION — 2026-07-22. Problem sized, root-cause pattern
confirmed with one verified fix landed. The remaining work is a systematic
campaign and is **not** started.

**Found by:** the MT4 forced-GC stress sweep added in
`vibe/Lambda_Design_MIR_Emission_Test.md` §10. This document is the follow-up
that sizes what that sweep exposed.

---

## 1. Sizing

Every `test/js/*.js` with a golden was run unstressed and then under
`LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1
LAMBDA_GC_POISON_FREED=1`, comparing exit status and stdout
(`temp/gc_sweep.py`, results in `temp/gc_sweep_results.json`).

| | count |
|---|---|
| scripts with goldens | 244 |
| **diverged under forced GC** | **107 (44%)** |
| — silent output mismatch | 66 |
| — segfault | 30 |
| — timeout | 11 |

Caveat: `lambda.exe` was rebuilt twice during the ~15-minute sweep, so a
handful of late scripts ran against a partially fixed binary. The magnitude is
sound; treat individual entries as indicative.

## 2. Root cause

Native runtime helpers hold raw `Item` locals across calls that can allocate,
and therefore collect. Precise rooting does not scan the native stack, so those
locals are not roots and the objects they name are freed mid-function.

`lambda/runtime/lambda-root-frame.hpp` exists precisely for this (CR2 of
`Lambda_Design_Stack_Rooting.md`): native code must hold live values in
`RootFrame` / `Rooted<Item>` slots. `js_runtime.cpp` already uses that API in
about 112 places — the migration was simply never completed, and the unconverted
remainder is large.

### Verified exemplar (fixed)

`new Set(iterable)` failed under forced GC with
`TypeError: Set.prototype.add is not callable`. The collection created by
`js_collection_create` was reachable only from native locals while
`js_collection_link_prototype` called `heap_create_name` and `js_property_get`;
the collection was freed there, and the prototype was written into dead memory,
so the later `"add"` lookup found nothing.

Fixed in `lambda/js/js_runtime.cpp` by rooting the collection from the moment of
creation and rooting the constructor/prototype/iterator/adder across the
allocating calls in both `js_collection_link_prototype` and
`js_set_collection_new_from`. Verified: `new Set(["a","b"])` now survives
`FORCE_EVERY=1 + POISON_FREED=1`, and `make test-lambda-baseline` stays at
3551/3551.

Note the first attempt at this fix failed and was instructive: rooting *after*
`js_collection_link_prototype` returned did nothing, because the object was
already dead by then. Roots must start at creation, not at first apparent use.

## 3. Severity — read this before prioritising

Three measurements qualify the headline number.

**`precise-only` is the shipped default.** `heap_default_gc_root_mode()`
(`lambda/runtime/lambda-mem.cpp`) returns `GC_ROOT_MODE_PRECISE_ONLY` for
non-C2MIR builds, which is the standard build. Conservative native-stack
scanning is *not* active in ordinary runs, so these are latent bugs in the
shipped configuration rather than a not-yet-ready mode.

**But the crash needs poisoning, and appears in both root modes.** For
`typed_arrays.js`:

| configuration | result |
|---|---|
| natural GC | ok |
| `FORCE_EVERY=1`, no poison | ok |
| `FORCE_EVERY=1` + `POISON_FREED=1` | SIGSEGV |
| `compatibility` + `FORCE_EVERY=1` + poison | SIGSEGV |

Compatibility mode conservatively scans the native stack and still crashes, so
this particular failure is not explained by precise rooting alone — a
register-held reference invisible to a stack scan is the likeliest candidate.
Without poisoning the freed memory still reads as plausible data, which is why
these stay silent in normal runs.

**Reachability at realistic pressure is mixed.** Sampling ten segfault-class
scripts at `FORCE_ONE_IN=10` (collect on ~10% of allocations) with poisoning:
five survived, five still failed — including `lib_acorn`, `lib_ajv`,
`dom_jquery_lib`, `dom_jquery_fx`, and `collections_advanced`. So this is not
purely an artifact of collecting at every single allocation; part of the class
is reachable by heavy real workloads.

## 4. Recommended approach

Fixing this one function at a time does not scale: 107 scripts sit on top of
thousands of native call sites across `js_runtime.cpp` (~40k lines) and
`js_globals.cpp` (~13k lines). Two better starting points:

1. **Mechanical audit before manual fixes.** The bug has a machine-detectable
   shape: an `Item`-typed local that is live across a call which can allocate,
   in a function that does not open a `RootFrame`. A static pass over the JS
   runtime — even a coarse one keyed on the known allocating helpers
   (`heap_create_name`, `heap_strcpy`, `js_array_new`, `js_array_push`,
   `js_property_get`, `js_call_function`, …) — would turn an open-ended search
   into a work list, in the spirit of `utils/check_gc_root_hazards.py`.
2. **Fix shared helpers first and re-measure.** `js_array_from`,
   `js_collection_*`, and the iterator step/close helpers are reached by many
   scripts. Re-running the sweep after each shared-helper fix shows how much it
   moved the 107, which is a far better guide than the per-script list.

Do not "fix" this by re-enabling conservative scanning: that reverses the
rooting design's scan retirement, and §3 shows compatibility mode does not
actually prevent the crashes anyway.

## 5. Reproduction

```bash
LAMBDA_GC_ROOT_MODE=precise-only LAMBDA_GC_FORCE_EVERY=1 \
  LAMBDA_GC_POISON_FREED=1 ./lambda.exe js --no-log test/js/<script>.js
```

Re-run the full sizing sweep with `python3 temp/gc_sweep.py`. The corpus-level
gate is `make test-mir-gc-stress`; `test/js/array_callback_gc_roots.js` is
carried there on `kKnownForcedGcFailures`, which asserts it *still* fails, so
whoever fixes it will be told to retire the entry. It remains failing after the
fix in §2 because a second unrooted site in `Array.from` over an iterator empties
the array — that is the next concrete lead.
