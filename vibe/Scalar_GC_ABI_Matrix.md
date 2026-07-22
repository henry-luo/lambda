# Scalar GC Return-Home ABI Matrix

This is the live migration checklist for the no-scalar-cell invariant.  A
pointer-backed `INT64`, `UINT64`, or non-inline `FLOAT` may leave an activation
only through the result home supplied by its ultimate caller.

| Boundary | Current result ABI | Current scalar persistence | Required end state |
| --- | --- | --- | --- |
| Lambda MIR direct body | `Item`, internal caller-home lane | `em_adopt_scalar_item` | Already caller-donated. |
| Lambda `_b` public wrapper | boxed `Item`, trailing `uint64_t*` | adopts into the donated home | Complete. |
| Lambda dynamic dispatch | `fn_call_into`, `fn_call0..3_into`, `fn_call_boxed_N_into` | forwards the exact donated home | Complete. |
| JS generated body | boxed `Item`, caller-home lane | adopts before watermark restore | Complete. |
| JS native dispatch/callbacks | `js_call_function_into` and callback invokers | forwards or terminally consumes a donated home | Complete. |
| Python MIR body | boxed `Item`, trailing `uint64_t*` | adopts before watermark restore | Complete. |
| Python hosted callbacks | Jube-hosted `Item` | callback results use a local only for terminal consumption or an owned array slot | Complete. |
| Isolated JS execution | `transpile_js_*` result plus `uint64_t* result_home` | `lambda_item_adopt_scalar_home` runs before `context = old_context` | Complete for the two one-shot context exits; every caller in `main`, TypeScript, and Radiant supplies a home that outlives its use. |
| Jube host persistence | raw Item storage | `item_slots_store/load` | Complete: destination-owned payload plus rematerializing read. |
| C2MIR | legacy direct return | frame-scan lifetime | Frozen; no ABI change. |

Rules for each row:

1. An intermediate C frame forwards the caller's exact `result_home`; it may
   not return an Item backed by its own local storage.
2. A terminal C caller may use `LAMBDA_SCALAR_HOME` only when it unboxes,
   discards, or stores the result in destination-owned storage before return.
3. Context restoration occurs only after the result has been adopted into the
   caller-owned home.
