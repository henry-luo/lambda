# Python Transpiler v6: Performance Optimization — Close the Gap with LambdaJS

## 1. Executive Summary

LambdaPy v5 achieved **0 FAIL across all 62 benchmarks** (52 PASS, 10 TIMEOUT in debug build). This proposal targets **closing the performance gap with LambdaJS** so that all benchmarks complete within timeout on release build, and Python-under-Lambda approaches JS-under-Lambda performance.

### Current Performance Gap (release build, benchmark_results_v3.json)

| Suite     | Benchmarks | Py/MIR geomean | Py/JS geomean | Worst Py/JS |
|-----------|-----------|----------------|---------------|-------------|
| R7RS      | 10        | 12.5×          | 5.4×          | cpstak 19.2× |
| AWFY      | 14        | 327×           | 95×           | sieve 12945× |
| BENG      | 10        | 2.5×           | 3.6×          | knucleotide 236× |
| Kostya    | 7         | 7.1×           | 1.4×          | primes 11.8× |
| Larceny   | 12        | 10.3×          | 5.7×          | array1 69.6× |
| JetStream | 9         | 2.0×           | 1.3×          | navier_stokes 8058× |
| **All 60**| **60**    | **11.6×**      | **5.7×**      | sieve 12945× |

**Target:** Py/JS geomean ≤ 2.0× across all suites, no single benchmark > 10× slower than JS.

### Why Python is Slow: Root Cause Analysis

The Python transpiler emits **100% boxed, polymorphic code**. Every operation goes through a runtime function call with full type dispatch:

| Operation | Python transpiler | JS transpiler | Slowdown factor |
|-----------|------------------|---------------|-----------------|
| `a + b` (int) | `py_add(Item, Item)` — 6-8 type branches | `MIR_ADD` native register | **10-50×** |
| `a < b` (int) | `py_lt(Item, Item)` — type check + convert to double | `MIR_LTS` native compare | **10-30×** |
| `a <= b` | `py_le()` = `py_lt()` + `py_eq()` — **double dispatch** | `MIR_LES` single instruction | **20-60×** |
| `obj.x` | `py_getattr()` — MRO walk, no cache | `js_get_shaped_slot(obj, N)` — O(1) indexed | **5-20×** |
| `for i in range(n)` | `py_get_iterator` + per-iteration `py_iterator_next` + `py_is_stop_iteration` + `py_setattr` (idx++) | Native counter + `MIR_LTS` bound check | **50-200×** |
| `f(x, y)` | `py_call_function` — 5 type checks + arity switch | Direct `MIR_CALL` to known function | **3-5×** |
| `self.method()` | `py_getattr` + `py_call_function` | `js_get_shaped_slot` + direct call | **10-30×** |

The AWFY micro benchmarks (sieve, permute, queens, etc.) are **10,000-30,000× slower** because they combine all these costs in tight loops with thousands of iterations per benchmark invocation.

---

## 2. Architecture Position

```
v1:  Core expressions, control flow, functions, 29 builtins      (✅ ~7.8K LOC)
v2:  Default/keyword args, slicing, f-strings, comprehensions    (✅ ~10.9K LOC)
v3:  OOP, inheritance, super, dunders, decorators, imports       (✅ ~14.4K LOC)
v4:  Generators, match/case, stdlib, async, bigints, metaclasses (✅ ~20K LOC)
v5:  Benchmark compatibility — 0 FAIL across 62 benchmarks       (✅ ~21K LOC)
v6:  Performance — native arithmetic, shaped slots, loop opt     (this doc, target ~23K LOC)
       Phase 1: Native integer arithmetic inline                 → 10-50× speedup on arithmetic
       Phase 2: Native comparisons inline                        → 10-30× on comparisons
       Phase 3: Range-for loop optimization                      → 50-200× on counted loops
       Phase 4: Shaped slot property access                      → 5-20× on OOP benchmarks
       Phase 5: Direct method dispatch                           → 3-10× on method-heavy code
       Phase 6: Runtime quick-fixes (py_le, iterator)            → 2× on comparison/iteration
```

### Design Principle: Follow the JS Transpiler's Proven Playbook

The LambdaJS transpiler went from ~55× slower than Node.js to ~7× through seven targeted optimizations (P1–P7). Each optimization followed the same pattern:

1. **Identify the hot path** (e.g., property access in class instances)
2. **Add compile-time type inference** (e.g., track which variables hold class instances)
3. **Emit native MIR instructions** instead of runtime function calls
4. **Keep boxed fallback** for untyped or polymorphic cases

We apply the same playbook to the Python transpiler, prioritized by benchmark impact.

---

## 3. Benchmark Impact Analysis

### Which optimizations fix which timeouts?

The 10 timeout benchmarks and their bottlenecks:

| Benchmark | Primary bottleneck | Fix phase | Expected speedup |
|-----------|--------------------|-----------|-----------------|
| r7rs/nqueens | int arithmetic in tight recursive loops | Phase 1+2 | 10-20× |
| beng/fannkuch | int arithmetic + comparisons in permutation loops | Phase 1+2 | 10-15× |
| beng/mandelbrot | float arithmetic in nested pixel loops | Phase 1+2 | 5-10× |
| beng/nbody | float arithmetic + `self.x` access | Phase 1+4 | 5-10× |
| kostya/brainfuck | array subscript + int arithmetic in interpreter loop | Phase 1+3 | 10-20× |
| kostya/collatz | int arithmetic + comparison in tight loop | Phase 1+2+3 | 20-50× |
| kostya/matmul | array subscript + float arithmetic in triple nested loop | Phase 1+3 | 10-20× |
| larceny/array1 | array subscript + int arithmetic | Phase 1+3 | 10-20× |
| larceny/primes | int arithmetic + comparison in sieve loop | Phase 1+2+3 | 10-20× |
| larceny/diviter | int arithmetic + comparison in deep iteration | Phase 1+2+3 | 20-50× |

**Phase 1+2+3 alone should eliminate all 10 timeouts** since they target the exact bottleneck (boxed arithmetic/comparison/loops) that causes the slowdown on compute-intensive benchmarks.

### AWFY micro benchmarks (sieve, permute, queens, towers, bounce, list, storage)

These are 1000-30000× slower than JS because they run thousands of iterations of OOP-heavy inner loops. They need **all phases** (1-5) for competitive performance, since every line of code involves attribute access, method calls, and arithmetic.

---

## 4. Phase 1: Native Integer Arithmetic (~400 LOC)

### Problem

Every `a + b` on integers emits:
```
CALL py_add(Item_a, Item_b) → Item_result
```
`py_add` does: 2× `get_type_id()`, 6-8 type branches, `it2i()` extraction, overflow check, `i2it()` re-boxing.

### Solution: Type Inference + Inline MIR Arithmetic

Add compile-time type tracking to `PyMirTranspiler`, mirroring the JS transpiler's `jm_get_effective_type()`:

```c
// New field on PyMirVarEntry
TypeId type_hint;  // LMD_TYPE_INT, LMD_TYPE_FLOAT, or LMD_TYPE_NULL (unknown)

// New function: pm_get_effective_type(mt, expr) → TypeId
// Returns inferred type based on:
//   - Integer literals → LMD_TYPE_INT
//   - Float literals → LMD_TYPE_FLOAT
//   - Bool literals → LMD_TYPE_BOOL
//   - Variables with known type_hint → that type
//   - Binary ops on int × int → LMD_TYPE_INT
//   - Binary ops involving float → LMD_TYPE_FLOAT
//   - range() → LMD_TYPE_INT (for loop variable)
//   - len() → LMD_TYPE_INT
//   - Otherwise → LMD_TYPE_NULL (unknown, use boxed path)
```

When both operands are provably INT:
```c
// pm_transpile_binary() — new native path
if (left_type == LMD_TYPE_INT && right_type == LMD_TYPE_INT) {
    MIR_reg_t l = pm_transpile_as_native_int(mt, bin->left);
    MIR_reg_t r = pm_transpile_as_native_int(mt, bin->right);
    MIR_reg_t result = pm_new_reg(mt, "iadd", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD, result, l, r));
    // overflow check: if result > INT56_MAX or < INT56_MIN, fallback to bigint
    return pm_box_int_reg(mt, result);
}
```

### Helper functions needed

| Function | Purpose |
|----------|---------|
| `pm_get_effective_type(mt, expr)` | Infer type from AST node |
| `pm_transpile_as_native_int(mt, expr)` | Emit code that produces unboxed int64 in register |
| `pm_transpile_as_native_float(mt, expr)` | Emit code that produces unboxed double in register |
| `pm_box_int_reg(mt, reg)` | Box int64 register back to Item (tag + mask) |
| `pm_box_float_reg(mt, reg)` | Box double register to Item (runtime `push_d`) |
| `pm_unbox_int(mt, item_reg)` | Extract int64 from boxed Item (`it2i` inline) |
| `pm_unbox_float(mt, item_reg)` | Extract double from boxed Item (`it2d` inline) |

### Operations to inline

| Python op | Typed path | MIR instruction | Overflow handling |
|-----------|-----------|-----------------|-------------------|
| `a + b` | int+int | `MIR_ADD` | Check INT56 bounds, fallback to `py_add` |
| `a - b` | int-int | `MIR_SUB` | Check INT56 bounds |
| `a * b` | int*int | `MIR_MUL` | `__builtin_mul_overflow` or bounds check |
| `a // b` | int//int | `MIR_DIV` + floor adjust | Zero-division check |
| `a % b` | int%int | `MIR_MOD` + sign adjust | Zero-division check |
| `a + b` | float+float | `MIR_DADD` | None needed |
| `a - b` | float-float | `MIR_DSUB` | None needed |
| `a * b` | float*float | `MIR_DMUL` | None needed |
| `a / b` | float/float | `MIR_DDIV` | None needed |

### Expected Impact

- **Arithmetic-heavy benchmarks** (fib, tak, nqueens, mandelbrot, nbody): **10-50×** speedup
- **Loop counters** (i += 1, i -= 1): **20-50×** speedup when combined with Phase 3
- Estimated: eliminates 4-6 of the 10 timeouts on its own

---

## 5. Phase 2: Native Comparisons (~150 LOC)

### Problem

Every `a < b` emits `CALL py_lt(Item, Item)` which does type dispatch + `py_get_number()` (converts to double). `a <= b` is **2× worse**: calls both `py_lt()` AND `py_eq()`.

### Solution

When both operands are provably INT or FLOAT, emit native MIR comparison:

```c
// pm_transpile_compare() — new native path
if (left_type == LMD_TYPE_INT && right_type == LMD_TYPE_INT) {
    MIR_reg_t l = pm_transpile_as_native_int(mt, left);
    MIR_reg_t r = pm_transpile_as_native_int(mt, right);
    MIR_reg_t result = pm_new_reg(mt, "cmp", MIR_T_I64);
    MIR_insn_code_t op;
    switch (cmp_op) {
        case PY_CMP_LT: op = MIR_LTS; break;
        case PY_CMP_LE: op = MIR_LES; break;  // single instruction, not double dispatch!
        case PY_CMP_GT: op = MIR_GTS; break;
        case PY_CMP_GE: op = MIR_GES; break;
        case PY_CMP_EQ: op = MIR_EQ; break;
        case PY_CMP_NE: op = MIR_NE; break;
    }
    pm_emit(mt, MIR_new_insn(mt->ctx, op, result, l, r));
    return pm_box_bool(mt, result);
}
```

### Runtime fix: `py_le` and `py_ge`

Even for untyped code, fix the double-dispatch bug:

```c
extern "C" Item py_le(Item left, Item right) {
    TypeId lt = get_type_id(left);
    TypeId rt = get_type_id(right);
    if ((lt == LMD_TYPE_INT || lt == LMD_TYPE_FLOAT) &&
        (rt == LMD_TYPE_INT || rt == LMD_TYPE_FLOAT)) {
        return (Item){.item = b2it(py_get_number(left) <= py_get_number(right))};
    }
    // string comparison
    if (lt == LMD_TYPE_STRING && rt == LMD_TYPE_STRING) {
        String* a = it2s(left); String* b = it2s(right);
        int cmp = memcmp(a->chars, b->chars, a->len < b->len ? a->len : b->len);
        if (cmp == 0) return (Item){.item = b2it(a->len <= b->len)};
        return (Item){.item = b2it(cmp <= 0)};
    }
    // dunder fallback
    if (lt == LMD_TYPE_MAP && py_is_instance(left)) {
        Item bm = py_getattr(left, (Item){.item = s2it(heap_create_name("__le__"))});
        if (get_type_id(bm) != LMD_TYPE_NULL) return py_call_function(bm, &right, 1);
    }
    return (Item){.item = b2it(it2b(py_lt(left, right)) || it2b(py_eq(left, right)))};
}
```

This makes the untyped path **2× faster** for numeric `<=`/`>=` by handling it in a single pass.

### Expected Impact

- **Comparison-heavy benchmarks** (collatz, primes, brainfuck): **2-10×** additional speedup on top of Phase 1
- **py_le/py_ge runtime fix**: **2× across all untyped le/ge operations** (immediately beneficial)

---

## 6. Phase 3: Range-For Loop Optimization (~250 LOC)

### Problem

`for i in range(n)` compiles to:
```
iter = py_get_iterator(range(n))       // Allocates Map object with __data__, __idx__, __len__
loop:
  item = py_iterator_next(iter)        // 3× py_getattr + 1× py_setattr + 1× py_subscript_get
  if py_is_stop_iteration(item): break
  _py_i = item
  ... body ...
  goto loop
```

**Per-iteration cost: ~150-200 CPU cycles** (5 hash lookups + 1 hash write + type checks)

The JS transpiler compiles `for(let i=0; i<n; i++)` to:
```
i = 0; bound = n;    // one-time setup
loop:
  if i >= bound: break    // MIR_LTS — native compare, ~1 cycle
  ... body ...
  i = i + 1              // MIR_ADD — native add, ~1 cycle
  goto loop
```

**Per-iteration cost: ~2-3 CPU cycles**

### Solution: Detect `for i in range(...)` at Compile Time

Pattern detection in `pm_transpile_for()`:

```c
// Detect: for <var> in range(<stop>) or range(<start>, <stop>) or range(<start>, <stop>, <step>)
if (forn->iter->node_type == PY_AST_NODE_CALL) {
    PyCallNode* call = (PyCallNode*)forn->iter;
    if (is_range_call(call)) {
        // Extract start, stop, step from arguments
        pm_transpile_range_for_native(mt, forn, start, stop, step);
        return;
    }
}
```

Native range-for emission:
```c
static void pm_transpile_range_for_native(PyMirTranspiler* mt, PyForNode* forn,
                                           PyAstNode* start, PyAstNode* stop, PyAstNode* step) {
    // 1. Evaluate bounds ONCE before loop
    MIR_reg_t counter = pm_new_reg(mt, "ri", MIR_T_I64);
    MIR_reg_t bound = pm_transpile_as_native_int(mt, stop);
    MIR_reg_t step_reg = step ? pm_transpile_as_native_int(mt, step) : /* 1 */;
    MIR_reg_t start_reg = start ? pm_transpile_as_native_int(mt, start) : /* 0 */;
    pm_emit(mt, MIR_MOV counter, start_reg);

    // 2. Set loop variable type hint to INT (enables Phase 1 inside body)
    const char* var_name = get_target_var(forn->target);
    pm_set_var_with_type(mt, var_name, counter, LMD_TYPE_INT);

    // 3. Loop structure
    MIR_label_t l_test = pm_new_label(mt);
    MIR_label_t l_body = pm_new_label(mt);
    MIR_label_t l_end = pm_new_label(mt);

    pm_emit_label(mt, l_test);
    // Native comparison: counter < bound
    MIR_reg_t cmp = pm_new_reg(mt, "rcmp", MIR_T_I64);
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_GES, cmp, counter, bound));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_BT, l_end, cmp));

    pm_emit_label(mt, l_body);
    // 4. Box counter for use in body (only if needed by non-native operations)
    MIR_reg_t boxed_i = pm_box_int_reg(mt, counter);
    pm_set_var(mt, var_name, boxed_i);

    // 5. Transpile body
    pm_transpile_block(mt, forn->body);

    // 6. Increment: counter += step (native)
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_ADD, counter, counter, step_reg));
    pm_emit(mt, MIR_new_insn(mt->ctx, MIR_JMP, l_test));

    pm_emit_label(mt, l_end);
}
```

### Key insight: Type propagation into loop body

Setting `type_hint = LMD_TYPE_INT` for the loop variable means that Phase 1 arithmetic inside the loop body will also use native paths:

```python
for i in range(n):
    x = i + 1       # Phase 1: native MIR_ADD (i is known INT)
    if i < limit:    # Phase 2: native MIR_LTS
        arr[i] = x  # subscript with native int index
```

### Expected Impact

- **Counted loops**: **50-200×** speedup (from ~150 cycles/iter to ~3 cycles/iter)
- Combined with Phase 1+2: **sieve goes from 12945× → ~5-10×** slower than JS
- Estimated: eliminates remaining 4-6 timeouts that Phase 1+2 didn't fix

---

## 7. Phase 4: Shaped Slot Property Access (~400 LOC)

### Problem

`self.x` emits `py_getattr(self, "x")` which:
1. Boxes attribute name as String Item
2. Calls `_map_get()` on instance (hash lookup)
3. If not found, walks MRO (2-5 more `_map_get()` calls)
4. If method, creates bound method wrapper

**Cost: 20-100 cycles per access** depending on MRO depth.

### Solution: Constructor Shape Analysis

Mirror the JS transpiler's P3/P4 optimization. During class definition transpilation:

1. **Scan `__init__` body** for `self.attr = expr` assignments
2. **Build a "shape"**: ordered list of property names with slot indices
3. **Store shape in class entry**: `PyClassEntry` with `slot_names[]` and `slot_count`
4. **In constructor**, emit `py_constructor_create_shaped(class, slot_count)` instead of generic instance creation
5. **For `self.attr` access**, emit `py_get_shaped_slot(obj, slot_idx)` — O(1) indexed access
6. **For `self.attr = val`**, emit `py_set_shaped_slot(obj, slot_idx, val)` — O(1) indexed write

```c
// New struct for class tracking
typedef struct {
    const char* name;
    int name_len;
    const char* slot_names[32];
    int slot_count;
    int parent_index;  // index in func_entries of __init__
} PyClassEntry;

// New: pm_ctor_scan_slots() — scan __init__ body for self.attr = ... assignments
// New: pm_find_class() — lookup class entry by name
// New: pm_ctor_prop_slot() — return slot index for a property name, or -1
```

### Property access compilation

```c
// pm_transpile_attribute() — new shaped path
if (attr->object is self && mt->current_class_entry) {
    int slot = pm_ctor_prop_slot(mt->current_class_entry, attr->attribute->chars);
    if (slot >= 0) {
        return pm_call_2(mt, "py_get_shaped_slot", MIR_T_I64,
            obj_reg, MIR_new_int_op(mt->ctx, slot));
    }
}
// fallback: py_getattr(obj, name)
```

### Runtime support needed

```c
// In py_runtime.cpp or py_class.cpp:
extern "C" Item py_constructor_create_shaped(Item cls, int slot_count);
extern "C" Item py_get_shaped_slot(Item obj, int slot_idx);
extern "C" void py_set_shaped_slot(Item obj, int slot_idx, Item value);
```

These can reuse Lambda's existing `Container` slot array mechanism — instances already store properties in a `Map` with a `TypeMap` (shape). The optimization is to **pre-assign slot indices at compile time** rather than discovering them at runtime via hash lookup.

### Expected Impact

- **OOP-heavy benchmarks** (AWFY sieve through cd): **5-20×** speedup on property access
- **self.x in methods**: O(100 cycles) → O(5 cycles)
- The AWFY macro benchmarks (richards, deltablue, havlak, cd) will see the biggest gains since they access class properties in tight loops

---

## 8. Phase 5: Direct Method Dispatch (~300 LOC)

### Problem

`obj.method(args)` compiles to:
```
method = py_getattr(obj, "method")     // MRO walk
result = py_call_function(method, args, argc)  // 5 type checks + arity switch
```

### Solution: Compile-Time Method Resolution

When the receiver's class is known (e.g., `self` in a method, or a variable with `class_entry`):

```c
// pm_transpile_call() — new method dispatch path
if (callee is ATTRIBUTE && receiver has class_entry) {
    PyClassEntry* ce = receiver->class_entry;
    int method_idx = pm_find_method(ce, method_name);
    if (method_idx >= 0) {
        // Direct call: emit MIR_CALL to the compiled method function
        // Prepend self as first argument
        pm_emit_direct_call(mt, ce->methods[method_idx].func_item, self_reg, args, argc);
        return result;
    }
}
```

This requires tracking which variables hold instances of known classes:

```c
// When we see: x = MyClass(...)
// Set x's type to class_entry = &my_class_entry
PyClassEntry* ce = pm_find_class(mt, class_name);
if (ce) {
    pm_set_var_class(mt, var_name, ce);
}
```

### Expected Impact

- **Method call overhead**: from ~100 cycles (getattr + dispatch) to ~10 cycles (direct call)
- **Deep call chains** (havlak, cd): **3-10×** additional speedup

---

## 9. Phase 6: Runtime Quick-Fixes (~50 LOC)

### 6a. Fix `py_le` / `py_ge` double dispatch

As shown in Phase 2, replace the current:
```c
extern "C" Item py_le(Item left, Item right) {
    Item lt_r = py_lt(left, right);
    Item eq_r = py_eq(left, right);
    return (Item){.item = b2it(it2b(lt_r) || it2b(eq_r))};
}
```

With a proper single-pass implementation that handles int/float/string/bigint/dunder in one function body. **2× faster for all `<=`/`>=` operations even without transpiler changes.**

### 6b. Lightweight range iterator

Replace the Map-based iterator (5 getattr + 1 setattr per iteration) with a dedicated struct:

```c
typedef struct {
    TypeId type;      // LMD_TYPE_MAP (or new iterator marker)
    int64_t current;
    int64_t stop;
    int64_t step;
} PyRangeIterator;

extern "C" Item py_range_iterator_next(Item iter) {
    PyRangeIterator* ri = (PyRangeIterator*)iter.ptr;
    if (ri->current >= ri->stop) return py_stop_iteration();
    Item val = (Item){.item = i2it(ri->current)};
    ri->current += ri->step;
    return val;
}
```

This makes even the **untyped** `for i in range(n)` path ~10× faster by avoiding 6 hash lookups per iteration.

---

## 10. Implementation Priority & LOC Estimates

| Phase | LOC | Difficulty | Impact | Dependencies |
|-------|-----|-----------|--------|--------------|
| **Phase 6** (runtime fixes) | ~50 | Low | 2× on le/ge, 10× on range iteration | None |
| **Phase 1** (native int arithmetic) | ~400 | Medium | 10-50× on arithmetic | Type inference infrastructure |
| **Phase 2** (native comparisons) | ~150 | Low | 10-30× on comparisons | Phase 1 (type inference) |
| **Phase 3** (range-for optimization) | ~250 | Medium | 50-200× on counted loops | Phase 1 (native int helpers) |
| **Phase 4** (shaped slots) | ~400 | High | 5-20× on OOP property access | Constructor analysis |
| **Phase 5** (direct method dispatch) | ~300 | High | 3-10× on method calls | Phase 4 (class tracking) |
| **Total** | **~1550** | | | |

### Recommended execution order

1. **Phase 6** first — immediate 2× on le/ge and 10× on range iteration with minimal code changes
2. **Phase 1** — builds the type inference infrastructure that Phase 2 and 3 depend on
3. **Phase 2** — small incremental addition on top of Phase 1
4. **Phase 3** — uses Phase 1 helpers for native loop counters; biggest single impact on timeouts
5. **Phase 4** — independent of 1-3 but more complex; biggest impact on AWFY OOP benchmarks
6. **Phase 5** — builds on Phase 4's class tracking

### Milestone checkpoints

| After | Expected result |
|-------|----------------|
| Phase 6 | All 10 timeouts still there, but faster. Runtime is 2× better for le/ge. |
| Phase 1+2 | 4-6 timeouts eliminated (pure numeric benchmarks complete in time) |
| Phase 1+2+3 | **All 10 timeouts eliminated** — 62/62 PASS on release build |
| Phase 1-5 | Py/JS geomean drops from 5.7× to ~2×. AWFY micro benchmarks from 1000-30000× to ~5-20×. |

---

## 11. Risk Analysis

| Risk | Mitigation |
|------|-----------|
| Type inference incorrect (e.g., int overflow to bigint) | Always emit overflow check + boxed fallback. Test with bigint benchmarks. |
| Shaped slot breaks dynamic attribute addition | Only use for `__init__`-declared properties. Unknown attrs fall through to hash map. |
| Performance regression on non-optimized paths | All changes add fast paths; boxed fallback is unchanged. |
| Increased compile time | Type inference is O(N) single-pass; shapes are scan-once. Negligible impact. |

---

## 12. Performance Targets

### Release build targets (30s timeout)

| Suite | Current Py/JS | Target Py/JS | Key optimization |
|-------|--------------|-------------|-----------------|
| R7RS | 5.4× | ≤ 2× | Phase 1+2 (native arithmetic) |
| BENG | 3.6× | ≤ 2× | Phase 1+2+3 (arithmetic + loops) |
| Kostya | 1.4× | ≤ 1.5× | Phase 1+3 (already close) |
| Larceny | 5.7× | ≤ 3× | Phase 1+2+3 (arithmetic + loops) |
| JetStream | 1.3× | ≤ 1.5× | Already close; Phase 1 for edge cases |
| AWFY micro | 95× | ≤ 5× | Phase 1-5 (all optimizations) |
| AWFY macro | 1.7× | ≤ 2× | Phase 4+5 (shaped slots + dispatch) |

### Timeout elimination target

**62/62 PASS on release build** after Phase 3 (no benchmark exceeds 30s timeout).

---

## 13. Files Modified

| File | Changes |
|------|---------|
| `lambda/py/transpile_py_mir.cpp` | Type inference, native arithmetic/comparison emission, range-for optimization, shaped slot emission, direct method dispatch |
| `lambda/py/py_runtime.cpp` | Fix `py_le`/`py_ge`, add `py_range_iterator_next`, add `py_get_shaped_slot`/`py_set_shaped_slot`, add `py_constructor_create_shaped` |
| `lambda/py/py_class.cpp` | Shaped instance creation support |
| `lambda/py/py_runtime.h` | Declarations for new runtime functions |

---

## Appendix A: Full Benchmark Timing Comparison (benchmark_results_v3.json)

| Suite | Benchmark | MIR (ms) | LambdaJS (ms) | Python (ms) | Py/MIR | Py/JS |
|-------|-----------|----------|---------------|-------------|--------|-------|
| r7rs | fib | 2.5 | 1.2 | 22.0 | 9.0× | 18.3× |
| r7rs | fibfp | 3.7 | 1.3 | 23.5 | 6.3× | 18.8× |
| r7rs | tak | 0.2 | 0.1 | 2.2 | 14.4× | 18.8× |
| r7rs | cpstak | 0.3 | 0.2 | 4.5 | 14.8× | 19.2× |
| r7rs | sum | 0.3 | 21.3 | 38.4 | 142× | 1.8× |
| r7rs | sumfp | 0.1 | 4.5 | 2.8 | 42× | 0.6× |
| r7rs | nqueens | 6.7 | 1.8 | 3.5 | 0.5× | 2.0× |
| r7rs | fft | 0.2 | 2.7 | 4.3 | 24× | 1.6× |
| r7rs | mbrot | 0.6 | 17.5 | 14.8 | 25× | 0.8× |
| r7rs | ack | 9.8 | 9.3 | 155.8 | 16× | 16.7× |
| awfy | sieve | 0.1 | 0.1 | 1763 | 33909× | 12945× |
| awfy | permute | 0.1 | 2.9 | 2113 | 33011× | 721× |
| awfy | queens | 0.1 | 2.3 | 1143 | 7829× | 497× |
| awfy | towers | 0.2 | 5.5 | 1113 | 5151× | 202× |
| awfy | bounce | 0.2 | 1.8 | 1389 | 7160× | 790× |
| awfy | list | 0.0 | 1.7 | 976 | 42440× | 584× |
| awfy | storage | 0.2 | 1.8 | 1274 | 6567× | 704× |
| awfy | mandelbrot | 31.6 | 160.8 | 741 | 23× | 4.6× |
| awfy | nbody | 46.9 | 372.9 | 135 | 2.9× | 0.4× |
| awfy | richards | 252.6 | 787.6 | 168 | 0.7× | 0.2× |
| awfy | json | 1.5 | 0.2 | 7.1 | 4.8× | 43× |
| awfy | deltablue | 64.1 | 154.2 | 67.9 | 1.1× | 0.4× |
| awfy | havlak | 60.9 | 1702 | 2107 | 35× | 1.2× |
| beng | binarytrees | 7.3 | 29.7 | 10.4 | 1.4× | 0.4× |
| beng | fannkuch | 0.8 | 0.5 | 5.1 | 6.7× | 9.6× |
| beng | fasta | 1.1 | 1.2 | 2.0 | 1.8× | 1.7× |
| beng | mandelbrot | 142.5 | 836.7 | 1368 | 9.6× | 1.6× |
| beng | nbody | 47.1 | 308.8 | 172 | 3.6× | 0.6× |
| beng | spectralnorm | 13.0 | 20.7 | 46.8 | 3.6× | 2.3× |
| kostya | brainfuck | 165.2 | 502.8 | 691 | 4.2× | 1.4× |
| kostya | matmul | 8.8 | 1303 | 535 | 61× | 0.4× |
| kostya | primes | 7.3 | 8.2 | 96.7 | 13× | 11.8× |
| kostya | levenshtein | 7.7 | 14.0 | 70.6 | 9.2× | 5.0× |
| kostya | collatz | 300.8 | 6222 | 7999 | 27× | 1.3× |
| larceny | triangl | 178.5 | 1005 | 2683 | 15× | 2.7× |
| larceny | array1 | 0.5 | 0.6 | 40.4 | 74× | 70× |
| larceny | deriv | 20.1 | 49.4 | 26.5 | 1.3× | 0.5× |
| larceny | diviter | 271.5 | 10773 | 26252 | 97× | 2.4× |
| larceny | divrec | 0.8 | 0.8 | 44.8 | 53× | 55× |
| larceny | paraffins | 0.3 | 1.2 | 2.9 | 9.0× | 2.5× |
| larceny | pnpoly | 58.5 | 70.6 | 112 | 1.9× | 1.6× |
| larceny | primes | 7.2 | 8.1 | 121 | 17× | 15× |
| larceny | puzzle | 3.8 | 15.0 | 20.7 | 5.5× | 1.4× |
| larceny | quicksort | 3.1 | 9.5 | 25.6 | 8.2× | 2.7× |
| larceny | ray | 7.1 | 11.3 | 11.6 | 1.6× | 1.0× |
| jetstream | nbody | 47.2 | 1863 | 146 | 3.1× | 0.1× |
| jetstream | cube3d | 23.5 | 20.8 | 45.6 | 1.9× | 2.2× |
| jetstream | richards | 259.4 | 566.2 | 225 | 0.9× | 0.4× |
| jetstream | splay | 164.8 | 48.9 | 326 | 2.0× | 6.7× |
| jetstream | deltablue | 17.4 | 47.9 | 18.1 | 1.0× | 0.4× |
| jetstream | crypto_sha1 | 16.5 | 143.6 | 321 | 19× | 2.2× |
| jetstream | raytrace3d | 348.4 | 719.7 | 144 | 0.4× | 0.2× |

**Geomean (60 benchmarks): Py/MIR = 11.6×, Py/JS = 5.7×**

### Appendix B: JS Transpiler Optimizations (for reference)

The LambdaJS transpiler achieved its performance through seven optimizations (P1–P7, documented in Overall_Result6.md):

| Opt | Name | Mechanism | Impact |
|-----|------|-----------|--------|
| P1 | Return type → variable type propagation | `jm_get_effective_type()` at var sites | Enables native arithmetic |
| P3 | Direct property stores in constructor | `js_set_shaped_slot()` | O(1) property writes |
| P4 | Direct property reads for typed instances | `js_get_shaped_slot()` | O(1) property reads |
| P5 | Module variable arithmetic without boxing | `modvar_type` tracking | Inline compound assignment |
| P6 | Single-expression function inlining | `jm_should_inline()` | Eliminates ABI overhead |
| P7 | Native method call resolution | Extended `jm_resolve_native_call()` | Direct MIR_CALL |
| P2 | Bump-pointer fast path for allocation | Pre-computed size class | Faster object creation |

The Python v6 proposal adapts P1→Phase 1, P3/P4→Phase 4, P7→Phase 5 for the Python transpiler's specific patterns.
