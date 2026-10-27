// u64(x) with an untyped operand publishes a boxed result: the MIR lowering
// once unboxed it under an Item carrier, and the JIT dereferenced the payload.
fn widen(p) => u64(p)
fn widen_declared(p) u64 => u64(p);
[widen(42), widen(-1), widen_declared(7), widen(41) + 1u64, widen(9u64), widen_declared(-2)]
