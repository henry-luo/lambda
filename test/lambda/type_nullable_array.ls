// Nullable array `T[]?` (S11.1.6v2: `T?` is `T | null`): an `int[]` value or
// `null`, never a bare `int`. The `?` binds outside the whole array, so the
// inline spelling is the type an alias spells `type Vec = int[]; Vec?`, and
// it differs from `T?[]`, an array whose elements may be null (D3.1.1v2).
// `int[]?` had never parsed: the suffix chain left the `?` as trailing input.

type Vec = int[]
type MaybeVec = Vec?
type Inline = int[]?
type Rec = {a: int[]?}

fn count(a: int[]?) { if (a == null) "none" else len(a) }
fn back(x) int[]? { x }
fn kind(v) { match v { case int[]?: "maybe-ints" default: "other" } }

'1. an int[] value or null'
"1.1"; (null is int[]?)          // true - zero is null
"1.2"; ([] is int[]?)            // true - the empty array is an int[]
"1.3"; ([1, 2] is int[]?)        // true
"1.4"; (5 is int[]?)             // false - a bare int is not an array
"1.5"; ([1, null] is int[]?)     // false - the ? is not on the element
"1.6"; ("s" is int[]?)           // false

'2. the inline spelling is the alias type'
"2.1"; [null is MaybeVec, [] is MaybeVec, [1, 2] is MaybeVec, 5 is MaybeVec, [1, null] is MaybeVec]
"2.2"; [null is Inline, [] is Inline, [1, 2] is Inline, 5 is Inline, [1, null] is Inline]

'3. annotation, parameter, return, field, and match boundaries'
let x: int[]? = null
let y: int[]? = [3, 4]
let z: int[]? = []
"3.1"; [x, y, z]
"3.2"; [count(null), count([]), count([1, 2])]
"3.3"; [back(null), back([]), back([7, 8])]
"3.4"; [{a: null} is Rec, {a: [1, 2]} is Rec, {a: 5} is Rec]
"3.5"; [kind(null), kind([]), kind([1, 2]), kind(5)]

'4. int?[] is still an array of nullable ints'
"4.1"; (null is int?[])          // false - the array itself is not nullable
"4.2"; ([] is int?[])            // true
"4.3"; ([1, null] is int?[])     // true - the ? is on the element
"4.4"; ([null] is int?[])        // true
"4.5"; (5 is int?[])             // false
"4.6"; ([1, 2] is int?[])        // true - no element has to be null
"4.7"; [[1] is int?[], (1 to 3) is int?[], [1.5, 2.5] is float?[], ["a"] is string?[]]
"4.8"; [[1, 2] is (int | null)[], [1, "a"] is (int | null)[], [[1]] is int?[]]

'5. suffixes compose left to right'
"5.1"; [null is int?[]?, [1, null] is int?[]?, 5 is int?[]?]     // nullable array of nullable ints
"5.2"; [[[1], null] is int[]?[], null is int[]?[]]               // array of nullable int arrays
"5.3"; [null is int[2]?, [1, 2] is int[2]?, [1] is int[2]?]      // counted array or null
"5.4"; [[1] is int[]? | string, "s" is int[]? | string, 5 is int[]? | string]

'6. a T?[] binding holds its elements, whatever lane carries them'
// Admission into `T?[]` publishes a native lane (D3.2.6) whose slots are lane
// words, not Items. Printing one crashed, `==`/`in`/`++` read raw words, and
// converting it into `i8[]` or `float[]` was refused although the same values
// in a plain array are admitted.
let e: int?[] = [1, 2]
let f: int?[] = [1, null]
let g: (int | null)[] = [1, 2]
let h: float?[] = [1.5, null]
let s: string?[] = ["a", null]
"6.1"; [e, f, g, h, s]
"6.2"; [e == [1, 2], f == [1, null], 2 in e, null in f, e ++ [3], [*f, *e]]
"6.3"; [sum(e), min(e), max(e), -e, len(s), s[1]]
let small: i8[] = e
let wide: float[] = e
"6.4"; [small, wide, type(wide[0])]
