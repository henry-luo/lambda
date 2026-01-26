# Lambda Stack Trace Implementation

**Author**: Lambda Development Team  
**Date**: January 2026  
**Status**: Research / Proposal  

## Overview

This document details the research and proposed implementation for Lambda's runtime stack trace feature. Stack traces are essential for debugging runtime errors, showing the call chain that led to an error.

**Related Document**: See [Lambda_Error_Handling.md](Lambda_Error_Handling.md) for the complete error handling system design.

---

## 1. Requirements

### 1.1 Goals

- Show Lambda function names in stack traces (not mangled C names)
- Display source file and line numbers for each call site
- Zero or minimal overhead during normal execution
- Work correctly with MIR JIT-compiled code

### 1.2 Example Output

```
error[E302]: index out of bounds
    |
 78 |     let item = arr[10]
    |                ^^^^^^^ index 10 out of bounds for array of length 5

Stack trace:
  0: at level3 (script.ls:78:17)
  1: at level2 (script.ls:52:9)
  2: at level1 (script.ls:35:5)
  3: at <main> (script.ls:12:1)
```

---

## 2. Background: Why Native `backtrace()` Doesn't Work

### 2.1 Initial Assumption

The original design assumed we could use native stack walking APIs:
- `backtrace()` on macOS/Linux
- `CaptureStackBackTrace()` on Windows

The plan was to:
1. Build a debug info table mapping native addresses to Lambda source locations
2. On error, call `backtrace()` to get native addresses
3. Look up each address in the debug table to get Lambda function names

### 2.2 What We Found

**Testing revealed that `backtrace()` cannot reliably walk through MIR JIT-generated code on macOS ARM64 (Apple Silicon).**

When testing with a simple call chain:
```lambda
fn level3() { let x = null; x() }  // error here
fn level2() { level3() }
fn level1() { level2() }
level1()
```

The `backtrace()` output only showed:
- `main` (the JIT entry point)
- C++ runtime frames (`execute_script_and_create_output`, etc.)

The intermediate Lambda functions (`level1`, `level2`, `level3`) were **not captured**.

### 2.3 Root Cause Analysis

MIR JIT generates native machine code, but `backtrace()` on macOS ARM64 uses a specific stack unwinding mechanism that may not work with JIT code because:

1. **No DWARF unwind info**: MIR doesn't generate `.eh_frame` or DWARF unwind tables that `backtrace()` relies on for accurate stack walking.

2. **Frame pointer optimization**: While MIR does set up frame pointers (FP/X29 on ARM64), the macOS `backtrace()` implementation may use other mechanisms.

3. **JIT code region**: The JIT-allocated memory region is not registered with the system's stack unwinder.

---

## 3. MIR Call Frame Architecture (Deep Analysis)

### 3.1 MIR ARM64 Stack Layout

From analyzing `mir-gen-aarch64.c`, MIR uses a **consistent frame pointer chain** on ARM64:

```
Stack layout (from higher to lower address):

   | ...              |  caller's stack frame
   |------------------|
   | LR (x30)         |  saved return address, at [FP + 8]
   |------------------|
   | old FP (x29)     |  saved frame pointer → POINTS TO CALLER'S FP
   |------------------|  <-- FP (x29) points here after prologue
   | saved regs       |  callee-saved registers (x19-x28, v8-v15)
   |------------------|
   | local variables  |  stack slots for locals
   |------------------|  <-- SP (x31) points here
```

**Key insight**: MIR saves `LR` at `[FP+8]` and `old FP` at `[FP+0]`, forming a linked list.

### 3.2 MIR Prologue Code Generation

From `target_make_prolog_epilog()` in MIR:

```c
// Prologue generation (simplified):
gen_mov(ctx, MIR_MOV, [SP, #0], FP);          // save old FP at SP
gen_mov(ctx, MIR_MOV, [SP, #8], LR);          // save LR at SP+8
gen_mov(ctx, MIR_MOV, FP, SP);                // FP = SP (establish frame)
// ... then SUB SP for locals ...
```

This translates to ARM64:
```asm
; Standard MIR prologue
sub    sp, sp, #frame_size     ; allocate stack (may be multiple insns)
str    x29, [sp]               ; save FP
str    x30, [sp, #8]           ; save LR
mov    x29, sp                 ; establish frame pointer
; ... or combined as STP:
stp    x29, x30, [sp, #-N]!    ; pre-decrement: save FP/LR, alloc N bytes
mov    x29, sp                 ; FP = SP
```

### 3.3 Manual Frame Pointer Walking

Since MIR maintains proper frame pointer chains, we CAN walk the stack manually:

```c
// ARM64: Read current frame pointer
static inline void* get_frame_pointer(void) {
    void* fp;
    __asm__ volatile("mov %0, x29" : "=r"(fp));
    return fp;
}

// Walk the frame pointer chain
StackFrame* walk_mir_frames(void* debug_info) {
    StackFrame* head = NULL;
    StackFrame** tail = &head;
    
    void** fp = (void**)get_frame_pointer();
    
    while (fp != NULL && (uintptr_t)fp > 0x1000) {  // sanity check
        void* return_addr = fp[1];  // LR is at FP+8
        
        // Look up return address in debug info table
        FuncDebugInfo* info = lookup_debug_info(debug_info, return_addr);
        if (info) {
            StackFrame* frame = calloc(1, sizeof(StackFrame));
            frame->function_name = info->lambda_func_name;
            frame->location.file = info->source_file;
            frame->location.line = info->source_line;
            
            *tail = frame;
            tail = &frame->next;
        }
        
        // Follow chain: old FP is at FP+0
        fp = (void**)*fp;
    }
    
    return head;
}
```

### 3.4 Why `backtrace()` Still Fails

Even though MIR creates valid frame pointer chains, `backtrace()` on macOS ARM64 may not use them:

1. **DWARF-based unwinding**: macOS prefers `.eh_frame` / DWARF compact unwind tables
2. **Code signing**: JIT code pages may not be recognized by the system unwinder
3. **Frame pointer trust**: The unwinder may not trust FP chains in unknown memory regions

### 3.5 Solution: Build Our Own Walker

**The solution is to walk the frame pointer chain ourselves**, bypassing `backtrace()`:

1. **At JIT compile time**: Build debug info table mapping addresses to Lambda function names
2. **On error**: Read current FP register
3. **Walk the chain**: Follow FP → old FP links
4. **Look up addresses**: Use debug table to resolve return addresses to Lambda names

This approach:
- Works with any code that maintains FP chains (MIR does!)
- Doesn't require DWARF info
- Is simple and efficient

---

## 4. Recommended Solution: Manual Frame Pointer Walking

Based on the MIR analysis above, **manual FP chain walking** is the recommended approach.

### 4.1 Design Overview

```
┌─────────────────────────────────────────────────────────────────┐
│                    Stack Trace Capture Flow                     │
├─────────────────────────────────────────────────────────────────┤
│                                                                 │
│   1. JIT Compile Time: Build Debug Info Table                   │
│      ┌────────────────────────────────────────┐                 │
│      │ Address Range  │ Lambda Function Name │                 │
│      ├────────────────┼──────────────────────┤                 │
│      │ 0x1000-0x1100  │ "level3"             │                 │
│      │ 0x1100-0x1200  │ "level2"             │                 │
│      │ 0x1200-0x1400  │ "level1"             │                 │
│      │ 0x1400-0x1600  │ "main_expr"          │                 │
│      └────────────────┴──────────────────────┘                 │
│                                                                 │
│   2. On Error: Walk Frame Pointer Chain                         │
│                                                                 │
│      Current FP ──► [old FP] ──► [old FP] ──► ...              │
│                     [LR=0x1050]  [LR=0x1150]                   │
│                         │            │                          │
│                         ▼            ▼                          │
│                     "level3"    "level2"                        │
│                                                                 │
└─────────────────────────────────────────────────────────────────┘
```

### 4.2 Implementation Components

#### 4.2.1 Debug Info Table (already implemented in mir.c)

The `build_debug_info_table()` function in `mir.c` already builds this:

```c
// Already exists in mir.c
void* build_debug_info_table(void* mir_ctx);
```

It collects all MIR function addresses and computes their boundaries using address ordering.

#### 4.2.2 Frame Pointer Walker

New code to add to `lambda_error.cpp`:

```c
#if defined(__aarch64__) || defined(_M_ARM64)

// Read current frame pointer (ARM64)
static inline void* get_current_fp(void) {
    void* fp;
    __asm__ volatile("mov %0, x29" : "=r"(fp));
    return fp;
}

// Walk frame pointer chain and build stack trace
StackFrame* capture_mir_stack_trace(void* debug_info_table) {
    if (!debug_info_table) return NULL;
    
    StackFrame* head = NULL;
    StackFrame** tail = &head;
    int depth = 0;
    const int MAX_DEPTH = 64;
    
    void** fp = (void**)get_current_fp();
    
    while (fp != NULL && depth < MAX_DEPTH) {
        // Sanity check: FP should be in valid stack range
        if ((uintptr_t)fp < 0x1000 || ((uintptr_t)fp & 0x7) != 0) {
            break;  // Invalid or misaligned FP
        }
        
        // Read return address (LR is stored at FP+8 on ARM64)
        void* return_addr = fp[1];
        
        // Look up in debug info table
        FuncDebugInfo* info = lookup_debug_info(debug_info_table, return_addr);
        if (info && info->lambda_func_name) {
            StackFrame* frame = (StackFrame*)calloc(1, sizeof(StackFrame));
            if (frame) {
                frame->function_name = info->lambda_func_name;
                frame->location.file = info->source_file;
                frame->location.line = info->source_line;
                frame->next = NULL;
                
                *tail = frame;
                tail = &frame->next;
                depth++;
            }
        }
        
        // Follow chain: old FP is stored at FP+0
        void* old_fp = *fp;
        
        // Detect infinite loops or backwards chain
        if ((uintptr_t)old_fp <= (uintptr_t)fp) {
            break;
        }
        
        fp = (void**)old_fp;
    }
    
    return head;
}

#elif defined(__x86_64__) || defined(_M_X64)

// x86-64 version (similar but RBP-based)
static inline void* get_current_fp(void) {
    void* fp;
    __asm__ volatile("mov %%rbp, %0" : "=r"(fp));
    return fp;
}

// ... similar implementation using [RBP+0] for old RBP, [RBP+8] for return addr

#endif
```

#### 4.2.3 Integration with Error System

```c
// In set_runtime_error() or when error is raised:
void capture_stack_trace_into_error(LambdaError* err, void* debug_info) {
    err->stack_trace = capture_mir_stack_trace(debug_info);
}
```

### 4.3 Advantages of This Approach

| Aspect | Frame Pointer Walking | Shadow Call Stack |
|--------|----------------------|-------------------|
| Transpiler changes | None required | Need instrumentation |
| Runtime overhead | Zero during execution | Push/pop on every call |
| Completeness | All MIR functions | Only instrumented calls |
| Memory | Zero (uses existing stack) | 6KB per thread |
| Direct calls | ✓ Captured | ✗ Not captured (unless instrumented) |

### 4.4 Limitations

1. **Requires FP preservation**: MIR does this, but `-fomit-frame-pointer` would break it
2. **C runtime frames**: We'll see Lambda functions but not intermediate C++ calls
3. **Inline functions**: If MIR inlines a function, it won't appear in trace

### 4.5 Fallback for Closure Calls

For closure calls through `fn_call*()`, we can optionally track them as a secondary source:

```c
// In fn_call0/fn_call1/etc. (already has function metadata)
Item fn_call0(Function* fn) {
    // Optional: maintain a small shadow stack just for closures
    // This catches cases where FP chain doesn't have the info
    closure_call_push(fn->name, fn->source_file, fn->source_line);
    Item result = call_function(fn);
    closure_call_pop();
    return result;
}
```

This is complementary, not required. The FP chain should capture most cases.

---

## 5. Implementation Plan

### Phase 1: Debug Info Table Enhancement
- [ ] Verify `build_debug_info_table()` captures all function addresses
- [ ] Add source file/line tracking to debug info (if not already present)
- [ ] Expose `lookup_debug_info()` API

### Phase 2: Frame Pointer Walker
- [ ] Implement `get_current_fp()` for ARM64
- [ ] Implement `capture_mir_stack_trace()` 
- [ ] Add bounds checking and safety guards
- [ ] Test with nested Lambda function calls

### Phase 3: Error System Integration
- [ ] Store debug info table pointer in runtime context
- [ ] Call `capture_mir_stack_trace()` when error is raised
- [ ] Format stack trace for display

### Phase 4: x86-64 Support
- [ ] Implement `get_current_fp()` for x86-64
- [ ] Verify MIR x86-64 also maintains frame pointers

---

## 6. Alternatives Considered (and Rejected)

### 6.1 Shadow Call Stack with Transpiler Instrumentation

**Rejected because:**
- Requires modifying transpiler for every function
- Runtime overhead on every call
- Complex to maintain with all exit paths

### 6.2 `backtrace()` / libunwind

**Rejected because:**
- Requires DWARF/eh_frame info that MIR JIT doesn't generate
- macOS code signing issues with JIT memory
- Would need to synthesize DWARF info (very complex)

### 6.3 MIR Interpreter Mode

**Rejected because:**
- 10-100x slower than JIT
- Defeats the purpose of using MIR

---

## 7. Conclusion

**Manual frame pointer walking** is the optimal solution because:

1. **MIR already maintains FP chains** - no runtime changes needed
2. **Zero overhead during normal execution** - only cost is when error occurs  
3. **Debug info table already exists** - just need to expose it for stack walking
4. **Works with direct calls** - doesn't require instrumenting every function

The implementation is straightforward: read x29, walk the chain, look up addresses.
