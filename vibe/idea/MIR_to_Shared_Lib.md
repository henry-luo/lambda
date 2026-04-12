# MIR to Shared Library Compilation

## Motivation

MIR (Medium Internal Representation) is a lightweight JIT compiler that produces native machine code in memory via `MIR_gen()`. However, it has no built-in support for emitting shared libraries (`.so`, `.dylib`, `.dll`). This document explores how to bridge that gap using libraries like [LIEF](https://lief-project.github.io/) to package JIT-compiled machine code into shared libraries for AOT distribution.

---

## Background: What MIR Provides

After `MIR_gen()` compiles a function, the native code lives in executable memory:

```c
MIR_gen(ctx, func_item);
void *code_ptr = func_item->u.func->machine_code;
```

MIR also offers:

- `MIR_write_module()` — serializes MIR *IR* (not native code) to a binary format
- `mir2c` — decompiles MIR IR back to C source
- `_MIR_publish_code()` — maps raw machine code into executable pages

None of these produce a shared library.

---

## Approaches

### 1. LIEF: Wrap JIT Output in a Shared Library (Preferred)

Use [LIEF](https://lief-project.github.io/) to programmatically construct an ELF/Mach-O/PE shared library containing the raw machine code bytes from `MIR_gen()`.

#### Pipeline

```python
import lief

# 1. Create a bare binary for the target platform
if platform == "linux":
    binary = lief.ELF.Binary("lambda_module", lief.ELF.ELF_CLASS.CLASS64)
elif platform == "macos":
    binary = lief.MachO.Binary(...)
elif platform == "windows":
    binary = lief.PE.Binary("lambda_module", lief.PE.PE_TYPE.PE32_PLUS)

# 2. Create a .text section with the raw machine code bytes
code_bytes = read_from_mir(code_ptr, code_len)
section = lief.ELF.Section(".text")
section.content = list(code_bytes)
section.type = lief.ELF.SECTION_TYPES.PROGBITS
section.flags = lief.ELF.SECTION_FLAGS.ALLOC | lief.ELF.SECTION_FLAGS.EXECINSTR
binary.add(section)

# 3. Add exported symbol(s)
symbol = lief.ELF.Symbol()
symbol.name = "my_function"
symbol.value = 0  # offset within .text
symbol.size = code_len
symbol.binding = lief.ELF.SYMBOL_BINDINGS.GLOBAL
symbol.type = lief.ELF.SYMBOL_TYPES.FUNC
binary.add_dynamic_symbol(symbol)

# 4. Write shared library
binary.write("lambda_module.so")
```

LIEF handles ELF/PE/Mach-O headers, `PT_LOAD` segments, and dynamic linking tables.

### 2. `mir2c` → System Compiler (Simpler, AOT)

1. Generate MIR IR
2. Convert to C via `mir2c`
3. Compile with `cc -shared -o output.so output.c`

Simple but loses JIT speed advantages — purely an AOT workflow.

### 3. Manual Binary Construction

Use `_MIR_publish_code()` to accumulate generated functions, build an export table mapping symbol names to code addresses, and write the platform-specific shared library format to disk manually. More work than LIEF for no real benefit.

---

## Key Challenges

### 1. Code Size Not Exposed

MIR does not expose the generated machine code length via its public API. The `machine_code` pointer exists but there's no corresponding `machine_code_len`.

**Solutions:**
- Patch MIR to expose it (add `MIR_gen_code_len()` or a `code_len` field on `MIR_func_t`)
- Track it via internal `code_len` in codegen structures
- Disassemble forward from the pointer to find the `ret` instruction (fragile, not recommended)

### 2. Position-Independent Code (PIC)

MIR's x86-64 codegen uses RIP-relative addressing for most things, so code is *mostly* position-independent. ARM64 codegen uses PC-relative addressing too. However, absolute addresses to `func_list[]` entries are embedded as immediates — these break when the code is relocated to a different address in a shared library.

### 3. External Call Relocations (The Hard Part)

JIT code hardcodes absolute addresses for calls to C runtime functions (from `func_list[]`). In a shared library, these must become GOT/PLT relocations.

MIR's codegen emits something like:

```asm
; x86-64: call to external C function (e.g., printf)
movabs rax, 0x7f1234567890   ; absolute address baked in by JIT
call rax
```

For a shared library, this needs to become:

```asm
call [rip + printf@GOTPCREL]  ; GOT-indirect, relocatable
```

**Two strategies:**

1. **Post-process the machine code**: Scan for `movabs + call` patterns, replace with GOT-indirect calls, and add LIEF relocation entries. Doable but brittle and architecture-specific.

2. **Modify MIR's codegen**: Add a `MIR_GEN_PIC` flag that makes `gen_call` emit `[rip + offset]` instead of absolute addresses, and collect relocation records during codegen. Cleaner but requires upstream changes.

### 4. Multiple Functions

When bundling multiple functions, lay them out sequentially in `.text` and track each symbol's offset within the section.

### 5. Data Sections

If MIR code references constant pools or global data, a `.rodata` or `.data` section is needed in the shared library alongside `.text`.

### 6. Platform Differences

DLL (PE), `.so` (ELF), and `.dylib` (Mach-O) formats differ significantly. LIEF abstracts most of this, but per-platform testing is required.

---

## Difficulty Assessment

| Scenario | Difficulty | Notes |
|----------|-----------|-------|
| Self-contained functions (no external calls) | **Easy** | LIEF + raw bytes + symbols. Works today. |
| Functions calling other MIR functions in same module | **Medium** | Need to resolve inter-function offsets within `.text` |
| Functions calling C runtime (`printf`, `malloc`, etc.) | **Hard** | Need relocation rewriting or MIR codegen changes |

---

## Recommended Path for Lambda

### Phase 1: Self-Contained Functions

Target pure computational functions (layout engine calculations, math, data transforms) that make no external calls. These can be packaged into a shared library with zero MIR modifications:

1. `MIR_gen()` all functions in a module
2. Copy machine code bytes into a LIEF binary
3. Add export symbols with correct offsets
4. Write `.so` / `.dylib` / `.dll`

### Phase 2: Inter-Module Calls

Allow MIR-compiled functions to call each other within the shared library by computing relative offsets during layout.

### Phase 3: External Call Support

Either:
- Modify MIR's codegen to support a PIC mode (`MIR_GEN_PIC`) that emits GOT-indirect calls and collects relocation metadata
- Or build a post-processing pass that rewrites absolute call patterns and adds relocation entries to the LIEF binary

---

## Required MIR Patches

| Patch | Purpose | Complexity |
|-------|---------|-----------|
| Expose `code_len` on `MIR_func_t` | Know how many bytes to copy | Trivial |
| Add `MIR_GEN_PIC` codegen flag | Emit position-independent external calls | Medium |
| Collect relocation records during codegen | Feed into LIEF relocation table | Medium |

---

## References

- [MIR Project](https://github.com/vnmakarov/mir) — Vladimir Makarov's lightweight JIT
- [LIEF](https://lief-project.github.io/) — Library to Instrument Executable Formats
- [MIR_JIT.md](../../MIR_JIT.md) — MIR SIMD and assembly interop notes
