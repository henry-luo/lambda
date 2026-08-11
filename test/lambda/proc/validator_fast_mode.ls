// Regression guard: the validator's allocation-free FAST mode must return the
// same verdict as the reporting FULL mode for every converted kind.
//
// Runtime type checking (`is`, declared boundaries) is a predicate, so it runs
// the fast mode: no ValidationResult, no ValidationError, no merge_errors deep
// copies, and it may short-circuit — the list walk stops at the first bad
// element and a union stops at the first matching member. Full mode may never
// short-circuit, because it owes an error for every failing path.
//
// Two modes means two ways to drift apart. Each line below pins a verdict that
// the fast prologues compute independently of the reporting walk:
//   stage 1  occurrence  — ArrayNum O(1) embed check, and the generic element walk
//   stage 2  base type   — any / delegating kinds / numeric embedding / nominal
//   stage 3  map+element — shape_entries_match_fast field rules
//   stage 4  union       — first-match short-circuit, no min_errors scoring
//
// Every expected value here was cross-checked against a pre-fast-mode build.

type Pt = {x: int, y: int}
type Named = {name: string, tags: [string*]}
type IntSeq = [int*]
type IntPlus = [int+]
type StrSeq = [string*]
type Mixed = int | string
type OptInt = [int?]

pn main() {
    // stage 3: map fields — exact, missing field, wrong field type, extra field
    print(({x: 1, y: 2} is Pt))
    print(" ")
    print(({x: 1} is Pt))
    print(" ")
    print(({x: 1, y: "s"} is Pt))
    print(" ")
    print(({x: 1, y: 2, z: 3} is Pt))
    print("\n")

    // stage 1: occurrence walks — homogeneous, heterogeneous, empty, min-count
    print(([1, 2, 3] is IntSeq))
    print(" ")
    print(([1, "a"] is IntSeq))
    print(" ")
    print(([] is IntSeq))
    print(" ")
    print(([] is IntPlus))
    print("\n")

    // stage 1: the failing element is found wherever it sits (short-circuit
    // must not change the verdict, only the work done to reach it)
    print((["a", "b", "c"] is StrSeq))
    print(" ")
    print((["a", "b", 3] is StrSeq))
    print(" ")
    print(([1, "b", "c"] is StrSeq))
    print("\n")

    // stage 4: union — first member, later member, no member
    print((1 is Mixed))
    print(" ")
    print(("s" is Mixed))
    print(" ")
    print((true is Mixed))
    print("\n")

    // stage 2: base types — nominal match, numeric embedding, null, optional
    print((1 is int))
    print(" ")
    print(("x" is string))
    print(" ")
    print((null is int))
    print(" ")
    print(([] is OptInt))
    print("\n")

    // nesting: map whose field is an occurrence (stage 3 -> stage 1 -> stage 2)
    print(({name: "a", tags: ["x", "y"]} is Named))
    print(" ")
    print(({name: "a", tags: [1]} is Named))
    print(" ")
    print(({name: 1, tags: ["x"]} is Named))
    print("\n")

    // a packed numeric array takes the O(1) representation path, a generic
    // array of the same values takes the element walk; both must agree
    var packed: int[] = fill(3, 7)
    print((packed is IntSeq))
    print(" ")
    print(([7, 7, 7] is IntSeq))
    print("\n")
}
