// Array construction preserves each element's literal type and precision;
// array arithmetic promotes exactly as scalar arithmetic does.
// Lambda_Typed_Array.md §4.1-§4.3 (construction), §7.2 (arithmetic).

'=== 1-D: only one exact element type packs ==='
let fi = [1, 2.5]
fi
type(fi[0])                        // int, not re-typed to float
type(fi[1])
let ii = [1, 2i64]
type(ii[0])                        // int, not re-typed to i64
type(ii[1])
let big = 9007199254740993i64
let bf = [big, 2.5]
bf                                 // the i64 keeps every digit
type(bf[0])

'=== N-D: rows of different types stay nested ==='
let nd = [[big, 1i64], [2.5, 3.5]]
nd
type(nd[0][0])                     // i64, not rounded into a float lane
type(nd[1][0])
let r1 = [big, 1i64]
let r2 = [2.5, 3.5]
let dyn = [r1, r2]
dyn
type(dyn[0][0])
let ir = [1, 2, 3]
let fr = [4.5, 5.5, 6.5]
let mixed = [ir, fr]
type(mixed[0][0])
type(mixed[1][0])

'=== N-D: rows of one type still form a tensor ==='
let h = [[1, 2], [3, 4]]
ndim(h)
shape(h)
let hd = [ir, [7, 8, 9]]
ndim(hd)
shape(hd)
let hf = [fr, [7.5, 8.5, 9.5]]
shape(hf)

'=== comprehension results keep their element types ==='
let c = [for (x in [1, 2, 3]) if (x == 2) 2.5 else x]
c
type(c[0])
type(c[1])

'=== an i64 or u64 in a generic container stays typed at any magnitude ==='
let small = 2i64
type([small, "a"][0])
type((small, "a")[0])
let su = 3u64
type([su, "a"][0])
fn get_small() => small
type(get_small())

'=== arithmetic follows scalar promotion per element ==='
let p = fi + 1
p
type(p[0])                         // int + int -> int
type(p[1])                         // float + int -> float
let q = [1, 2] + [0.5, 0.5]
q
type(q[0])                         // int + float -> float
let w = [255u8, 1u8] + [1u8, 2u8]
w                                  // same sized lane wraps
type(w[0])
let s = [big] + 0.5
type(s[0])                         // bounded integer meets float in float

'=== reductions widen narrow lanes; mean is float ==='
let u = [200u8, 100u8]
sum(u)                             // 300: a narrow lane accumulates in int
type(sum(u))
math.prod([20u8, 20u8])            // 400
math.cumsum(u)                     // [200, 300]
type(math.cumsum(u)[1])
let um = [[200u8, 100u8], [100u8, 100u8]]
sum(um, 0)                         // [300, 200]
sum([100i8, 100i8])                // 200
sum([1, 2.5])                      // 3.5: int + float
avg(u)                             // 150: a mean accumulates in float
type(avg(u))
sum([9223372036854775807i64, 1i64]) // i64 keeps its lane and wraps
math.mean([1i64, 2i64])            // 1.5: float, not decimal
math.dot([200u8, 1u8], [2u8, 1u8]) // 145: dot follows NumPy, wraps in the lane
let mm = matmul([[200u8, 1u8], [1u8, 1u8]], [[2u8, 0u8], [1u8, 1u8]])
mm                                 // [[145, 1], [3, 1]]: matmul wraps in u8
type(mm[0][0])
type(matmul([[100i8, 1i8]], [[2u8], [1u8]])[0][0])  // i16: the common lane
type(matmul([[1, 2]], [[3], [4]])[0][0])            // int, not i64
type(matmul([[1.5f32]], [[2.0f32]])[0][0])          // f32
max(u)                             // 200: min/max keep the lane
type(max(u))
