# Lambda GC Rooting Issue — `MIR_T_I64` Local Over-Rooting

**Status:** FIXED — blanket `MIR_T_I64` rooting removed; boxed values are now rooted by honest type tracking.
**Symptom test:** `NegativeScriptTest.RuntimeError_StackOverflow` (`test_lambda_errors_gtest.cpp:686`) previously hung instead of failing fast.
**Component:** MIR Direct JIT — local-variable GC rooting (`lambda/transpile-mir.cpp`).
**Related:** BUG-001 (heap-use-after-free in JIT locals; write-up in `~/Projects/Lambda_Bug.md`). The blanket rooting was BUG-001's fix.

---

## 1. Symptom

The baseline's `test_lambda_errors_gtest` never completes — it hangs on
`NegativeScriptTest.RuntimeError_StackOverflow`, which runs:

```lambda
// test/lambda/negative/runtime/stack_overflow.ls
fn f(n) => n + f(n + 1)
let x = f(0)
```

This is expected to recurse until the **C stack** overflows, at which point Lambda's
stack-guard raises a catchable error (`ExpectErrorWithoutCrash`). Instead it grows
unboundedly **on the heap** and hangs (the baseline harness kills it after ~180 s idle).

Every prior baseline in the project shows the same hang; it is pre-existing.

---

## 2. The rooting code (`lambda/transpile-mir.cpp`)

When the transpiler registers a local variable (`set_var`, `set_state_var`), it decides
whether to allocate a GC **root slot** for it so the collector can't sweep the value while
it's still live across an allocation:

```c
// ~line 372
static bool is_gc_root_type(TypeId type_id) {
    switch (type_id) {
    case LMD_TYPE_STRING: case LMD_TYPE_SYMBOL: case LMD_TYPE_DECIMAL:
    case LMD_TYPE_DTIME:  case LMD_TYPE_BINARY: case LMD_TYPE_INT64:
    case LMD_TYPE_UINT64:
    case LMD_TYPE_ARRAY:  case LMD_TYPE_ARRAY_NUM: case LMD_TYPE_MAP:
    case LMD_TYPE_ELEMENT: case LMD_TYPE_OBJECT: case LMD_TYPE_RANGE:
    case LMD_TYPE_FUNC:   case LMD_TYPE_TYPE:   case LMD_TYPE_PATH:
    case LMD_TYPE_VMAP:   case LMD_TYPE_ANY:
        return true;
    default:                 // INT, BOOL, NULL, FLOAT, ERROR, NUMBER, ...
        return false;
    }
}

static bool should_gc_root_var(MIR_type_t mir_type, TypeId type_id) {
    return mir_type == MIR_T_P || is_gc_root_type(type_id);
}

static bool should_gc_root_local_var(MIR_type_t mir_type, TypeId type_id) {
    return should_gc_root_var(mir_type, type_id);
}
```

Callers: `set_var` (~line 473) and `set_state_var` (~line 493) — each does
`if (should_gc_root_local_var(...)) entry.var.root_slot = create_gc_root_slot(mt, reg);`.

The old `mir_type == MIR_T_I64` clause (added in commit `0e85f99dc "bug fix"`, the BUG-001 fix)
makes a local get a root slot if its MIR register is a 64-bit integer register — i.e. for
**every** `MIR_T_I64` local, regardless of `type_id`.

---

## 3. Why the blanket causes the hang

Lambda's value model (see `CLAUDE.md` / `lambda/lambda-data.hpp`):

| Category | Examples | Representation | Needs GC rooting? |
|----------|----------|----------------|-------------------|
| Simple scalar | `null`, `bool`, `int` (int56) | **packed directly** in the I64 (value + TypeId tag) | **No** — never a pointer |
| Compound scalar | `int64`, `float`, `datetime`, `decimal`, `symbol`, `string`, `binary` | **tagged pointer** (heap / GC nursery) | **Yes** |
| Container | `array`, `map`, `element`, `range` | direct pointer | **Yes** |

A native `int` local lives in an `MIR_T_I64` register holding a *packed scalar* — not a
heap pointer. Rooting it is unnecessary, and it costs a **root-frame slot per local per
call frame**.

`fn f(n) => n + f(n+1)` has `n : int` (positive evidence from `n + 1`), so `n` is
`MIR_T_I64`. The blanket roots `n` on **every recursive frame**. The JIT root frame is
heap-backed, so each recursion appends a slot and the **heap** grows without bound — the
**C stack** is barely touched, so the stack-overflow guard never fires. Result: an
effectively unbounded heap loop = hang, instead of the expected fast stack overflow.

---

## 4. Why the "obvious" narrowings don't work

Both candidate fixes were built and tested on the current tree (cd.ls inference fix
present, blanket the only variable):

| Variant | `should_gc_root_local_var` body | StackOverflow | deltablue / deltablue2 |
|---------|--------------------------------|---------------|------------------------|
| **Status quo (blanket)** | `mir_type == MIR_T_I64 \|\| should_gc_root_var(...)` | **hangs** | PASS, fast (~0.6–1.8 s ASan) |
| **Full removal** | `should_gc_root_var(...)` | fails fast (~0.8 s) ✅ | **hang** (40 s, GC corruption) ❌ |
| **Option A** (exclude only packed scalars) | root all I64 **except** `INT`/`BOOL`/`NULL` | fails fast (~0.8 s) ✅ | **hang** (30 s) ❌ |

`json`/`json2`/`havlak`/`havlak2`/`cd`/`cd2` stay correct in all three.

**Full removal** un-roots `int64` too — `is_gc_root_type(LMD_TYPE_INT64)` is `false`, yet a
boxed `int64` is a nursery pointer. That alone can sweep live data.

**Option A** keeps rooting `int64` but still excludes packed scalars, and deltablue
**still hangs**. That was the useful clue: the problem was not simply "int64 needs a root".
Some DeltaBlue values were still travelling through `MIR_T_I64` locals with non-rooted
static types (`NULL` placeholders and stale call result types), even though they later held
boxed heap Items. Excluding packed scalars exposed that latent type-tracking bug.

So at the rooting site the old tuple `(mir_type, type_id)` was only safe if `type_id`
honestly described the register's runtime representation:

- StackOverflow's `n` — native packed `int`, `MIR_T_I64`, no heap pointer → must NOT be rooted.
- DeltaBlue placeholders/call results — boxed heap Items in `MIR_T_I64`, statically
  recorded as non-rootable → MUST be tracked as `ANY` or a precise heap type and rooted.

The blanket "root everything I64" kept DeltaBlue correct by accident, but it also rooted
genuine native ints and caused the StackOverflow hang.

---

## 5. Root cause found

The root cause was not that all `MIR_T_I64` values should or should not be rooted. The
missing distinction was whether the `I64` register held a **packed scalar** or a **boxed
Item**.

Three concrete type-tracking holes were found:

1. `LMD_TYPE_INT64` was missing from `is_gc_root_type()`, even though `int64` is a boxed
   heap scalar. Removing the blanket therefore unrooted live boxed `int64` values.
2. Unannotated mutable null placeholders such as `var inp = null`, `var out = null`, and
   `var overridden = null` were registered as `LMD_TYPE_NULL`. These variables often receive
   heap values later (`inp = c.v1`, `out = c_get_output(c)`, `overridden = cc`). The late
   assignment path could create the root slot only after the value already existed, and
   growing the root frame can allocate before that heap value is protected.
3. The first attempt to fix (2) checked `!declare->type`, but the AST already stores the
   inferred `NULL` type there. The correct signal for "source did not write a type
   annotation" is `AstNamedNode::entry->has_type_annotation`.

The DeltaBlue failure was the visible symptom: some object/map locals were unrooted after
starting as `null`, so later GC activity corrupted the constraint graph and `fn_map_set`
started seeing a bool/null-like value instead of a map target.

---

## 6. Fix landed

The fix keeps the useful part of the old BUG-001 protection while removing the per-call
native-int overhead:

1. `is_gc_root_type()` now includes `LMD_TYPE_INT64`, because boxed `int64` values are heap
   pointers and must be protected.
2. `should_gc_root_local_var()` now delegates to `should_gc_root_var()` instead of rooting
   every `MIR_T_I64`. Packed `int`/`bool`/`null` locals stay unrooted.
3. Unannotated `var ... = null` declarations are tracked as `LMD_TYPE_ANY` from declaration
   time:

   ```c
   if (let_node->node_type == AST_NODE_VAR_STAM &&
       (!asn->entry || !asn->entry->has_type_annotation) &&
       expr_tid == LMD_TYPE_NULL) {
       var_tid = LMD_TYPE_ANY;
   }
   ```

   This creates the GC root slot up front for mutable placeholders that may later hold
   boxed heap Items, while preserving explicit scalar annotations such as `var x: int`.
4. Call result post-processing now uses `get_effective_type()` instead of the raw AST call
   type. This catches stale call types where one return path is `null` but another returns a
   heap value, e.g. `vec_remove_first()`.

The important property: local rooting is now decided by representation truth (`MIR_T_P` or
rootable `TypeId`), not by the broad fact that a value lives in an `MIR_T_I64` register.

---

## 7. Validation

Validated after the fix:

1. `NegativeScriptTest.RuntimeError_StackOverflow` passes in 124 ms.
2. Full `./test/test_lambda_errors_gtest.exe` passes: 61/61 tests.
3. `./lambda.exe run test/benchmark/awfy/deltablue.ls --no-log` passes.
4. `./lambda.exe run test/benchmark/awfy/deltablue2.ls --no-log` passes.
5. `git diff --check` passes.

---

## 8. Final state

The blanket `mir_type == MIR_T_I64` root is removed. StackOverflow now reaches the C stack
guard quickly because native int frames no longer allocate root slots. DeltaBlue remains
correct because the locals that can hold heap values are typed/rooted as boxed Items from
the start, and boxed `int64` is explicitly rootable.
