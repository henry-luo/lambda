# JavaScript Transpiler v6: Performance Enhancement Proposal

## 1. Executive Summary

Lambda's built-in JavaScript engine (LambdaJS) is substantially slower than both Lambda MIR Direct and Node.js V8 on compute-intensive benchmarks. Across the benchmark suites, LambdaJS is typically **10–100x slower** than Lambda MIR and **5–50x slower** than Node.js on numeric and loop-heavy workloads, while on some micro-benchmarks it is paradoxically fast (near-zero times) because the workload is trivially small.

This proposal targets the **structural performance gap** by applying lessons learned from the Lambda MIR transpiler's Phase 3 and Phase 4 optimization work ([Lambda_Transpile_Restructure3.md](Lambda_Transpile_Restructure3.md), [Lambda_Transpile_Restructure4.md](Lambda_Transpile_Restructure4.md)). The core insight is that the JS MIR transpiler currently operates as a **"fully boxed interpreter with JIT dispatch"** — MIR compiles the control flow, but every arithmetic operation, comparison, and property access dispatches through a C runtime function call with dynamic type checks. The Lambda MIR transpiler, by contrast, is a **type-specializing JIT** that emits native CPU instructions when types are known or inferred.

### Architecture Position

```
v1–v2: JS AST → C codegen → C2MIR → native          (removed)
v3:    Runtime alignment, GC, DOM, selectors          (done)
v4:    JS AST → direct MIR IR → native                (done, current)
v5:    Language coverage + typed arrays + closures     (done/in-progress)
v6:    Type inference + native code generation         (this proposal)
         Phase 1: Type-tracked variables               ✅ done
         Phase 2: Native arithmetic emission           ✅ done  (2–43x speedup)
         Phase 3: For-loop specialization              ✅ done  (11–52x total)
         Phase 4: Function call optimization           ⬜ not started
         Phase 5: Property access optimization         ⬜ not started
```

### Goal

Close the performance gap between LambdaJS and Lambda MIR Direct on compute-intensive benchmarks from the current **10–100x** to **2–5x**, making LambdaJS competitive with QuickJS and approaching Node.js performance on numeric-heavy code.

---

## 2. Performance Analysis

### 2.1 Benchmark Evidence

From [Overall_Result2.md](../test/benchmark/Overall_Result2.md), selected benchmarks where LambdaJS has results:

| Benchmark | MIR (ms) | LambdaJS (ms) | Node.js (ms) | JS/MIR | JS/Node | Bottleneck |
|-----------|-------:|----------:|--------:|-------:|--------:|------------|
| **R7RS fib** | 2.0 | 10.8 | 1.6 | 5.4x | 6.8x | Recursive calls, boxing round-trip |
| **R7RS fibfp** | 3.8 | 10.7 | 1.6 | 2.8x | 6.7x | Float boxing via `push_d` per add |
| **R7RS tak** | 0.15 | 0.85 | 0.76 | 5.7x | 1.1x | Recursive call overhead |
| **R7RS cpstak** | 0.31 | 1.7 | 0.91 | 5.5x | 1.9x | Closure + call overhead |
| **R7RS sum** | 0.27 | 16.1 | 1.1 | 59.6x | 14.6x | `js_add()` call per iteration |
| **R7RS sumfp** | 0.066 | 1.6 | 0.82 | 24.2x | 2.0x | Float boxing per add |
| **R7RS nqueens** | 6.9 | 58.5 | 1.7 | 8.5x | 34.4x | Array access + backtracking |
| **R7RS fft** | 1.5 | 2.9 | 1.5 | 1.9x | 1.9x | Float arithmetic (smaller gap) |
| **R7RS mbrot** | 0.58 | 15.9 | 1.6 | 27.4x | 9.9x | Tight float loop |
| **R7RS ack** | 10.3 | 61.1 | 12.4 | 5.9x | 4.9x | Deep recursion |
| **AWFY mandelbrot** | 33.6 | 794 | 31.5 | 23.6x | 25.2x | Float arithmetic in tight loop |
| **Beng mandelbrot** | 23.0 | 119 | 4.2 | 5.2x | 28.3x | Same |
| **Kostya brainfuck** | 168 | 713 | 47.5 | 4.2x | 15.0x | Interpreter loop, array mutation |
| **Kostya matmul** | 331 | 1,390 | 15.9 | 4.2x | 87.4x | Dense float multiply-accumulate |
| **Kostya primes** | 7.5 | 54.4 | 4.5 | 7.3x | 12.1x | Integer sieve loop |

**Key observation**: The JS/MIR ratio ranges from 1.9x (fft) to 59.6x (sum), with a typical range of 5–25x on numeric code. The benchmarks where LambdaJS is closest to MIR (fft at 1.9x) are those with the fewest loop iterations and function calls — confirming that per-operation overhead is the dominant factor.

### 2.2 Root Cause: Fully Boxed Execution Model

The JS MIR transpiler (`transpile_js_mir.cpp`, 3,884 lines) treats every JavaScript value as a boxed `Item` (64-bit tagged pointer). Every operation goes through a C runtime function call:

```
JavaScript:   a + b
MIR emitted:  left  = jm_transpile_box_item(a)    // ensure boxed Item
              right = jm_transpile_box_item(b)     // ensure boxed Item
              result = call js_add(left, right)     // FUNCTION CALL
```

The `js_add()` runtime function then:
1. Calls `get_type_id(a)` — extract tag bits, branch on type
2. Calls `get_type_id(b)` — extract tag bits, branch on type
3. Checks if either is a string (for concatenation semantics)
4. Calls `js_to_number(a)` — another type switch + potential conversion
5. Calls `js_to_number(b)` — another type switch + potential conversion
6. Calls `fn_add(a_num, b_num)` — Lambda's boxed add, which does yet another type dispatch

**A simple `a + b` on two integers costs ~6 function calls and ~12 type-tag extractions** instead of a single `MIR_ADD` instruction (1 cycle on Apple Silicon).

Compare with the Lambda MIR transpiler, which emits:
```
Lambda:    a + b   (where a, b are known int)
MIR:       MIR_ADD(a_native, b_native)    // single native instruction
```

### 2.3 Comparison: JS vs Lambda MIR Transpiler Capabilities

| Capability | Lambda MIR (`transpile-mir.cpp`) | JS MIR (`transpile_js_mir.cpp`) |
|-----------|--------------------------------|--------------------------------|
| **Variable type tracking** | `MirVarEntry.type_id` + `mir_type` per variable | `JsMirVarEntry` — no type info, all `MIR_T_I64` |
| **Native arithmetic** | `MIR_ADD`, `MIR_SUB`, `MIR_MUL`, `MIR_DADD` etc. when types known | Always calls `js_add()`, `js_subtract()` etc. |
| **Inline boxing/unboxing** | `emit_box_int()`, `emit_unbox()` — inline MIR sequences | Only `jm_box_int_const()` for literals; no unboxing |
| **Parameter type inference** | Phase 2: scans body to infer param types from usage | None |
| **Dual native+boxed functions** | Phase 4: `_n` native + `_b` boxed wrapper | Single boxed version only |
| **Native return types** | Phase 4: infers and uses native return type | Always returns boxed Item |
| **Tail-call optimization** | Full TCO with loop transformation | None |
| **Inline array access** | Phase 4: direct memory load for known-type arrays | Always calls `js_property_access()` |
| **Boolean specialization** | Phase 4: inline bool comparisons, native bool ops | Always calls `js_is_truthy()` |
| **Speculative fast paths** | Phase 4: INT fast path for untyped arithmetic | None |
| **Loop-invariant hoisting** | Phase 4: hoists array metadata above loops | None |

---

## 3. Proposed Optimizations

The optimizations are structured in five phases, ordered by impact and dependency. Each phase builds on the previous, and the expected improvements compound.

### Phase 1: Type-Tracked Variables and Literal Propagation

**Target**: All benchmarks. **Expected impact**: 3–5x improvement on arithmetic-heavy code.

#### 3.1.1 Problem

The `JsMirVarEntry` struct has no type information:

```cpp
// Current (js_transpiler.hpp)
struct JsMirVarEntry {
    MIR_reg_t reg;
    bool from_env;
    int env_slot;
    MIR_reg_t env_reg;
};
```

All variables are `MIR_T_I64` (boxed Item). A loop counter `let i = 0; i < n; i++` stays boxed throughout — each increment goes through `js_add(boxed_i, boxed_1)`.

#### 3.1.2 Solution: Add Type Tracking

Extend `JsMirVarEntry` to track type information, following the Lambda `MirVarEntry` pattern:

```cpp
struct JsMirVarEntry {
    MIR_reg_t reg;
    MIR_type_t mir_type;     // MIR_T_I64 for int/boxed, MIR_T_D for float
    TypeId type_id;          // LMD_TYPE_INT, LMD_TYPE_FLOAT, LMD_TYPE_ANY, etc.
    TypeId elem_type;        // element type for typed arrays
    bool from_env;
    int env_slot;
    MIR_reg_t env_reg;
};
```

#### 3.1.3 Solution: Narrow Types from Initializers

When a variable is declared with a known-type initializer, propagate the type:

```javascript
let x = 0;           // → type_id = LMD_TYPE_INT,   mir_type = MIR_T_I64 (native int)
let y = 3.14;         // → type_id = LMD_TYPE_FLOAT, mir_type = MIR_T_D   (native double)
let s = "hello";      // → type_id = LMD_TYPE_STRING, stays boxed
let b = true;         // → type_id = LMD_TYPE_BOOL,  mir_type = MIR_T_I64 (native 0/1)
let z = x + 1;        // → type_id = LMD_TYPE_INT (propagated from x)
```

**Implementation**: In `jm_transpile_variable_declaration()`, after transpiling the initializer, check the expression's effective type. If the RHS is a numeric literal, identifier with known type, or arithmetic on known-type operands, set the variable's type accordingly. Store the native (unboxed) value directly in the register.

#### 3.1.4 Solution: Track Effective Types Through Expressions

Add a `jm_get_effective_type()` function (analogous to Lambda's `get_effective_type()`) that returns the `TypeId` of an expression based on:

1. **Literals**: `42` → INT, `3.14` → FLOAT, `"str"` → STRING, `true/false` → BOOL
2. **Identifiers**: look up `JsMirVarEntry.type_id`
3. **Binary arithmetic** (`+`, `-`, `*`, `/`, `%`): if both operands are INT → INT; if either is FLOAT → FLOAT; if either is STRING and op is `+` → STRING
4. **Comparison** (`<`, `>`, `<=`, `>=`, `==`, `===`): always BOOL
5. **Unary** (`!`) → BOOL; (`-`, `+`) → propagate operand type; (`typeof`) → STRING
6. **Call expressions**: return ANY (unknown until we have function return type inference)
7. **Property access**: return ANY (dynamic dispatch)

This allows chained expressions like `x * y + z` to maintain type information throughout, enabling native code emission for the entire expression tree.

#### 3.1.5 Complexity and Risk

| Step | Effort | Risk |
|------|--------|------|
| Extend `JsMirVarEntry` with type fields | Low | Low |
| Type narrowing from initializers | Medium | Low — literals have obvious types |
| `jm_get_effective_type()` implementation | Medium | Medium — must handle all expression node types |
| Update `jm_transpile_assignment` for type changes | Medium | Medium — reassignment may change type |
| **Total** | **~3 days** | **Medium** |

---

### Phase 2: Native Arithmetic Emission

**Target**: sum, sumfp, mandelbrot, matmul, fib, mbrot, primes. **Expected impact**: 5–20x on tight numeric loops.

**Dependency**: Phase 1 (type tracking).

#### 3.2.1 Problem

Every `a + b` calls `js_add()` — a C function with 6 cascaded type checks. For a tight loop like:

```javascript
let sum = 0;
for (let i = 0; i < 10000000; i++) {
    sum += i;
}
```

This means 10M function calls to `js_add`, each with ~20 instructions of dispatch overhead. Lambda MIR emits a single `MIR_ADD` per iteration.

#### 3.2.2 Solution: Emit Native MIR Instructions When Types Are Known

When `jm_get_effective_type()` returns a concrete numeric type for both operands, emit native MIR arithmetic instead of a runtime function call:

```cpp
static MIR_reg_t jm_transpile_binary(JsMirTranspiler* mt, JsBinaryNode* bin) {
    TypeId left_tid  = jm_get_effective_type(mt, bin->left);
    TypeId right_tid = jm_get_effective_type(mt, bin->right);

    // FAST PATH: both operands are native int
    if (left_tid == LMD_TYPE_INT && right_tid == LMD_TYPE_INT) {
        MIR_reg_t left  = jm_transpile_expr_native(mt, bin->left, LMD_TYPE_INT);
        MIR_reg_t right = jm_transpile_expr_native(mt, bin->right, LMD_TYPE_INT);
        MIR_reg_t result = new_reg(mt, "arith", MIR_T_I64);
        switch (bin->op) {
            case JS_OP_ADD: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, ...)); break;
            case JS_OP_SUB: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_SUB, ...)); break;
            case JS_OP_MUL: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MUL, ...)); break;
            case JS_OP_DIV: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DIV, ...)); break;
            case JS_OP_MOD: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MOD, ...)); break;
            // ...
        }
        return result;  // native int, NOT boxed
    }

    // FAST PATH: float arithmetic
    if ((left_tid == LMD_TYPE_FLOAT || left_tid == LMD_TYPE_INT) &&
        (right_tid == LMD_TYPE_FLOAT || right_tid == LMD_TYPE_INT)) {
        // Widen ints to double, emit MIR_DADD/MIR_DSUB/MIR_DMUL/MIR_DDIV
        MIR_reg_t left  = jm_transpile_expr_as_float(mt, bin->left);
        MIR_reg_t right = jm_transpile_expr_as_float(mt, bin->right);
        MIR_reg_t result = new_reg(mt, "farith", MIR_T_D);
        switch (bin->op) {
            case JS_OP_ADD: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DADD, ...)); break;
            case JS_OP_SUB: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DSUB, ...)); break;
            case JS_OP_MUL: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DMUL, ...)); break;
            case JS_OP_DIV: emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DDIV, ...)); break;
            // ...
        }
        return result;  // native double, NOT boxed
    }

    // SLOW PATH: unknown types — call runtime function (existing code)
    MIR_reg_t left  = jm_transpile_box_item(mt, bin->left);
    MIR_reg_t right = jm_transpile_box_item(mt, bin->right);
    return jm_call_2(mt, "js_add", ...);
}
```

#### 3.2.3 Native Comparisons

When operand types are known, emit native comparison instructions instead of calling `js_less_than()` etc.:

```cpp
// INT < INT → MIR_LTS (signed less-than)
// FLOAT < FLOAT → MIR_DLT (double less-than)
// INT == INT → MIR_EQ
// FLOAT == FLOAT → MIR_DEQ
```

For `===` (strict equality) on known types, this is safe because same-type comparisons in JS follow the same semantics as native comparisons (except NaN, handled by IEEE 754 which MIR_DEQ already implements).

#### 3.2.4 Native Increment/Decrement

Currently `i++` boxes 1 and calls `js_add()`. With type tracking:

```cpp
// When i is known INT:
//   i++  →  MIR_ADD(i_reg, 1)  (single instruction)
//   i--  →  MIR_SUB(i_reg, 1)
```

Note: the `for` loop counter is already partially optimized (uses `MIR_ADD` for the update step at line ~3074 of `transpile_js_mir.cpp`), but this doesn't extend to the loop body or to `while` loops.

#### 3.2.5 Boxing Boundary: When to Box/Unbox

The critical challenge is managing the boundary between native and boxed representations. The rule:

1. **Variables with known types** hold native values (unboxed)
2. **When passing to a runtime function** that expects `Item`, box the native value first
3. **When receiving from a runtime function** that returns `Item`, unbox if the target variable has a known type
4. **When assigning between same-type native variables**, no conversion needed
5. **When a variable's type changes** (e.g., `x = 0; ... x = "hello"`), transition to boxed (type becomes ANY)

Implement `jm_transpile_box_item()` and `jm_transpile_unbox()` as MIR inline sequences (not function calls), following Lambda's `emit_box_int()`, `emit_box_float()`, `emit_unbox()` patterns:

```cpp
// Inline int boxing: val → (INT_TAG << 56) | (val & MASK56)
static MIR_reg_t jm_emit_box_int(JsMirTranspiler* mt, MIR_reg_t val) {
    MIR_reg_t result = new_reg(mt, "boxed", MIR_T_I64);
    // AND val with 56-bit mask, OR with INT tag
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_AND, result_op, val_op, mask_op));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_OR,  result_op, result_op, tag_op));
    return result;
}

// Inline int unboxing: item → sign-extend lower 56 bits
static MIR_reg_t jm_emit_unbox_int(JsMirTranspiler* mt, MIR_reg_t item) {
    MIR_reg_t result = new_reg(mt, "unboxed", MIR_T_I64);
    // SHL by 8, then arithmetic SHR by 8 for sign extension
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LSH, result_op, item_op, imm8_op));
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_RSH, result_op, result_op, imm8_op));
    return result;
}
```

#### 3.2.6 Complexity and Risk

| Step | Effort | Risk |
|------|--------|------|
| Native int arithmetic (ADD/SUB/MUL/DIV/MOD) | Medium | Low — straightforward MIR emission |
| Native float arithmetic (DADD/DSUB/DMUL/DDIV) | Medium | Low |
| Native comparisons (LT/GT/LE/GE/EQ/NE) | Medium | Low |
| Inline boxing/unboxing helpers | Medium | Medium — must match Lambda's tag format exactly |
| Boxing boundary management | High | High — must correctly transition at all boundaries |
| **Total** | **~5 days** | **Medium-High** |

---

### Phase 3: For-Loop Specialization

**Target**: sum, primes, mandelbrot, matmul, sieve, array1. **Expected impact**: 2–3x additional on loop-heavy code (compounds with Phase 2).

**Dependency**: Phase 1 + Phase 2.

#### 3.3.1 Problem

The standard `for` loop pattern in JS benchmarks:

```javascript
for (let i = 0; i < n; i++) {
    sum += arr[i];
}
```

Currently generates:
1. `i = 0` — boxed literal
2. `js_less_than(i, n)` — function call for comparison
3. `js_is_truthy(result)` — function call for boolean coercion
4. Body: `js_property_access(arr, i)` — function call for array access
5. `js_add(sum, element)` — function call for addition
6. `i = js_add(i, 1)` — function call for increment

Six function calls per iteration, most of which are unnecessary when types are known.

#### 3.3.2 Solution: Recognize and Specialize Standard For-Loop Patterns

When the transpiler detects a C-style `for` loop with:
- Integer initializer (`let i = 0` or `let i = start`)
- Comparison condition (`i < n`, `i <= n`, `i >= 0`)
- Simple increment/decrement (`i++`, `i--`, `i += step`)

Emit a fully native loop:

```
    i_reg = 0  (native int)
    n_reg = transpile_as_int(n)
loop_top:
    MIR_BGE(i_reg, n_reg, loop_end)      // native comparison, no function call
    // ... loop body with native i_reg ...
    MIR_ADD(i_reg, i_reg, 1)              // native increment
    MIR_JMP(loop_top)
loop_end:
```

This eliminates all per-iteration overhead from the loop mechanics. The loop body can then also benefit from Phase 2's native arithmetic if the body operations have known types.

#### 3.3.3 TypedArray Specialization Inside Loops

When iterating over a typed array (`Int32Array`, `Float64Array`), specialize the element access:

```javascript
for (let i = 0; i < arr.length; i++) {
    sum += arr[i];
}
```

If `arr` is known to be a `Float64Array`:

```
// Hoist: load length and data pointer before loop
len_reg = *(arr_ptr + offsetof(JsTypedArray, length))
data_reg = *(arr_ptr + offsetof(JsTypedArray, data))

loop:
    // Direct memory load — NO function call
    val_reg = *(data_reg + i_reg * 8)    // MIR_LDMOV (double load)
    // Native float accumulation
    MIR_DADD(sum_reg, sum_reg, val_reg)
```

This is the same **loop-invariant code motion** technique from Lambda Phase 4 ([Lambda_Transpile_Restructure4.md §3.4](Lambda_Transpile_Restructure4.md)): hoist the array metadata (length, data pointer) above the loop, then use direct memory access inside the loop body.

#### 3.3.4 Complexity and Risk

| Step | Effort | Risk |
|------|--------|------|
| C-style for-loop detection | Medium | Low — pattern is syntactically recognizable |
| Native loop emission | Medium | Low — standard MIR loop construct |
| TypedArray direct access | High | Medium — pointer arithmetic must be correct |
| Loop-invariant hoisting | Medium | Medium — requires proving invariance |
| **Total** | **~4 days** | **Medium** |

---

### Phase 4: Function Call Optimization

**Target**: fib, tak, cpstak, ack, nqueens, triangl, brainfuck. **Expected impact**: 2–4x on call-heavy recursive benchmarks.

**Dependency**: Phase 1.

#### 3.4.1 Problem: Boxing Round-Trip at Every Call

Every JS function call performs a boxing round-trip identical to the problem solved in Lambda Phase 4 ([Lambda_Transpile_Restructure4.md §1](Lambda_Transpile_Restructure4.md)):

```
Caller:  native_int_x → jm_box_int(x) → Item_x        ← box arg
Callee:  Item_x → lives as boxed Item throughout        ← never unboxed!
Callee:  result → boxed Item                             ← already boxed
Caller:  Item → used as boxed Item                       ← no unboxing either
```

The JS transpiler never unboxes parameters at function entry because there's no type information. Every variable inside the function stays boxed, and every operation uses runtime dispatch.

#### 3.4.2 Solution A: Parameter Type Inference from Usage (Recommended)

Scan the function body to infer parameter types from how they're used, following Lambda's Phase 2 inference from [Lambda_Transpile_Restructure3.md §P2](Lambda_Transpile_Restructure3.md):

```javascript
function fib(n) {              // n used in: n <= 1, n - 1, n - 2
    if (n <= 1) return n;      //   → comparison with int literal
    return fib(n - 1) + fib(n - 2);  // → arithmetic with int literals
}                              // Inference: n is INT
```

**Inference rules**:
1. Parameter used in arithmetic (`+`, `-`, `*`, `/`, `%`) with an INT → likely INT
2. Parameter used in arithmetic with a FLOAT → likely FLOAT
3. Parameter used as array index (`arr[param]`) → likely INT
4. Parameter compared with numeric literal → likely INT/FLOAT
5. Parameter passed to a function whose parameter type is known → propagate
6. Parameter used in string operation (`.length`, `.indexOf`) → STRING
7. Parameter used in conflicting ways (arithmetic AND string concat) → ANY (no inference)

**Implementation**: A pre-pass over the function body AST before MIR emission:

```cpp
typedef struct JsParamTypeInfo {
    TypeId inferred_type;        // LMD_TYPE_INT, LMD_TYPE_FLOAT, or LMD_TYPE_ANY
    int int_evidence;            // count of int-suggesting usages
    int float_evidence;          // count of float-suggesting usages
    int string_evidence;         // count of string-suggesting usages
} JsParamTypeInfo;

static void jm_infer_param_types(JsMirTranspiler* mt, JsFunctionNode* fn) {
    // Walk function body, accumulate evidence per parameter name
    // Resolve: if int_evidence > 0 && float_evidence == 0 → INT
    //          if float_evidence > 0 → FLOAT
    //          else → ANY
}
```

Once parameter types are known, apply Phase 1's type tracking: unbox parameters at function entry and use native types throughout the function body.

#### 3.4.3 Solution B: Dual Function Versions (Native + Boxed)

Following Lambda Phase 4 ([Lambda_Transpile_Restructure4.md §1](Lambda_Transpile_Restructure4.md)), generate two versions of eligible functions:

1. **Native version**: Parameters use native MIR types, body uses native arithmetic, returns native type
2. **Boxed wrapper**: All-`Item` ABI — unboxes params → calls native → boxes return. Used for dynamic dispatch and closures.

```
// function fib(n)  →  two MIR functions:

// Native version: fib_n(int64_t n) → int64_t
fib_n:
    if (n <= 1) return n              // native comparison + return
    return fib_n(n - 1) + fib_n(n - 2)  // native calls, native add

// Boxed wrapper: fib_b(Item n) → Item
fib_b:
    int64_t n_native = unbox_int(n)
    int64_t result = fib_n(n_native)
    return box_int(result)
```

**Call-site resolution** (same logic as Lambda Phase 4):
- Direct call to known function with known arg types → call native version
- Direct call with unknown arg types → call boxed wrapper
- Dynamic dispatch (callbacks, closures) → call boxed wrapper
- Method call (`obj.method()`) → call boxed wrapper

#### 3.4.4 Return Type Inference

Following Lambda Phase 4 ([Lambda_Transpile_Restructure4.md §3.3](Lambda_Transpile_Restructure4.md)):

1. For single-expression functions (`return expr`), the return type is the expression's effective type
2. For multi-return functions, unify all return expression types; if consistent → use that type
3. If any return path has an unknown type → return ANY (boxed Item)

This enables the native version to return a native type, eliminating the box-at-return + unbox-at-caller overhead.

#### 3.4.5 Tail-Call Optimization (TCO)

Several benchmarks (tak, cpstak, ack) are deeply recursive. Lambda has full TCO that transforms tail-recursive calls into loops. The JS transpiler has none.

**Implementation**: Detect self-tail-calls (return statement whose expression is a recursive call to the same function) and transform:

```javascript
function tak(x, y, z) {
    if (y >= x) return z;
    return tak(tak(x-1, y, z), tak(y-1, z, x), tak(z-1, x, y));
}
```

The outer `tak` call is a tail call. Transform to:

```
tak_entry:
    if (y >= x) return z
    x_new = tak(x-1, y, z)
    y_new = tak(y-1, z, x)
    z_new = tak(z-1, x, y)
    x = x_new; y = y_new; z = z_new
    goto tak_entry
```

Follow Lambda's TCO mechanism: `tco_label` for the loop-back label, `tco_count_reg` for recursion depth tracking, parameter registers reassigned before the jump.

#### 3.4.6 Complexity and Risk

| Step | Effort | Risk |
|------|--------|------|
| Parameter type inference | High | Medium — heuristic-based, may mispredict |
| Dual function versions | High | Medium — must handle all call-site patterns |
| Return type inference | Medium | Low — same approach as Lambda |
| Tail-call optimization | High | Medium — must correctly identify tail calls |
| **Total** | **~8 days** | **Medium-High** |

---

### Phase 5: Property Access and Method Dispatch Optimization

**Target**: nqueens, brainfuck, nbody, json_gen. **Expected impact**: 2–3x on object-heavy benchmarks.

**Dependency**: Phase 1.

#### 3.5.1 Problem: Dynamic Property Access

`js_property_access(obj, key)` performs a cascade of runtime checks:
1. `get_type_id(obj)` — type dispatch
2. If MAP: check typed array marker, check DOM marker, check computed style marker — 3 branch checks before the actual map lookup
3. `map_get()` — hash lookup by string key

For simple `.x` property access on a plain object, this is ~10 branch checks + a hash lookup per access.

#### 3.5.2 Solution: Compile-Time Method Resolution

When the transpiler sees `Math.sqrt(x)`, it currently emits:
```
obj = lookup("Math")
fn = js_property_access(obj, "sqrt")
result = js_invoke_fn(fn, x)
```

Instead, resolve known globals at compile time:

```cpp
// In jm_transpile_call_expr:
if (callee is MemberExpression && object is "Math") {
    switch (property_name) {
        case "sqrt":  return jm_call_1(mt, "fn_sqrt", ...);   // direct call
        case "abs":   return jm_call_1(mt, "fn_abs", ...);
        case "floor": return jm_call_1(mt, "fn_floor", ...);
        case "ceil":  return jm_call_1(mt, "fn_ceil", ...);
        case "trunc": return jm_call_1(mt, "fn_trunc", ...);
        case "max":   return jm_call_2(mt, "fn_max", ...);
        case "min":   return jm_call_2(mt, "fn_min", ...);
        case "pow":   return jm_call_2(mt, "fn_pow", ...);
        case "log":   return jm_call_1(mt, "fn_log", ...);
        case "sin":   return jm_call_1(mt, "fn_sin", ...);
        case "cos":   return jm_call_1(mt, "fn_cos", ...);
        // ...
    }
}
```

For `Math.sqrt`, take it further — when the argument is a known float, emit the native instruction directly:

```cpp
// Math.sqrt(x) where x is known FLOAT → MIR_DSQRT (single instruction)
if (property_name == "sqrt" && arg_type == LMD_TYPE_FLOAT) {
    emit_insn(mt, MIR_new_insn(mt->ctx, MIR_DSQRT, result_op, arg_op));
    return result;
}
```

#### 3.5.3 Solution: String Method Inlining

Replace the runtime string dispatcher (`js_string_method()` with `strcmp` chains) with compile-time resolution:

```cpp
// In jm_transpile_call_expr for member expressions on strings:
if (obj_type == LMD_TYPE_STRING) {
    switch (method_name_hash) {
        case hash("length"):    return jm_call_1(mt, "fn_len", ...);
        case hash("indexOf"):   return jm_call_2(mt, "fn_index_of", ...);
        case hash("substring"): return jm_call_3(mt, "fn_substring", ...);
        case hash("charAt"):    return jm_call_2(mt, "fn_index", ...);
        // ...
    }
}
```

#### 3.5.4 Solution: TypedArray Direct Access

When a variable is known to be a specific typed array (from `new Int32Array(n)` or `new Float64Array(n)` at declaration), emit direct memory access instead of `js_property_access()`:

```cpp
// arr[i] where arr is known Int32Array, i is known INT:
// 1. Load data pointer (hoisted if in loop)
// 2. Direct load: val = *(int32_t*)(data + i * 4)
// 3. Widen to int64_t

MIR_reg_t data = load_typed_array_data(mt, arr_reg);
MIR_reg_t raw = new_reg(mt, "elem", MIR_T_I64);
// MIR_T_I32 load at computed offset
emit_insn(mt, MIR_new_insn(mt->ctx, MIR_MUL, offset_reg, i_reg, imm4));
emit_insn(mt, MIR_new_insn(mt->ctx, MIR_ADD, addr_reg, data_reg, offset_reg));
// Load i32 and sign-extend
emit_insn(mt, MIR_new_insn(mt->ctx, MIR_LDMOV, raw, addr_op));  // load 32-bit
```

For `Float64Array`, the load is a native `MIR_T_D` (double) — no boxing needed if the value is used in native float arithmetic.

#### 3.5.5 Solution: Object Shape Caching (Future)

For objects with fixed shapes (e.g., `{x: 0, y: 0, vx: 0, vy: 0}` in nbody), track the shape at creation time and resolve field offsets at compile time instead of hash lookup. This is analogous to V8's hidden classes but much simpler:

- At `let body = {x: 0, y: 0, ...}`, record the key set and order
- At `body.x`, if the object was created in the same function with a known shape, emit a direct offset load

This is a longer-term optimization and may not be needed if the simpler techniques in Phases 1–4 close the gap sufficiently.

#### 3.5.6 Complexity and Risk

| Step | Effort | Risk |
|------|--------|------|
| Compile-time Math method resolution | Low | Low — well-defined set of functions |
| Compile-time string method resolution | Low | Low |
| TypedArray direct access | High | Medium — pointer arithmetic, bounds checking |
| Object shape caching | Very High | High — requires shape tracking infrastructure |
| **Total (without shape caching)** | **~4 days** | **Medium** |

---

## 4. Speculative Optimizations for Untyped Code

JavaScript is dynamically typed. Even with Phases 1–4, many variables will have type `ANY` because their type depends on runtime values (function parameters without inference, object property values, dynamically computed values).

For these cases, apply **speculative native fast paths** — the same technique proposed in Lambda Phase 4 ([Lambda_Transpile_Restructure4.md §3.5](Lambda_Transpile_Restructure4.md)):

### 4.1 Speculative INT Arithmetic

When both operands are `ANY`, emit a runtime type check that takes the fast path if both happen to be INT:

```
// a + b where both are ANY:
tag_a = a >> 56
tag_b = b >> 56
if (tag_a != INT_TAG || tag_b != INT_TAG) goto slow_path

// Fast path: native int add (~4 instructions)
val_a = sign_extend_56(a)
val_b = sign_extend_56(b)
result = val_a + val_b
boxed_result = box_int(result)
goto done

slow_path:
    boxed_result = call js_add(a, b)    // full runtime dispatch
done:
```

**Expected impact**: Since most benchmark variables are integers, the fast path will be taken in the vast majority of iterations — even when the transpiler cannot statically prove the type. The overhead is ~4 instructions for the type check (shift + compare + branch) vs ~20+ instructions for the function call path.

### 4.2 Speculative FLOAT Arithmetic

Similar pattern for float-heavy code, but checking for the float tag:

```
tag_a = a >> 56
if (tag_a != FLOAT_TAG) goto slow_path
tag_b = b >> 56
if (tag_b != FLOAT_TAG) goto slow_path

// Fast path: load double from tagged pointer, native DADD
ptr_a = a & PTR_MASK
val_a = *(double*)(ptr_a + 8)     // load double value
ptr_b = b & PTR_MASK
val_b = *(double*)(ptr_b + 8)
result = val_a + val_b
boxed_result = push_d(result)      // allocate in nursery
goto done
```

The float case is less beneficial than int because it still requires `push_d()` (GC nursery allocation) to box the result. However, it avoids the 6-level function call chain of `js_add → js_to_number → fn_add`.

### 4.3 Guard Deoptimization

If the speculative fast path's assumption fails (e.g., a variable that was INT becomes STRING), the slow path handles it correctly. No deoptimization or recompilation is needed — both paths produce the same result, just at different speeds.

### 4.4 Complexity and Risk

| Step | Effort | Risk |
|------|--------|------|
| Speculative INT arithmetic | Medium | Low — correctness ensured by slow fallback |
| Speculative FLOAT arithmetic | Medium | Low |
| Speculative comparisons | Medium | Low |
| **Total** | **~3 days** | **Low** |

---

## 5. Implementation Roadmap

### Prioritized Order

| Priority | Phase | Optimization | Impact (est.) | Actual | Effort | Status |
|:--------:|:-----:|-------------|:-------------:|:------:|:------:|:------:|
| **P1** | 1 | Type-tracked variables | 2–3x | — | 3 days | ✅ Done |
| **P2** | 2 | Native int arithmetic | 5–10x | 2–43x | 3 days | ✅ Done |
| **P3** | 2 | Native float arithmetic | 3–5x (float code) | (incl. in P2) | 2 days | ✅ Done |
| **P4** | 3 | For-loop specialization | 2–3x (loop code) | 2–14x | 4 days | ✅ Done |
| **P5** | 4 | Parameter type inference | 2x (call-heavy) | — | 3 days | ⬜ Not started |
| **P6** | 4 | Dual function versions | 2–3x (recursive) | — | 5 days | ⬜ Not started |
| **P7** | 5 | Compile-time method resolution | 1.5–2x (method-heavy) | — | 2 days | ⬜ Not started |
| **P8** | 4 | TCO for recursive functions | 2–5x (tail-recursive) | — | 3 days | ⬜ Not started |
| **P9** | 5 | TypedArray direct access | 2–3x (array-heavy) | — | 3 days | ⬜ Not started |
| **P10** | 4 | Speculative INT fast path | 1.5–2x (untyped code) | — | 3 days | ⬜ Not started |

**Total estimated effort**: ~31 days (~12 days completed for P1–P4)

**Recommended minimum viable set** (P1–P4): ~12 days for the highest-impact optimizations — **completed**. LambdaJS now achieves 11–52x speedup on numeric benchmarks, reaching near-native performance on tight integer loops (50ms for 100M iterations).

**Progress**: P1–P4 completed. Measured 11–52x total speedup on micro-benchmarks. The bound caching optimization (P4) was the key breakthrough: `intSum` went from 0.71s to 0.05s (14x) by caching the unboxed loop bound before the loop, making `for(let i=0; i<n; i++)` with function-param bounds as fast as literal-bound loops. Remaining phases (P5–P10) target function calls, method dispatch, and TypedArray access. See [Section 9](#9-implementation-results-phases-13) for detailed results.

### Dependency Graph

```
Phase 1 (Type Tracking)                          ✅ DONE
   ├── Phase 2 (Native Arithmetic)               ✅ DONE
   │     └── Phase 3 (Loop Specialization)        ✅ DONE (bound caching + semi-native comparisons)
   ├── Phase 4 (Function Call Optimization)       ⬜ Not started
   │     ├── Parameter Inference
   │     ├── Dual Versions
   │     ├── Return Type Inference
   │     └── TCO
   └── Phase 5 (Property Access)                  ⬜ Not started
         ├── Method Resolution (independent)
         └── TypedArray Direct Access
         
Phase 4.Speculative (independent, can be done anytime after Phase 1)  ⬜ Not started
```

---

## 6. Expected Benchmark Improvements

### Conservative Estimates (Phases 1–3 Only)

| Benchmark | Current JS (ms) | Expected (ms) | Improvement | Technique |
|-----------|-------------:|------------:|:----------:|-----------|
| R7RS sum | 16.1 | ~0.5 | ~32x | Native int add in loop |
| R7RS sumfp | 1.6 | ~0.3 | ~5x | Native float add in loop |
| R7RS mbrot | 15.9 | ~2.0 | ~8x | Native float arithmetic |
| R7RS fib | 10.8 | ~4.0 | ~3x | Type inference on n, native arithmetic |
| AWFY mandelbrot | 794 | ~80 | ~10x | Native float, loop specialization |
| Kostya matmul | 1,390 | ~200 | ~7x | Native float, loop specialization, TypedArray |
| Kostya primes | 54.4 | ~10 | ~5x | Native int sieve loop |

### With Full Pipeline (Phases 1–5)

| Benchmark | Current JS (ms) | Expected (ms) | Improvement | Primary Technique |
|-----------|-------------:|------------:|:----------:|-------------------|
| R7RS sum | 16.1 | ~0.3 | ~50x | Native loop, no function calls |
| R7RS fib | 10.8 | ~2.5 | ~4x | Dual versions, native param/return |
| R7RS tak | 0.85 | ~0.2 | ~4x | TCO + native params |
| R7RS nqueens | 58.5 | ~10 | ~6x | Native array access, bool ops |
| AWFY mandelbrot | 794 | ~50 | ~16x | Full native float pipeline |
| Kostya brainfuck | 713 | ~200 | ~4x | Native int, TypedArray direct access |

---

## 7. Comparison with Alternative Approaches

### Why Not "Just Interpret"?

The current LambdaJS already JIT-compiles via MIR — the problem isn't interpretation vs compilation, but that the compiled code calls back into C for every operation. The fix is to emit the operations as native MIR instructions, not to change the overall architecture.

### Why Not a Profiling JIT (Like V8)?

V8's multi-tier JIT (Ignition interpreter → Sparkplug baseline → Maglev mid-tier → TurboFan optimizing) is a massive engineering effort. Lambda's approach of **ahead-of-time type inference at the function level** is much simpler and captures most of the benefit for benchmark-style code:

- V8's advantage comes from runtime profiling + inline caches + deoptimization
- Lambda's advantage is a simpler implementation that still produces native code for type-stable code
- For the benchmark suite, function-level type inference covers ~80% of the cases

### Why Not Transpile JS to Lambda Script?

Transpiling JS to Lambda AST was considered in v2 ([Transpile_Js2.md](Transpile_Js2.md)). The type systems and semantics diverge too much (JS coercion, `this` binding, prototype chains, `null`/`undefined` distinction). The direct MIR approach (v4, current) avoids these semantic mismatches while sharing the same runtime infrastructure.

---

## 8. Testing Strategy

### Correctness Tests

1. **Existing JS test suite**: All current JS tests must continue to pass with each phase
2. **Type inference validation**: Add tests for edge cases:
   - Variable type changes (`let x = 0; x = "hello"`)
   - Mixed-type arithmetic (`let x = 1; let y = x + 0.5`)
   - Function called with different argument types
   - Closures capturing typed variables
3. **Benchmark correctness**: Each benchmark must produce the same output as Node.js

### Performance Tests

1. **Per-phase regression**: Run the full benchmark suite after each phase
2. **A/B comparison**: Compare with/without each optimization via a feature flag
3. **Track the following metrics per benchmark**:
   - Execution time (median of 3 runs)
   - JS/MIR ratio (target: < 5x)
   - JS/Node.js ratio (target: < 10x)
   - JS/QuickJS ratio (target: < 1.0x — faster than QuickJS)

### Feature Flags

Each phase should be gated behind a flag during development:

```cpp
// In js_transpiler.hpp or compile-time defines
#define JS_ENABLE_TYPE_TRACKING     1   // Phase 1
#define JS_ENABLE_NATIVE_ARITH      1   // Phase 2
#define JS_ENABLE_LOOP_SPEC         1   // Phase 3
#define JS_ENABLE_FUNC_OPT          1   // Phase 4
#define JS_ENABLE_PROP_OPT          1   // Phase 5
#define JS_ENABLE_SPECULATIVE       1   // Speculative paths
```

This allows isolating regressions and measuring per-phase impact.

---

## 9. Implementation Results (Phases 1–3)

### 9.1 Changes Applied

Phases 1–3 were implemented in `transpile_js_mir.cpp` (~250 lines of new infrastructure + rewrites of binary/unary/assignment/var_decl/for-loop/while-loop/if transpilers).

**Phase 1: Type-Tracked Variables**
- `JsMirVarEntry` extended with `MIR_type_t mir_type` and `TypeId type_id` fields
- `jm_set_var()` extended with default type parameters
- `jm_get_effective_type()`: full expression type inference (~100 lines) covering literals, identifiers, binary ops, unary ops, assignment, conditional
- `jm_is_native_type()`: returns true for INT, FLOAT, BOOL
- `jm_transpile_var_decl`: creates native INT/FLOAT registers when init expression has known numeric type

**Phase 2: Native Arithmetic Emission**
- `jm_transpile_as_native()`: transpiles expressions returning native registers, with literal bypass (no boxing), identifier direct use, and robust boxed→native fallback via it2d+D2I
- `jm_transpile_binary`: native arithmetic fast path for ADD, SUB, MUL, DIV (float), MOD (int), all 6 comparisons, all 6 bitwise ops; boxed fallback for EXP, logical AND/OR, mixed-type operands
- `jm_transpile_unary`: native +, -, ++, -- for typed identifiers
- `jm_transpile_assignment`: native simple/compound assignment for INT and FLOAT variables (including DIV_ASSIGN via MIR_DIV for INT)
- `jm_transpile_box_item`: native-type-aware boxing with precise native-path detection for binary, unary, and assignment expressions
- Closure capture: boxes native-typed variables before storing in env slots
- JS 32-bit bitwise semantics: URSH masks to 32-bit unsigned, LSH/RSH sign-extend results

**Phase 3: For-Loop Specialization**
- `jm_transpile_binary`: semi-native comparison path — when at least one operand is typed numeric, the other is unboxed inline via `jm_transpile_as_native` and compared with native MIR instructions (eliminates boxing the typed side + 2 runtime calls)
- `jm_transpile_for`: bound caching — detects comparison tests where one side is typed numeric and the other is untyped (e.g., `i < n` where `n` is a function param); caches the unboxed bound ONCE before the loop, then uses native comparison + branch per iteration (0 runtime calls per iteration)
- `jm_transpile_while`: native test support — same semi-native comparison logic for while-loop conditions
- `jm_transpile_if`: native test support — uses native comparison result directly instead of boxing + `js_is_truthy` when test is a comparison with at least one typed numeric operand
- `jm_transpile_box_item` + `jm_transpile_as_native`: updated native-path detection to recognize semi-native comparisons for correct boxing/unboxing at expression boundaries

### 9.2 Bugs Found and Fixed During Implementation

1. **Missing forward declaration**: `jm_transpile_box_item` needed forward declaration before `jm_transpile_as_native`; without it, the C++ compiler silently called a wrong function from a different translation scope, causing infinite loops
2. **Closure segfault**: Native-typed variables stored raw in closure env slots; fixed by boxing before storing
3. **Conditional double-boxing**: `jm_transpile_conditional` returns boxed, but box_item tried to re-box when `jm_get_effective_type` returned INT/BOOL  
4. **For-loop infinite loop (mixed types)**: `i < n` where `i` is INT but `n` is untyped — native comparison returns 0/1 but boxed comparison returns tagged Item (always non-zero); fixed by checking both operands for `both_numeric` before using native branch
5. **EXP (power) boxed/native mismatch**: `js_power` returns FLOAT Item via `pow()`, but type inference said INT; fixed by making EXP always return FLOAT in type inference
6. **Bitwise NOT boxed/native mismatch**: `~5` goes through boxed `js_bitwise_not` runtime but type inference says INT; fixed with precise native-path detection for unary ops
7. **Unsigned right shift 64-bit bug**: `(-1) >>> 24` should give 255 (32-bit JS semantics) but MIR_URSH on 64-bit gives wrong result; fixed by masking left operand to 32 bits
8. **DIV_ASSIGN on INT**: Compound `/=` defaulted to MIR_ADD; fixed by adding MIR_DIV case

### 9.3 Benchmark Results

All benchmarks use `for (let i = 0; i < n; i++)` style loops with native-typed loop variables.

| Benchmark | Baseline (s) | Phase 2 (s) | Phase 3 (s) | P2 Speedup | P3 Speedup | Total Speedup |
|-----------|----------:|----------:|----------:|--------:|--------:|--------:|
| **intSum** | 1.51 | 0.71 | **0.05** | 2.1x | 14.2x | **30x** |
| **fib** | 3.52 | 0.63 | **0.32** | 5.6x | 2.0x | **11x** |
| **nested** | 2.60 | 0.64 | **0.05** | 4.1x | 12.8x | **52x** |
| **typed** | 1.73 | 0.04 | 0.05 | 43x | ~1x | **35x** |

**Phase 2 observations (native arithmetic):**
- 2–6x speedup on function-param-bound loops (mixed native/boxed) — arithmetic is native but loop test still boxed
- 43x speedup on fully-typed loops (literal bounds) — entire loop body and test are native MIR instructions

**Phase 3 observations (bound caching + semi-native comparisons):**
- **intSum 14x additional speedup** (0.71→0.05s): the `i < n` test was the bottleneck — caching the unboxed bound before the loop makes the entire loop zero-function-call. Now matches the "typed" benchmark speed.
- **nested 13x additional speedup** (0.64→0.05s): both outer `i < n` and inner `j < n` bounds are cached, making both loops fully native.
- **fib 2x additional speedup** (0.63→0.32s): the test `i < n` is now native, but the loop body (3 arithmetic ops + 1 modulo) was already native — the test overhead was only ~50% of the total.
- **typed ~1x** (no change): was already fully native with literal bounds — no bound caching needed.
- The `intSum` and `nested` benchmarks are now **50ms for 100M iterations**, matching the hardware-native performance ceiling for integer addition loops on Apple Silicon.

### 9.4 Test Suite

33/33 tests pass. Zero regressions. (`dom_basic` previously failing is now fixed.)

### 9.5 Phase 3 Detailed Tuning Analysis

Phase 3 targeted the remaining per-iteration overhead in loops where Phase 2 had already made the arithmetic native. The key insight: after Phase 2, a `for(let i=0; i<n; i++) sum += i` loop still called **two runtime functions per iteration** for the loop test alone (`jm_transpile_box_item(test)` → `js_is_truthy(boxed_result)`), even though the loop body (`sum += i`) was already a single `MIR_ADD`. The test was now the bottleneck, not the arithmetic.

#### 9.5.1 Bound Caching (`jm_transpile_for`)

**Problem**: In `for(let i = 0; i < n; i++)` where `i` is typed INT (from `let i = 0`) but `n` is an untyped function parameter, the Phase 2 "both operands typed" fast path doesn't fire. Every iteration executes:

```
// Per-iteration cost WITHOUT bound caching (Phase 2):
1. box i           →  AND + OR  (inline, ~2 instructions)
2. box n           →  load from var reg (already boxed, ~1 instruction)
3. call js_less_than(boxed_i, boxed_n)  →  FUNCTION CALL (~20 instructions: 
       2x get_type_id, 2x tag extract, 2x unbox, 1x compare, 1x box result)
4. call js_is_truthy(result)  →  FUNCTION CALL (~10 instructions:
       get_type_id, branch on type, extract bool value)
5. BF on truthy result
```

Total: **~35 instructions + 2 function calls per iteration** just for the loop test.

**Solution**: Before the loop starts, detect the `typed_counter CMP untyped_bound` pattern, unbox `n` once, and cache the native value in a register. Inside the loop, read `i` directly (already native) and compare with the cached bound using a single MIR comparison instruction.

```
// BEFORE the loop (one-time cost):
cached_bound = jm_transpile_as_native(n)  →  SHL 8 + RSH 8 (sign-extend unbox)

// Per-iteration cost WITH bound caching (Phase 3):
1. MIR_LTS(i_reg, cached_bound)  →  1 native comparison instruction
2. MIR_BF(result, loop_end)       →  1 branch instruction
```

Total: **2 instructions, 0 function calls per iteration**.

This is why `intSum` went from 0.71s to 0.05s (14x) — the loop body was already 1 instruction (`MIR_ADD`), and the test dropped from ~35 instructions + 2 calls to 2 instructions. The loop is now 3 native instructions total per iteration, matching what a C compiler would emit.

The bound caching implementation handles all 8 comparison operators (LT, LE, GT, GE, EQ, NE, STRICT_EQ, STRICT_NE) and both operand orderings (`i < n` and `n > i`). It selects between integer MIR instructions (MIR_LTS, MIR_LES, etc.) and float instructions (MIR_DLT, MIR_DLE, etc.) based on the typed operand's type.

**Three-tier test optimization in `jm_transpile_for`:**

| Tier | Condition | Per-iteration cost | Example |
|------|-----------|-------------------|---------|
| Full native | Both sides typed numeric | 2 instructions (compare + branch) | `for(let i=0; i<10; i++)` |
| Semi-native (bound cached) | One side typed, other untyped | 2 instructions (compare + branch) + 1× unbox before loop | `for(let i=0; i<n; i++)` where `n` is a param |
| Boxed fallback | No type info | ~35 instructions + 2 function calls | Dynamic/complex test expressions |

#### 9.5.2 Semi-Native Comparisons (`jm_transpile_binary`)

**Problem**: Phase 2's native comparison path required **both** operands to be typed numeric. When one operand is typed (e.g., a loop counter) and the other is untyped (e.g., a function parameter), the entire comparison fell through to the boxed runtime path — boxing both sides and calling `js_less_than()`.

**Solution**: Added a middle tier in `jm_transpile_binary` between the "both typed" fast path and the "boxed runtime" slow path. When at least one operand has a known numeric type and the operation is a comparison:

1. Transpile both sides as native values via `jm_transpile_as_native()` — the typed side extracts directly from its native register, the untyped side is unboxed inline (SHL 8 + RSH 8 for int, or load from tagged pointer for float)
2. Emit a single native MIR comparison instruction
3. Return a native 0/1 result (no boxing)

```
// Semi-native comparison: i < n  (i is typed INT, n is untyped)
// Phase 2 (boxed path):
    boxed_i = AND(i, MASK56) | INT_TAG     // box
    result = call js_less_than(boxed_i, n)  // runtime dispatch
    // result is a boxed bool Item

// Phase 3 (semi-native path):
    native_n = SHL(n, 8); RSH(native_n, 8) // inline unbox (sign-extend 56-bit)
    result = MIR_LTS(i, native_n)           // single native instruction
    // result is native 0/1
```

This eliminates **1 boxing operation + 1 function call** per comparison. The semi-native path fires for any comparison where the transpiler knows at least one operand's type, which covers the common patterns:
- `i < n` — typed loop counter vs function parameter
- `i < arr.length` — typed counter vs property access result
- `x > 0` — typed variable vs literal (already handled by full-native, but semi-native is the fallback)

#### 9.5.3 Native Tests in `if` and `while` Statements

**Problem**: After Phase 2, `if (i < n)` and `while (i < n)` still went through the boxed path when one operand was untyped:

```
// Phase 2 if-statement with mixed types:
    boxed_test = jm_transpile_box_item(test_expr)  // box the comparison result
    truthy = call js_is_truthy(boxed_test)          // FUNCTION CALL
    BF(truthy, else_label)
```

Even though the comparison itself might produce a native 0/1 via semi-native comparison, the `if`/`while` transpiler didn't know this and always boxed the result before calling `js_is_truthy()`.

**Solution**: Added native test detection in both `jm_transpile_if` and `jm_transpile_while`. Before transpiling the test expression, check whether it's a binary comparison with at least one typed numeric operand. If so, call `jm_transpile_expression` directly (which routes through the semi-native comparison path) and use the native 0/1 result for branching:

```
// Phase 3 if-statement with typed/untyped comparison:
    test_val = jm_transpile_expression(test_expr)  // returns native 0/1 via semi-native path
    BF(test_val, else_label)                        // direct branch, no boxing or js_is_truthy
```

This saves **1 boxing + 1 function call** per `if`/`while` evaluation. For `fib`, which has `if (n <= 1)` in a recursive function called millions of times, this contributed to the 2x Phase 3 speedup (0.63s → 0.32s).

#### 9.5.4 Updated Boxing/Unboxing Boundary Detection

The semi-native comparison path introduced a new case in the boxing boundary logic: comparison expressions that return native 0/1 when only one operand is typed. Both `jm_transpile_box_item` and `jm_transpile_as_native` needed to recognize this case to avoid double-boxing or incorrect unboxing.

**`jm_transpile_box_item` BINARY case**: Added `left_num`/`right_num` detection. When a comparison has at least one typed numeric operand but isn't "both numeric", the result is still native — must box it before passing to a runtime function. Without this fix, the native 0/1 result would be interpreted as a raw Item (tag bits misread).

**`jm_transpile_as_native` BINARY case**: Same detection — when downstream code needs a native value from a comparison and the comparison used the semi-native path, the result is already native. Without this fix, `jm_transpile_as_native` would try to unbox an already-native value (SHL 8 + RSH 8 on a 0/1 value), which happens to work for 0/1 but is wasteful.

#### 9.5.5 Compound Effect Across Tiers

The Phase 3 optimizations compound with each other and with Phase 2:

| Code pattern | Phase 2 | Phase 3 | Instructions per iteration |
|-------------|---------|---------|---------------------------|
| `for(let i=0; i<10; i++) sum+=i` | 3 (ADD + LTS + BF) | 3 (no change — already full native) | 3 |
| `for(let i=0; i<n; i++) sum+=i` | ~38 (ADD + box + call + is_truthy + BF) | 3 (ADD + LTS + BF via bound caching) | 3 |
| `if (i < n)` inside function | ~33 (box + call + is_truthy + BF) | 4 (unbox_n + LTS + BF) | 4 |
| Nested loops `i<n`, `j<n` | ~76 (2× inner) | 6 (2× bound cached before outer loop) | 6 |

The nested loop case (`bench_nested.js`) shows the largest speedup (52x total) because bound caching applies to both the outer and inner loops, and each loop iteration was dominated by the test overhead.

### 9.6 Performance Gap Analysis

After Phase 3, the remaining performance gaps are:

| Bottleneck | Affected benchmarks | Phase to address |
|-----------|-------------------|------------------|
| **Function parameters stay boxed** — `fib(n)` receives `n` as a boxed Item; every operation on `n` must unbox it | fib (11x total, but could be 30x+ with native params) | Phase 4 (P5: parameter type inference) |
| **Recursive call overhead** — each `fib(n-1)` boxes the argument, calls the boxed function ABI, unboxes inside | fib, tak, ack, cpstak | Phase 4 (P6: dual function versions) |
| **No tail-call optimization** — deep recursion uses stack frames instead of loop transformation | tak, cpstak, ack | Phase 4 (P8: TCO) |
| **Property access dispatches through runtime** — `arr[i]`, `obj.x` always call `js_property_access()` | nqueens, brainfuck, nbody | Phase 5 (P9: TypedArray direct access) |
| **Math methods dispatch through runtime** — `Math.sqrt(x)` does string lookup + property access + invoke | mandelbrot, fft, nbody | Phase 5 (P7: compile-time method resolution) |

The `fib` benchmark (11x) illustrates the remaining gap: its loop body is already native (arithmetic on typed variables), and Phase 3 made the `if (n <= 1)` test semi-native, but the recursive calls `fib(n-1)` still box `n-1`, invoke the boxed ABI wrapper, and unbox `n` again inside each call frame. Phase 4's dual function versions would eliminate this round-trip entirely.

---

## 10. References

- [Lambda_Transpile_Restructure.md](Lambda_Transpile_Restructure.md) — Dual-version function generation (`_n`/`_b`), structured returns
- [Lambda_Transpile_Restructure2.md](Lambda_Transpile_Restructure2.md) — JIT header diet, O(1) sys func lookup
- [Lambda_Transpile_Restructure3.md](Lambda_Transpile_Restructure3.md) — `_store_i64` elimination, inline array access, body-usage type inference
- [Lambda_Transpile_Restructure4.md](Lambda_Transpile_Restructure4.md) — Dual versions in MIR Direct, return type inference, bool specialization, inline item_at
- [Transpile_Js4.md](Transpile_Js4.md) — Direct MIR generation architecture (current JS engine)
- [Transpile_Js5.md](Transpile_Js5.md) — Language coverage: typed arrays, closures, control flow
- [Overall_Result2.md](../test/benchmark/Overall_Result2.md) — Benchmark results showing LambdaJS performance gaps
