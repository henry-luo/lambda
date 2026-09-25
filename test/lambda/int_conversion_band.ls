// S4.1.1 (v5): int() keeps every value in the int53 band exact, and inf, -inf
// and nan pass through, since they are int values themselves (S4.2.2). The
// interpreter had parsed text into an int32 and returned in-band floats beyond
// the int32 range as floats (LR04-9).
let text = [int("3000000000"), int("9007199254740991"), int("-9007199254740991")]
let floats = [int(1e10), type(int(1e10)), int(-2.9), int(9007199254740991.0)]
let poison = [int(nan), int(inf), int(-inf), type(int(nan))]
let wide = [int(i64(5000000000)), int(u32(4000000000)), int(5000000000.7m), type(int(i64(5000000000)))]
let r = [text, floats, poison, wide]
r
