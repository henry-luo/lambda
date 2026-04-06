# Lambda Type Packing: Safety of High-Byte Tagging on 64-bit OSes

## Background

Lambda's `Item` type is a 64-bit value that dual-encodes both scalars and container pointers.
Scalar Items pack a `TypeId` in bits 63–56 (the high byte). Container Items store raw heap
pointers directly (no tag), relying on the high byte being zero for all valid user-space addresses.

The question: is it safe to assume the high 8 bits of any 64-bit heap pointer are always zero
on Windows, macOS, and Linux?

---

## x86-64

### 4-level paging / 48-bit VA (current mainstream)

- User-space virtual addresses are **canonical**: bits 63–48 must all be zero.
- High byte is **guaranteed 0** for all heap pointers. ✓

### 5-level paging / LA57 / 57-bit VA (Linux ≥4.14, Intel Ice Lake+)

- User space spans 0 to 128 PB; bits 63–57 = 0 for user space, but **bit 56 can be 1**.
- A valid allocation returning `0x00F0000000000000` has high byte `0x00`, but addresses
  in the upper portion of the 128 PB range can have bit 56 set.
- High byte is **not guaranteed 0**. ✗
- Linux opts into LA57 only when explicitly requested (`ADDR_LIMIT_57BIT` via `prctl`).
  Standard `malloc` still allocates from low address space in practice.

---

## ARM64

- Default VA width is 48 bits (bits 63–48 = 0 for user space). ✓
- **Top Byte Ignore (TBI)** is enabled on Linux ARM64 and all Apple Silicon: the MMU
  ignores bits 63–56 during address translation. The OS and hardware (PAC, MTE, ASAN)
  actively use those bits for metadata.
- In practice `malloc` on both Linux/ARM64 and macOS/AArch64 returns pointers with
  high byte = 0, but this is **not contractual** — it is a side-effect of allocator behaviour.

---

## Lambda's Actual Risk

Container `Item`s are raw pointers (the `it2map` / `it2list` macros are plain casts with no
masking). The discriminant between a container Item and a scalar Item is entirely the high byte:

- Scalar Items: high byte = `TypeId` (1–`LMD_TYPE_COUNT`, currently ≤ 30)
- Container Items: high byte expected = 0

**Collision scenario**: if a heap allocator ever returns a pointer whose high byte happens to
equal a valid `TypeId`, the runtime will misinterpret it as a scalar. The practical threshold
before this happens on 4-level paging is ~30 × 2⁵⁶ bytes of heap — astronomically far away.
On LA57 (128 PB user space) the gap is real but still requires exhausting tens of PB of heap.

**Near-term risk: essentially zero.** Allocators pull from low virtual address space first.
**Long-term risk: non-zero** if LA57 use widens or allocators change strategy.

---

## Prior Art

| Engine | Approach | Documented limit |
|--------|----------|-----------------|
| LuaJIT NaN-boxing | high 17 bits = 0 for pointers | Documents 47-bit address requirement |
| V8 pointer compression | compresses heap pointers to 32 bits | Hard 4 GB heap cage |
| SpiderMonkey | high 17 bits = 0, `MOZ_ASSERT` at alloc | 47-bit assumption, checked in debug builds |

All of them make the same assumption but document and/or assert it explicitly.

---

## Recommendations

### 1. Runtime assertion at startup (minimal cost)

```c
void lambda_assert_address_model(void) {
    void* probe = malloc(16);
    if (!probe) return;
    if (((uintptr_t)probe >> 56) != 0) {
        fprintf(stderr, "FATAL: heap address 0x%016llx exceeds 56-bit range — "
                        "Lambda tagged-Item encoding unsafe on this platform\n",
                (unsigned long long)(uintptr_t)probe);
        abort();
    }
    free(probe);
}
```

Call once from `lambda_init()` or equivalent. Fails loudly on LA57 environments where
allocators start handing out high-range addresses.

### 2. Compile-time documentation (comment in `lambda.h`)

Add a block comment near the `Item` / `ITEM_NULL` macros:

```c
// Item encoding requires that all heap pointers fit within 56 bits (high byte == 0).
// This holds on x86-64 with 4-level paging (48-bit VA) and ARM64 (48-bit VA + TBI).
// On x86-64 with LA57 (57-bit VA, Linux + prctl ADDR_LIMIT_57BIT), bit 56 of a
// user-space pointer can be 1; this encoding would be unsafe in that configuration.
// lambda_assert_address_model() guards against this at runtime.
```

### 3. Future-proofing: use `type_id` field for container disambiguation (optional)

Every container struct (`List`, `Array`, `Map`, `Element`, `Object`, `Range`) already
carries a `TypeId type_id` field at offset 0. If the high-byte-zero assumption ever
becomes untenable, container Items could be discriminated by reading `((Container*)ptr)->type_id`
instead of the Item's high byte. This would require a pointer-vs-scalar predicate change
throughout the runtime but would make the encoding fully address-space safe.

---

## Conclusion

The high-byte-zero assumption is **pragmatically safe** on all current mainstream
configurations (Windows/Linux/macOS, x86-64 4-level paging, ARM64). It is the same
assumption made by LuaJIT, V8, and SpiderMonkey. The primary exposure is LA57 on Linux,
which is opt-in and not yet widely deployed. A startup assertion is a low-cost way to
guarantee a loud failure rather than silent corruption if the assumption is ever violated.
