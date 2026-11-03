// S7.9.3/S7.5.4: sized conversions preserve errors through their receiver.
fn opaque(value) => value
fn propagate_conversion() u64^ => u64(error("early"))^
let source = error("source")
let unsigned = u64(source)
let signed = i32(source)
let floating = f32(source)
let invalid = f32(opaque("bad"))
let early = propagate_conversion()
let recovered = u64(error("recovery")) ^ { 7 };
[unsigned.message, signed.message, floating.message, invalid is error, early.message, recovered]
