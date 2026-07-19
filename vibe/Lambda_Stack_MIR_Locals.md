# Lambda and LambdaJS per-frame MIR locals

**Status:** current implementation profile and reduction review

**Profile date:** 2026-07-19

**Current profile:** `79c407082`

**Comparison profile:** `638e11c93`

**Companion reports:** [Lambda_Stack_MIR.md](./Lambda_Stack_MIR.md),
[Lambda_Stack_JS_MIR.md](./Lambda_Stack_JS_MIR.md), and
[Lambda_Design_Stack_Rooting.md](./Lambda_Design_Stack_Rooting.md)

## 1. Conclusion

For the representative functions used by the stack-frame reviews, the current
production compiler emits:

| Compiler/function | Arguments | MIR locals | Precise root slots | Root-frame bytes | Executable MIR | Calls |
|---|---:|---:|---:|---:|---:|---:|
| Lambda `_frame_review_0` | 2 | **47** | **12** | **96** | 101 | 11 |
| LambdaJS `_js_frameReview_149` | 3 | **53** | **7** | **56** | 136 | 26 |

These numbers were refreshed at `79c407082` and are unchanged from the
post-rooting-implementation profile at `638e11c93`.

The most important interpretation is:

> A MIR local is a virtual register. It is not automatically a native-stack
> slot, a precise GC-root slot, or an independently allocated runtime object.

MIR's backend assigns virtual registers to physical registers, coalesces values
whose live ranges do not overlap, and spills only when necessary. Therefore 53
LambdaJS locals does **not** mean a 424-byte native frame. The separately
measured precise side-root frame is only seven `Item` slots, or 56 bytes.

The LambdaJS probe has only six more MIR locals than the Lambda probe. Its count
looks large because 25 locals are shared frame/return machinery and its dynamic
JS body needs many helper-call and exception-status temporaries. The historical
214-local LambdaJS result was a rooting implementation blow-up; that result is
not the current design.

Per-frame local count is not a language-wide constant. It varies with function
body, inferred representations, control flow, calls, exceptions, closures,
`eval`, async lowering, and return mode. The counts in this report are a stable,
like-for-like review probe, not a claim that every Lambda or JS function has
exactly 47 or 53 locals.

## 2. What is being counted

The probe sources are:

```lambda
fn frame_review(a: string, b: string) {
    let combined = a ++ b
    let holder = {value: combined}
    let values = [holder.value, combined]
    join(values, ":")
}
```

```javascript
function frameReview(a, b, callback) {
    let sum = a + b;
    let holder = { value: sum };
    sum = callback(holder, sum);
    if (sum) {
        holder.value = sum;
    }
    return holder.value;
}
```

The count comes from the MIR `local` declarations and the generated summary
between the selected function's `func` and `endfunc`. Function arguments are
reported separately and are not included in the local count. Executable MIR
counts exclude declarations, comments, blank lines, labels, prototypes,
imports, and `endfunc`.

Debug builds were used only to expose pre-backend MIR and frame-slot
diagnostics. No debug-build timing is treated as performance evidence.

### 2.1 Three different storage concepts

| Concept | Owner | Contents | Count for the probes |
|---|---|---|---:|
| MIR locals | MIR IR/backend | Virtual registers for source values, addresses, predicates, call results, and frame control | Lambda 47; JS 53 |
| Precise side-root slots | Lambda runtime `Context` root side stack | Only semantically GC-managed values that must be visible at a safepoint | Lambda 12; JS 7 |
| Native register/spill frame | MIR backend and platform ABI | Physical registers, call-preserved state, and only those virtual values the backend spills | Not derivable from the MIR-local count |

The wide-scalar side stack is a fourth storage class. `number_frame` or
`js_number_frame` is one MIR local holding its watermark. It does not allocate
one native or side-stack cell for every numeric MIR local. These probes reserve
no fixed per-function number slots; a wide scalar return can advance the
number watermark when the return value must be donated to its caller.

Production root analysis further confirms that roots and locals are not
one-to-one:

| Function | Semantic root candidates | Stable slots | Reusable scratch slots | Final root slots | Root stores |
|---|---:|---:|---:|---:|---:|
| Lambda `_frame_review_0` | 20 | 12 | 0 | **12** | 12 |
| LambdaJS `_js_frameReview_149` | 19 | 5 | 2 | **7** | 11 |

Different virtual registers can be versions of one canonical binding home, and
short-lived temporaries whose safepoint live ranges do not overlap can reuse a
scratch slot. Function arguments can also be root candidates even though MIR
does not count them as locals.

### 2.2 Are MIR locals typed?

Yes, at two distinct levels:

1. MIR gives each virtual register a machine representation such as `i64`,
   `d` (double), or `p` (pointer).
2. The Lambda emitters separately track semantic GC representation, such as a
   boxed `Item`, raw GC pointer, non-GC scalar, or non-GC address.

Every local in the two selected function dumps is declared as `i64`, but they
do not all have the same semantics. An `i64` may hold a tagged `Item`, a raw
address, a Boolean predicate, an argument-stack pointer, or a root-stack
watermark. GC rooting is consequently driven by semantic representation and
safepoint liveness, not by the MIR machine type alone. Other generated
functions do contain `d` locals, so `i64` is a property of these two functions,
not a general LambdaJS restriction.

## 3. Lambda MIR-Direct: 47-local breakdown

The current `_frame_review_0` declaration contains 47 locals in four groups:

| Group | Count | Purpose |
|---|---:|---|
| Runtime and module access | 8 | Resolve `Context`, constants, type metadata, heap, and GC pointers |
| Frame and return control | 7 | Root/number watermarks, unified return, bounds check, and zeroing result |
| Function-body values and call results | 18 | Boxed parameters, bindings, containers, field access, constants, and helper results |
| Dynamic scalar-return classifier/donation | 14 | Detect an out-of-line scalar payload and transfer its number-side-stack ownership |
| **Total** | **47** | |

### 3.1 Runtime and module access: 8

```text
rt_addr_0          address of the imported `_lambda_rt` cell
runtime_1          current `Context*`
mod_consts_bss_2   address of the module-constants BSS cell
consts_3           module constants pointer
mod_tl_bss_4       address of the module type-list BSS cell
type_list_5        module type-list pointer
heap_ptr_6         current heap pointer loaded from `Context`
gc_ptr_7           GC pointer loaded from the heap
```

These are compiler/runtime access values, not user variables. Their names are
virtual-register names; the backend may coalesce or rematerialize them.

### 3.2 Frame and return control: 7

```text
root_frame_8        base of this activation's precise side-root frame
return_value_9      unified boxed return value
number_frame_10     saved number-side-stack watermark
root_top_43         proposed root watermark after reservation
root_limit_44       root-side-stack limit
root_overflow_45    reservation bounds-check result
root_frame_zeroed_46 result operand of the frame-zeroing `memset` call
```

Only `root_frame_8` points at the 12-slot root frame. The other six are control
values; they are not six additional root slots.

### 3.3 Function-body values and call results: 18

```text
boxs_11, boxs_12              boxed string arguments
fn_join_13, letv_14           `a ++ b` result and `combined` binding
type_list_15                  type-list constant used to create `holder`
map_with_tl_16, map_fill_17   map allocation/fill results
letv_18                       `holder` binding
array_19                      in-progress `values` array
boxc_20, fld_21               member receiver and field-name Item
fn_member_22                  `holder.value` result
array_end_23, letv_24         completed array and `values` binding
boxc_25, cptr_26, boxs_27     `join` receiver/constant preparation
fn_join2_28                   final `join(values, ":")` result
```

Several pairs are source-level or exception-safe assignment boundaries. For
example, a helper result is first materialized and then committed to a binding.
MIR's backend can often map such non-overlapping virtual registers to the same
physical register even though both names remain in the textual MIR.

### 3.4 Dynamic scalar-return classifier/donation: 14

```text
scalar_top_29          current number-side-stack top
scalar_payload_30      decoded payload address, or zero
scalar_result_31       returned Item after any rehoming
scalar_double_32       inline-double classification predicate
scalar_tag_33          Item type tag
scalar_is_type_34      reusable type-test predicate
scalar_inline_double_35
scalar_zero_36         zero/sentinel predicate
scalar_inline_int64_37
scalar_ge_base_38      payload is within this activation's number extent
scalar_lt_top_39       payload is below the saved top
scalar_raw_40          raw wide-scalar bits
scalar_tag_bits_41     preserved Item tag bits
scalar_donated_top_42  caller-owned watermark after donation
```

This is a shared correctness mechanism, not GC-root publication. It prevents a
returned boxed `int64`, float, or datetime from retaining a payload owned by a
callee number extent that is about to be restored.

## 4. LambdaJS: 53-local breakdown

The current `_js_frameReview_149` declaration contains 53 locals in six groups:

| Group | Count | Purpose |
|---|---:|---|
| Runtime and base frame | 6 | Runtime pointer, root/number bases, unified return, and direct-`eval` cleanup state |
| Root reservation/control | 5 | First-entry binding and checked root-stack reservation |
| Source-binding versions | 4 | TDZ/predeclaration and committed versions of `holder` and `sum` |
| Exception-status results | 9 | Immediate branches after potentially throwing JS operations |
| Other JS operation temporaries | 15 | Dynamic operators, object/property helpers, call setup, constants, and truthiness |
| Dynamic scalar-return classifier/donation | 14 | Same ownership transfer used by Lambda MIR-Direct |
| **Total** | **53** | |

### 4.1 Runtime and base frame: 6

```text
side_rt_addr_0       address of the imported `_lambda_rt` cell
side_rt_1            current `Context*`
js_root_frame_2      base of this activation's precise root slots
js_number_frame_3    saved number-side-stack watermark
js_return_value_4    unified boxed return value
eval_local_frame_5   whether a direct-eval lexical frame must be popped
```

`eval_local_frame_5` exists in the general JS function skeleton so normal,
implicit, and exceptional exits can share correct cleanup. In this probe it
remains zero because the body does not execute direct `eval`.

### 4.2 Root reservation/control: 5

```text
js_root_top_48       proposed root watermark after seven slots
js_root_limit_49     root-side-stack limit
js_root_overflow_50  bounds-check result
js_root_bound_51     whether this thread/context is already bound
js_root_ensured_52   result of the first-entry side-stack ensure call
```

Together with `js_root_frame_2`, these implement the checked seven-slot frame.
They are six MIR virtual registers around the root frame, not six extra roots.
The first-entry ensure call is skipped after the context is bound; capacity
checks remain inline.

### 4.3 Source-binding versions: 4

```text
_js_holder_6   predeclared/TDZ state for `holder`
_js_sum_7      predeclared/TDZ state for `sum`
_js_sum_8      initialized and subsequently assigned `sum`
_js_holder_10  initialized `holder`
```

JavaScript lexical bindings must represent the uninitialized TDZ state before
their declaration is evaluated. The distinct committed versions also make
assignment, exception, and root-liveness boundaries explicit.

### 4.4 Exception-status results: 9

```text
js_check_exception_14
js_check_exception_18
js_check_exception_19
js_check_exception_22
js_check_exception_23
js_check_exception_26
js_check_exception_27
js_check_exception_29
js_check_exception_31
```

Each result is consumed immediately by a branch to the shared exception exit.
These names account for 17.0% of the textual local count. Their live ranges are
short and non-overlapping, so they are strong candidates for physical-register
coalescing even before any textual-MIR cleanup.

### 4.5 Other JS operation temporaries: 15

```text
js_add_9                         dynamic `a + b` result
js_new_object_11                  object allocation result
strlit_12                         property-name Item for object initialization
js_create_data_property_13        property-definition helper result
js_args_save_15                   saved JS argument-stack watermark
js_debug_check_callee_16          callee-check helper result
js_args_push_17                   argument area for the callback
undef_20                          callback `this` value
js_call_function_21               callback result
js_is_truthy_24                   `if (sum)` predicate
strlit_25                         property-name Item for assignment
js_property_set_named_ic_28       property-set helper result
js_property_access_named_ic_30    returned `holder.value`
undef_32                          implicit-return value
null_33                           exceptional-return sentinel
```

The body uses dynamic JS helpers for addition, object construction, property
definition, TDZ checking, argument-stack management, callback validation and
dispatch, truthiness, property assignment, property access, and exception
testing. This is why the probe has 26 calls versus 11 in Lambda.

### 4.6 Dynamic scalar-return classifier/donation: 14

```text
scalar_top_34, scalar_payload_35, scalar_result_36
scalar_double_37, scalar_tag_38, scalar_is_type_39
scalar_inline_double_40, scalar_zero_41, scalar_inline_int64_42
scalar_ge_base_43, scalar_lt_top_44, scalar_raw_45
scalar_tag_bits_46, scalar_donated_top_47
```

This group has the same purpose as the Lambda group in §3.4. The JS function's
inferred return is `ANY`, so the current compiler cannot statically exclude a
wide scalar payload and must emit the dynamic classifier.

## 5. Why LambdaJS has more locals

The 53-local total separates into 25 frame/return locals and 28 body-related
locals:

| Component | Lambda | LambdaJS | Difference |
|---|---:|---:|---:|
| Runtime, frame, root control, and scalar return | 29 | 25 | −4 |
| Body bindings, operations, and exception results | 18 | 28 | +10 |
| **Total** | **47** | **53** | **+6** |

LambdaJS therefore does not have a dramatically larger current frame
skeleton. Its extra locals come from JS semantics:

- TDZ-aware lexical predeclaration and later committed binding versions;
- dynamic operator/property/call helpers instead of statically typed direct
  operations;
- explicit argument-stack save, allocate, populate, call, and restore state;
- exception checks and a shared exceptional return path after throwing
  operations;
- dynamic truthiness and inline-cache property access;
- general cleanup state for direct `eval` and other JS features.

The scalar-return classifier is exactly the same 14-local cost in both
functions. JS has two fewer runtime-access locals and two fewer frame/root
control locals than Lambda in this categorization. Its fixed machinery is thus
four locals smaller; rooting is not the explanation for the six-local total
difference.

## 6. Rooting contribution and historical comparison

### 6.1 Same current compiler

| Function/mode | MIR locals | Root slots | Root stores |
|---|---:|---:|---:|
| Lambda write-through oracle | 65 | 34 | not separately reported |
| Lambda production write-back | **47** | **12** | 12 |
| Lambda reduction | **−18** | **−22** | — |
| LambdaJS write-through oracle | 54 | 15 | 31 |
| LambdaJS production write-back | **53** | **7** | **11** |
| LambdaJS reduction | **−1** | **−8** | **−20** |

The JS result is especially useful: changing from always-current write-through
to production safepoint write-back removes more than half of the root slots and
almost two thirds of the root stores, while changing the textual local count by
only one. Current JS local count is consequently dominated by language and
fixed return/frame lowering, not by root-publication temporaries.

Lambda's oracle has 18 more locals because its eager path materializes more
root-publication intermediates for this body. Production's CFG liveness,
dirty-state propagation, scratch coloring, and direct store insertion remove
those intermediates as well as 22 slots.

### 6.2 Implementation generations

| Function | Pre-stack locals | Broad/reviewed side-stack locals | Current locals | Broad/reviewed → current |
|---|---:|---:|---:|---:|
| Lambda `_frame_review_0` | 60 | 80 | **47** | **−33 (−41.3%)** |
| LambdaJS `_js_frameReview_149` | 29 | 214 | **53** | **−161 (−75.2%)** |

The historical LambdaJS 214-local function came from broad shadow publication:
161 root copy/store pairs introduced many one-use root-copy registers. The
current canonical-slot finalizer emits 11 direct stores for the same source and
does not scan or republish all root bindings after every instruction.

The pre-stack baselines used different rooting/runtime machinery, so they are
useful architectural comparison points rather than isolated measurements of
one compiler switch. The same-compiler table in §6.1 is the clean comparison of
current publication policies.

## 7. Can the MIR-local count be reduced?

Yes, but textual local count should not be the primary performance target.
Several current names already have non-overlapping live ranges and may share a
physical register. The best changes remove executable work or actual spills,
not merely names from the dump.

### 7.1 Recommended opportunities

| Priority | Opportunity | Textual-local opportunity | Expected value and constraint |
|---|---|---:|---|
| 1 | Specialize scalar-return mode from proven return type | Up to 14 in either function | Also removes a substantial classifier/donation block. Safe only when type inference proves the return cannot reference a callee-owned wide scalar. The Lambda probe's final string is a strong candidate; the JS `ANY` return is not. |
| 2 | Reuse one JS exception-status virtual register | Up to 8 | All nine results are immediately branched on. Primarily reduces IR size because the backend probably coalesces them already. Must preserve every semantic check and branch. |
| 3 | Direct helper results into their committed binding when safe | Several | Examples include `fn_join_13` → `letv_14` and `js_add_9` → `_js_sum_8`. Only combine them when exception, TDZ, debugger, closure, and root-home boundaries remain correct. |
| 4 | Elide dead straight-line TDZ placeholders | 2 in this JS probe | `_js_holder_6` and `_js_sum_7` are never read before initialization here. Requires CFG-aware proof; it is unsafe for branches, closures, direct `eval`, or an observable pre-initialization access. |
| 5 | Common repeated constants | About 2 in this JS probe | The duplicate property string and `undefined` constants can share or be rematerialized. Mostly an IR/compile-time cleanup if backend constant propagation already handles them. |
| 6 | Add discard-result call lowering | Up to 3 JS plus the Lambda `memset` result | Some helper return operands are unused. A void/discard form can avoid a virtual result only when the ABI/import form permits it and exception behavior remains unchanged. |
| 7 | Specialize already-bound internal JS entry paths | Up to 2 control locals | Could omit `js_root_bound`/`js_root_ensured` on entry paths whose context-binding invariant is proven. This complicates ABI/re-entry rules and is lower priority than return specialization. |

### 7.2 Changes not recommended solely to lower the number

- Do not merge MIR locals across `MAY_GC` calls if doing so loses semantic root
  identity or makes the safepoint value ambiguous.
- Do not make every MIR local a root slot. Machine representation alone cannot
  distinguish GC references from non-GC addresses and predicates.
- Do not eliminate TDZ, exception, direct-`eval`, async, or non-local cleanup
  state based only on the simple probe.
- Do not count the 47 or 53 locals as native-frame bytes or multiply them by
  eight to claim stack savings.
- Do not restore broad post-instruction root-binding scans to obtain simpler
  lowering; that was the source of the historical JS expansion.

## 8. Performance-oriented measurement before further reduction

Before optimizing local declarations, add or collect post-register-allocation
metrics per generated function:

1. native frame size in bytes;
2. spill-slot count and spill load/store count;
3. peak simultaneously live virtual registers at each call;
4. generated machine-code bytes;
5. executable MIR instructions and call count;
6. side-root slots, stores, and stores executed at runtime;
7. release-build workload timing and GC time.

A useful acceptance condition is not “fewer local names.” It is one or more of:
fewer executable instructions, fewer actual spills, smaller machine code,
fewer executed root stores, or a repeatable release-build speedup, with precise
forced-GC and write-through/write-back differential tests still passing.

The highest-value first experiment is return-mode specialization because it
can remove both 14 textual locals and the corresponding dynamic instruction
block. Reusing exception registers is worthwhile compiler hygiene, but should
be expected to improve MIR size more than execution time unless backend spill
measurements show otherwise.

## 9. Implementation and evidence map

| Area | Current implementation |
|---|---|
| Lambda virtual-register creation and frame lowering | `lambda/transpile-mir.cpp` |
| LambdaJS virtual-register creation and side-root frame | `lambda/js/js_mir_hashmap_scope_utils.cpp` |
| LambdaJS function skeleton, TDZ bindings, and `eval` cleanup | `lambda/js/js_mir_function_class_lowering.cpp`, `lambda/js/js_mir_analysis.cpp` |
| Shared scalar-return classifier/donation | `lambda/mir_emitter_shared.hpp` |
| Shared semantic root liveness/write-back finalization | `lambda/mir_emitter_shared.hpp` |
| MIR backend optimization setup | `lambda/mir.c` |

Current refreshed artifacts:

- `temp/lambda_mir_locals_head_writeback.txt` and
  `temp/lambda_mir_locals_head_writeback.log`;
- `temp/lambda_mir_locals_head_writethrough.txt` and
  `temp/lambda_mir_locals_head_writethrough.log`;
- `temp/js_mir_locals_head_writeback.txt` and
  `temp/js_mir_locals_head_writeback.log`;
- `temp/js_mir_locals_head_writethrough.txt` and
  `temp/js_mir_locals_head_writethrough.log`.

The refresh rebuilt the current debug target successfully, ran both probes in
production write-back and write-through-oracle modes, and restored the
pre-existing release `lambda.exe` byte-for-byte afterward.
