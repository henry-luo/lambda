# MIR Alloca Inlining Bug

**Date discovered**: 2026-04-15  
**Severity**: Critical — SIGSEGV crash in JIT-compiled code  
**File**: `mac-deps/mir/mir.c` function `func_alloca_features()` (line ~3880)  
**Impact**: 220 test262 tests crashing; baseline 21,581 → 21,799 after fix

---

## 1. Symptom

Certain JavaScript programs crash with SIGSEGV at address `0x10` inside JIT-compiled code. The crash manifests whenever both conditions are met:

1. A user-defined function with an `alloca` (e.g. args array) is called — any function with `js_call_function` that builds an arguments array.
2. A `throw new Constructor(args)` expression exists in the same module, behind a conditional branch.

Minimal reproducer (`temp/_debug_minimal12.js`):

```javascript
function Test262Error(message) { this.message = message || ""; }
function fn(arg) { return arg + 1; }
function call(f, a, b) { return f(a) * b; }

var r = call(fn, 2, 2);
if (r !== 6) {
  throw new Test262Error('x');
}
```

This exits 0 (the throw is dead code) yet crashes with signal 11 every time.

---

## 2. Root Cause

### Background: MIR's Top-Alloca Optimization

MIR's inliner (`MIR_simplify_func` in `mir.c`) hoists alloca from inlined callees into the caller. It identifies the callee's "top alloca" — the first alloca in the function's straight-line prefix — and replaces it with an offset from the caller's consolidated alloca base. This avoids runtime stack pointer manipulation for each inlined call.

The function `func_alloca_features()` scans a function's instruction list to find the "top alloca":

```c
for (insn = DLIST_HEAD(MIR_insn_t, func->insns); insn != NULL;
     insn = DLIST_NEXT(MIR_insn_t, insn)) {
    if (insn->code == MIR_LABEL && set_top_alloca_p)
        set_top_alloca_p = FALSE;     // ← only labels stop the scan
    ...
}
```

It stops considering allocas as "top" once it encounters `MIR_LABEL`. The rationale: a label means control flow can arrive from elsewhere, so the alloca is no longer guaranteed to execute on function entry.

### The Bug

`func_alloca_features()` **does not check for branch instructions** (`BF`, `BT`, `JMP`, etc.). A branch before an alloca means that alloca is conditional — it may or may not execute depending on runtime state. But the scanner does not terminate `set_top_alloca_p` on branches, so it classifies the conditional alloca as a "top alloca".

### The Trigger in Generated JS Code

The transpiler (`transpile_js_mir.cpp`) generates a `js_main` function like:

```
js_main:  func  i64, p:ctx
    ...
    call  js_ne_raw_p, js_ne_raw, js_ne_raw_50, ...  ; r !== 6
    bf    L7, js_ne_raw_50            ; ← BRANCH: skip throw if equal
    alloca  ctor_names_52, 8          ; ← conditional alloca (for new Test262Error)
    alloca  ctor_lens_53, 4
    alloca  args_55, 8
    ...
L7:
L8:
    ret  result_32
```

`func_alloca_features()` scans past the `bf` instruction without clearing `set_top_alloca_p`, and classifies `ctor_names_52` as `js_main`'s top alloca.

### The Inlining Failure

When MIR inlines `_js_call_105` (the 3-arg higher-order function) into `js_main`:

1. The callee's alloca (`args_25` for the arguments array) needs a base pointer.
2. MIR rewrites the inlined alloca to `ADD new_reg, func_top_alloca_reg, offset`.
3. `func_top_alloca_reg` is the register from the **conditional** alloca (`ctor_names_52`).
4. If the branch (`bf L7`) is taken (the `throw` path is skipped), the conditional alloca never executes, and `func_top_alloca_reg` remains **uninitialized** (zero).

### The Crash

The inlined code does:

```asm
add  x21, x26, #0x10     ; x26 = func_top_alloca_reg = 0 (never initialized)
                          ; x21 = 0x10
str  x25, [x21, xzr, lsl #3]  ; store to address 0x10 → SIGSEGV
```

This crashes at address `0x10` — null pointer (0) plus the alloca offset (16).

---

## 3. The Fix

**One-line change** in `func_alloca_features()`:

```diff
  for (insn = DLIST_HEAD(MIR_insn_t, func->insns); insn != NULL;
       insn = DLIST_NEXT(MIR_insn_t, insn)) {
-    if (insn->code == MIR_LABEL && set_top_alloca_p)
+    if ((insn->code == MIR_LABEL || MIR_any_branch_code_p(insn->code)) && set_top_alloca_p)
       set_top_alloca_p = FALSE;
```

`MIR_any_branch_code_p()` covers: `JMP`, `BT`, `BF`, `BEQ`, `BNE`, `BLT`, `BLTS`, `BGT`, `BGTS`, `BLE`, `BLES`, `BGE`, `BGES`, `FBEQ`, `FBNE`, `FBLT`, `FBGT`, `FBLE`, `FBGE`, `DBEQ`, `DBNE`, `DBLT`, `DBGT`, `DBLE`, `DBGE`, `LDBEQ`, `LDBNE`, `LDBLT`, `LDBGT`, `LDBLE`, `LDBGE`, `JMPI`, `SWITCH`.

After the fix, any alloca appearing after a branch is classified as **non-top** (it has `non_top_alloca_p = TRUE`). The inliner then wraps the inlined code in `BSTART`/`BEND` instead of trying to hoist the alloca, which is safe regardless of control flow.

### Build Steps

The fix is in the MIR subproject source. After editing `mac-deps/mir/mir.c`, rebuild the static library and then relink `lambda.exe`:

```bash
cd mac-deps/mir && make          # rebuilds libmir.a
cd ../.. && make build           # relinks lambda.exe against new libmir.a
```

Note: `make build` alone won't recompile `libmir.a` because it's a prebuilt dependency.

---

## 4. Additional Fix: `--opt-level` in JS Command

While debugging, we discovered `./lambda.exe js --opt-level=0 file.js` was silently ignored — the `js` command handler in `main.cpp` didn't parse `--opt-level`. Only the `js-test-batch` handler did. Added the flag parsing:

```cpp
// lambda/main.cpp, inside the "js" command argument loop:
} else if (strncmp(argv[i], "--opt-level=", 12) == 0) {
    int level = atoi(argv[i] + 12);
    if (level >= 0 && level <= 3) g_js_mir_optimize_level = (unsigned int)level;
}
```

---

## 5. Impact Summary

| Metric | Before | After |
|--------|--------|-------|
| test262 baseline | 21,581 | 21,799 |
| Improvements | — | +220 tests |
| Regressions | — | 1 (unrelated Promise test) |
| Quarantine entries | 22 | 20 |
| Lambda baseline | 577/578 | 577/578 (unchanged) |

---

## 6. Debugging Timeline

The crash was difficult to diagnose because:

1. **No inlining theory initially held**: testing with `--opt-level=0` appeared to confirm the crash happened without inlining — but the `js` handler wasn't parsing the flag, so it was always running at level 2.
2. **Pattern appeared semantic**: the crash required both a 3-arg function call AND `throw new Constructor()`, suggesting a transpiler code-gen bug.
3. **Minimization was misleading**: removing either the function call or the throw made the crash disappear, reinforcing the semantic hypothesis.

The breakthrough came from ARM64 disassembly showing:
- `add x21, x26, #0x10` — alloca offset computation using x26 as base
- `x26 = 0` — the "top alloca" register was never written
- Tracing back, the alloca was behind a `bf` (branch-if-false) instruction
- The MIR inliner hoisted this conditional alloca as if it were unconditional

### Variant Testing Matrix

| Variant | 3-arg call | throw new | Crash? |
|---------|-----------|-----------|--------|
| A: no throw | ✗ | ✗ | No |
| B: 3-arg, no throw | ✓ | ✗ | No |
| C: throw new, no 3-arg | ✗ | ✓ | No |
| D: both (dead branch) | ✓ | ✓ | **Yes** |
| E: throw string (no `new`) | ✓ | ✗ | No |
| F: 2-arg + throw new | ✗ | ✓ | No |
| G: both (live branch) | ✓ | ✓ | **Yes** |
| U: 3-arg simple + throw new | ✓ | ✓ | No |

The key insight from variant U: `function f(a,b,c) { return a+b+c; }` doesn't have an `alloca` (no indirect calls), so there's nothing for the inliner to hoist. The crash requires the callee to have an `alloca` — which higher-order functions do (for building the arguments array via `js_call_function`).
