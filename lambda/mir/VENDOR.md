# Vendored MIR

This is a vendored copy of [MIR](https://github.com/vnmakarov/mir), the JIT
compiler backend Lambda uses for `transpile-mir.cpp` (MIR Direct). Its C
frontend is no longer reachable from the Lambda CLI; it is retained because
Jube hosts it as a guest runtime and the benchmark suite uses `c2m` as a
native reference.

| | |
|---|---|
| Upstream | https://github.com/vnmakarov/mir |
| Commit | `99c65079038f3ba9242ef646f308c266cfd7a8e5` (2024-08-29) |
| Local patches | `patches/mir-rotr.patch`, `patches/mir-alloca-branch-fix.patch`, `patches/mir-release-func-ir.patch`, `patches/mir-spilled-reg-bounds.patch`, `patches/mir-self-phi-copy.patch` |

**The patches under `patches/` are already applied to the source here.** They
are kept as the record of our delta versus upstream, so a future re-sync can
replay them onto a newer MIR. They are *not* applied at build time — doing that
would rewrite tracked files on every build.

`make verify-mir-patches` checks the invariant: it clones pristine upstream at
the commit above, applies every `patches/mir-*.patch`, and diffs the result
against this directory. Run it after editing anything here by hand.

## What is vendored

Only what `libmir.a` needs, plus licence and docs — about 2.7 MB:

- all top-level `mir*.c` / `mir*.h` (core, interpreter, and every target backend)
- `real-time.h` (included by `mir-gen.c` and `c2mir/c2mir.c`)
- `c2mir/` source and the `mirc` headers for all five architectures
- `GNUmakefile`, `check-threads.sh`, `.clang-format`, `LICENSE`, upstream docs

Dropped from upstream: `c-benchmarks` (45 MB), `c-tests`, `adt-tests`,
`mir-tests`, `mir-utils`, `mir2c`, `llvm2mir`, `.github`, the SVGs, and
`CMakeLists.txt` (it references directories the trim removes — build with
`GNUmakefile`, which is what the top-level Makefile does).

`check-threads.sh` is load-bearing, not documentation: `GNUmakefile` shells out
to it, and if it is missing the build silently drops `-DC2MIR_PARALLEL` and
`-lpthread`.

## Building

Driven from the top-level Makefile, which builds only the `libmir.a` target
(`mir.o` + `mir-gen.o` + `c2mir.o`); MIR's own executables are not vendored.

```
make build-mir     # build lambda/mir/libmir.a
make clean-mir     # remove build outputs, keep the source
```

`GITCOMMIT` is pinned to the upstream commit on the command line. Left alone,
`GNUmakefile` computes it with `git log -1`, which inside this repository
resolves to *Lambda's* HEAD and would rebuild all of MIR on every commit.

## Re-syncing to a newer upstream

1. Clone upstream at the new commit.
2. Apply each `patches/mir-*.patch`; fix up any that no longer apply.
3. Copy the vendored subset above over this directory.
4. Update the commit in this file and `MIR_UPSTREAM_COMMIT` in the Makefile.
5. `make verify-mir-patches && make build-mir && make test-lambda-baseline`.

## Local patches

### `mir-rotr.patch` — adds `MIR_ROTR`, a 64-bit rotate-right

Lambda's inline double-boxing path emits `ROTR bits, 63` (rotate left by one,
expressed as rotate right by 63) to move a double's sign bit into the tag
position without a call — see `emit_box_double` in
`lambda/runtime/transpile-mir.cpp` and its LambdaJS twin in
`lambda/js/js_mir_calls_boxing_types.cpp`. Touches `mir.h` (opcode), `mir.c`
(insn_desc), `mir-interp.c`, `mir-gen-x86_64.c`, and `mir-gen-aarch64.c`.

Rotate-*left* is deliberately absent: aarch64 has no rotate-left instruction,
and every rotate-left by n is a rotate-right by 64−n, so one opcode covers both
directions without a per-target lowering.

### `mir-alloca-branch-fix.patch` — `func_alloca_features` branch handling

Ends the "top alloca" window at any branch, not only at a label.

### `mir-release-func-ir.patch` — adds `MIR_release_func_ir`

Frees a generated function's IR — its insn lists (`insns`, `original_insns`)
and per-func register tables — while keeping the item, its thunk, and the
published machine code callable. MIR retains the IR after `MIR_gen` (the
generator machinizes a *duplicate* and restores the original) only for
re-generation, link-time inlining, the MIR interpreter, and lazy generation;
once a sealed Lambda module context has generated everything, none of those
apply and the IR is pure dead weight (~32 + 48·nops bytes per insn).

Touches `mir.h` (decl) and `mir.c`: the new API next to `remove_func_insns`, a
NULL guard in `func_regs_finish` so `MIR_finish` stays safe after an early
release, and `process_inlines` refusing empty bodies so a released function
degrades to a plain call instead of being "inlined" as nothing. Used by
`jit_release_generated_ir()` in `lambda/runtime/mir.c` after the final
link+gen of a module (skipped for MIR-interp scripts and lazy-pending
functions via the `func->machine_code != NULL` gate; `LAMBDA_MIR_KEEP_IR=1`
disables it).

### `mir-spilled-reg-bounds.patch` — bounds spilled-register operand rewrites

Adapted from [upstream PR #468](https://github.com/vnmakarov/mir/pull/468).
In `try_spilled_reg_mem`, register coalescing can make an instruction's output
and both inputs refer to the same spilled register (`dmul r, r, r` or
`dsub r, r, r`). The two-entry undo array then overflows when assertions are
disabled. On Linux x86-64 this triggers the stack protector in the `tune27`
nullable-float and `larceny_ray2` fixtures; other targets can avoid this
allocation pattern or fail to detect the overwrite.

The patch expands the array to three entries and checks its capacity before
changing an operand. If it fills, prior substitutions are restored and normal
register reload handling continues. The runtime check also protects
instructions with more operands, including variadic instructions.

Touches only `mir-gen.c`. Regression coverage comes from the existing
`tune27_float_literal_nullable` MIR emission and forced-GC fixtures, the
`Tune27FixturesAgreeOnEveryTier` parity test, and `larceny_ray2` in the Lambda
script corpus (D8.6.2–D8.6.3).

### `mir-self-phi-copy.patch` — keep self-only phis until unreachable cleanup

When GVN removes entry edges to an unreachable loop, a phi may be left with
only its own back-edge value. The ordinary two-operand-phi copy rule then
redirects the phi's users to that same phi and frees it, leaving dangling SSA
definitions. On the `navier_stokes` compiler input, a later branch reads the
freed `bb_insn`; ASAN reports a heap-use-after-free in `gvn_modify`.

The patch excludes a self-referential phi from copy substitution. Such a phi
has no distinct replacement definition; the existing unreachable-code cleanup
removes its loop. Other phi copies continue to optimize normally. The focused
standalone MIR replay and the Navier–Stokes release/ASAN runs cover this case.
The diagnostic and original ASAN trace are preserved under
`temp/tune32-phase2/` (D8.6.2–D8.6.3).

## Not vendored: the NULL-label workaround

An earlier local MIR tree carried ~50 lines in `mir.c` and `mir-gen.c` that
made MIR tolerate branch instructions with NULL or dangling label operands:
`remove_unused_and_enumerate_labels` stopped freeing removed labels,
`redirect_duplicated_labels` NULL-checked before dereferencing `->data`, and
`MIR_link` / `generate_func_code` deleted any instruction carrying a NULL label
operand.

It is deliberately excluded. Deleting a branch instruction silently rewrites
control flow, and the real defect was upstream of MIR: the JS→MIR transpiler
emitting branches to labels never inserted into the function (see the comment
at `lambda/js/js_mir_function_class_lowering.cpp:3845`). That is fixed at the
emission site, and LambdaJS additionally validates for NULL labels before
linking. Across the full Lambda + Input baseline the workaround's diagnostics
never fired.
