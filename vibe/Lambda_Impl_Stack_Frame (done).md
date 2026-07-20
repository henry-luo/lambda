# Lambda Stack Frames — Phase 1 Implementation Record

**Status:** implemented and verified
**Completed:** 2026-07-15
**Design:** `vibe/Lambda_Design_Stack_Frame.md` (SF1–SF20)
**Scope:** Lambda MIR-Direct plus the shared runtime. JS frame emission remains phase 2 in `vibe/Lambda_Impl_Stack_Frame_JS.md`; C2MIR frame emission is intentionally unchanged per OS6.

---

## 1. Delivered architecture

Lambda execution now uses two thread-local, virtually reserved side regions:

| Region | Reserve | Contents | GC treatment |
|---|---:|---|---|
| root stack | 16 MiB | exact `Item`/GC-pointer roots | precisely scanned over `[side_root_base, side_root_top)` |
| number stack | 64 MiB | wide `int64`, `DateTime`, and out-of-band `double` payloads | never scanned as GC roots |

`lib/side_stack.c` owns reserve, watermark, snapshot/restore, allocation, and decommit behavior. POSIX uses a demand-paged `mmap`; Windows uses `MEM_RESERVE` followed by page-aligned `MEM_COMMIT` as a generated frame advances, and `MEM_DECOMMIT` after collection. `Context` carries the active bases, tops, commit limits, and hard limits so generated code can use direct loads/stores.

The MIR-Direct function shape is now:

1. load the current watermarks;
2. reserve the function's exact root-slot count and, only for a widened return, one number scratch slot;
3. execute with direct fixed-offset root stores;
4. funnel every result through one epilogue;
5. preserve the outgoing value, restore both watermarks, and return.

Root-slot count is known after lowering, so the checked prologue is inserted at the function anchor once. A rooted assignment is one inline memory store and makes no C rooting call. Functions with zero root slots do not advance the root stack. Async-owned variables do not receive redundant side-root slots.

The runner establishes the execution-base number extent. Shared boxing helpers used by JS, C2MIR, host helpers, and the MIR interpreter therefore retain script-lifetime behavior unless a Lambda MIR-Direct frame establishes a narrower extent.

---

## 2. Implemented stages

| Stage | Result | Main implementation points |
|---|---|---|
| 0a — compact int64 | complete | Signed 56-bit `int64` values are encoded inline with `LMD_TYPE_INT64`; only the wide residue needs backing storage. Full-domain representation tests cover both boundaries. |
| 0b — concat rebase | complete | Array/list copy and concat paths use owned-slot stores so scalar payload pointers are rebased into the destination rather than retaining a source/frame interior pointer. |
| 0c — telemetry | complete | `LAMBDA_MIR_LOG_FRAME_SLOTS=1` reports final per-function root and number-scratch counts. |
| 1 — precise roots | complete | Side-root region, precise GC scan, checked static frames, single epilogues, and recovery snapshots landed. The heap `JitGcRootFrame` implementation and all emitters were removed after confirming Lambda was its only user. |
| 2 — number region | complete | `push_l`, `push_k`, and the out-of-band float path allocate from the number side stack. The numeric `gc_nursery` source, headers, factory state, and build entry were deleted. |
| 3 — scoped numbers | complete | Lambda frames restore number watermarks; returns, container storage, environments, async frames, module/import bridges, and reads all obey re-homing rules. `INT64`/`DTIME`/`FLOAT` are no longer GC-root candidates. |
| 4 — cleanup | complete | `LambdaRootGuard` landed with the typed-array view constructors as its pilot; async root duplication and obsolete inference/root machinery were removed; GC-driven side-stack decommit and the scalar identity audit landed. |

No feature flag or legacy Lambda frame path remains.

---

## 3. Wide-scalar lifetime rules

### 3.1 Owned storage

Runtime-owned `Array`, `List`, `Map`, `Element`, environment, and async storage never retains a pointer into a transient number frame. Each logical Item slot has an owned scalar payload slot used by `owned_item_slot_store`; wide values are copied into that payload and the stored Item is rebased to it.

Reads from movable/owned storage call `scalar_storage_read`, which copies a wide scalar into the reader's current number extent before it can escape. Pool/arena/const containers carry `is_immortal`; their backing lifetime already exceeds execution, so reads may return the existing stable reference. The `LAMBDA_STATIC` input library similarly returns its pool-owned scalar references directly because that standalone build has no execution side stack.

`VMap` was included in the pointer-identity audit. Wide integer and `DateTime` keys now hash and compare by value, keys/values are stabilized on insertion, updates preserve the stable stored key, and getters/key iteration copy wide scalars out. The permanent regression covers wide `int64` and `DateTime` keys.

### 3.2 Captures and async suspension

Captured scalar values are stored in environment-owned tails and copied out on reads. Async frames allocate two equal halves:

- the Item half is registered as an exact GC root range and uses owned scalar payloads;
- the raw half holds arbitrary MIR `I64` spill bits and is deliberately unscanned.

Generated async functions reserve the compile-time slot count. Resizing preserves both halves independently. At `wait`, every live temporary needed by the resumed state is in the async frame; no suspended state points into a popped side-stack extent.

### 3.3 Return ABI

Boxed Lambda JIT-to-JIT calls return one caller-owned Item. Their epilogue uses the shared MIR scalar classifier and donates `frame_base[0]` only when the returned tagged payload lies inside the callee's `[frame_base, current_top)` number extent. Donation copies the raw payload down, retags the Item, and restores `side_number_top` to `frame_base + 1`; all other representations restore exactly to `frame_base`. Callers and generated boxed wrappers therefore forward the result without a scalar side channel or rebuild helper.

Native `T^E` returns still use a scalar result plus an error Item. MIR's ARM64 multi-result lowering loses the primary lane when a nested call feeds the return, so this error-only ABI stages the primary value in one reserved scratch slot and publishes the error Item through `Context::mir_return_lane`.

Imported cross-language functions bypass Lambda `_b` wrapper lookup because their module exports already use the direct boxed ABI.

Language-level `i64^` success/error returns cover the full `int64` domain, including `INT64_MAX`; the old value-as-error sentinel is not used on this path. The legacy C system-function registry still uses its historical sentinel contract, so `transpile_box_item` retains `push_l_safe` only at that compatibility boundary.

---

## 4. GC, failure, and host-helper integration

- `heap_gc_collect` passes the exact root side-region to the collector while retaining the conservative native-stack scan required by JS and unmigrated host code.
- The number region is absent from all GC root scans. The unrelated data-zone nursery in `gc_data_zone` remains.
- Runner stack-overflow recovery, test262 batch crash/timeout/MIR-error recovery, and cached-import execution snapshot both watermarks before entering generated code and restore them after `longjmp`/signal recovery.
- Side-stack exhaustion reports the existing catchable stack-overflow error instead of accessing beyond the reservation.
- Collection decommits unused pages above each live watermark, preventing transient deep execution from permanently setting the RSS floor.
- `LambdaRootGuard` is a non-copyable C++ RAII helper that appends exact dynamic roots above the current watermark and restores it on destruction. The typed-array external/storage view constructors are the pilot migration.
- Module globals, REPL values, and non-Lambda callers remain in the execution-base extent and therefore keep stable script-lifetime scalar backing.

---

## 5. Frame telemetry

The permanent stack-frame regression produced these final slot counts:

```text
_wide_0             roots=0  numbers=1
_read_69            roots=1  numbers=1
_make_reader_41     roots=1  numbers=1
_make_scalar_map_100 roots=0 numbers=1
_maybe_wide_238     roots=1  numbers=1
_delayed_wide_344   roots=1  numbers=1
_main_415           roots=40 numbers=1
main                roots=0  numbers=0
```

`numbers=1` is the reserved return scratch slot, not the number of dynamic boxes. Ordinary boxing advances within the current number extent through the shared helpers; the function epilogue restores the saved watermark. This is an implementation adjustment from the design's stronger static-number-slot ideal, while preserving its lifetime and GC invariants. Root slots remain exact, static, and inline.

---

## 6. Regression coverage

`test/lambda/proc/proc_stack_frame.ls` exercises the complete Lambda phase-1 surface on both native MIR and the MIR interpreter:

- `INT64_MAX` direct and nested returns;
- closure capture and readback;
- concat destination rebasing;
- map field storage/readback;
- successful and failing `i64^` returns, including `INT64_MAX` as success;
- wide values before, inside, and after async suspension;
- `VMap` wide-integer and `DateTime` keys plus wide values.

Existing focused tripwires also pass:

- `NegativeScriptTest.RuntimeError_StackOverflow` terminates and reports overflow;
- DeltaBlue retains live pointer locals across allocation/GC;
- Havlak exercises the historical `array_end` plus `push` BUG-001 path;
- binary JS bridge/module imports verify that the cross-language boxed ABI was not redirected through Lambda-only wrappers.

---

## 7. Final verification — 2026-07-15

| Gate | Result |
|---|---|
| `make test-lambda-baseline` | **3412 / 3412 passed**; Lambda scripts 530/530, JS gtests 330/330, Node preliminary 110/110 |
| `make test262-baseline` | **40261 / 40261 fully passed**; 0 failed, 0 non-fully-passing, 0 regressions, retry 0.0 s |
| stack-frame procedural regression | native MIR and `--mir-interp` output match the golden file |
| stack-overflow focused gtest | **1 / 1 passed** |
| release DeltaBlue | PASS; 92.9–97.6 ms across three current-tree runs |
| release Havlak + push | PASS; 72.0–73.6 ms across three current-tree runs |

The AWFY values are current release-build tripwires, not a controlled before/after A/B: the checked-in workloads changed after the older benchmark result snapshots. They establish correctness and a reproducible post-change timing range without making a false cross-version performance claim.

The full test262 run also served as the long-process memory/recovery soak: all 40,261 selected tests reported memory data and completed without a retry or non-fully-passing batch.

---

## 8. Phase boundary and residual work

Phase 1 is complete. The following are intentionally outside this document rather than incomplete Lambda work:

- JS-generated static root/number frames and JS two-lane returns (`vibe/Lambda_Impl_Stack_Frame_JS.md`);
- conservative native-stack scan retirement, which requires the JS and remaining host-helper migrations;
- broad adoption of `LambdaRootGuard` beyond the completed pilot;
- changes to C2MIR frame emission.

The old `lib/num_stack` utility may still exist for its standalone library tests; it is not the Lambda runtime's scalar allocation path. The deleted component is the runtime numeric `gc_nursery`.

## 9. Phase-1 success criteria

- [x] zero C calls for generated rooting stores;
- [x] zero root-frame overhead for functions with no root slots;
- [x] wide scalars live outside GC and survive every escape surface;
- [x] numeric nursery removed;
- [x] native `^E` Lambda returns remain typed and preserve the full `int64` domain;
- [x] stack-overflow and abnormal exits restore both watermarks;
- [x] JS and cross-language behavior unchanged at the phase boundary;
- [x] Lambda and test262 regression gates pass with no regressions.
