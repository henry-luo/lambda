// Regression guard: fill(n, bool) produces a PACKED bool lane (ELEM_BOOL),
// not n boxed Items. The lane is not visible through type(), so each check
// below pins a behaviour that breaks if a different piece of the lane is
// missing. Four separate defects were found here (Result27 follow-up):
//
//   1. fn_fill had no ELEM_BOOL branch -> generic boxed Array. A declared
//      bool[] boundary then degenerated from the O(1) representation check to
//      an O(n) element walk (1M-element boundary: 728ms vs 1.97ms).
//   2. validator_array_elem_embeds had no ELEM_BOOL case -> `default: false`,
//      so a correct packed array was REJECTED by its own declared type.
//   3. fn_array_set's ELEM_BOOL branch never widened: a non-bool write was
//      coerced, silently storing `false` for a string (data loss).
//   4. convert_specialized_to_generic had no ELEM_BOOL branch; its i64-lane
//      fallback read eight packed bools per element, yielding 65537.

type BoolSeq = [bool*]
type IntSeq = [int*]

pn main() {
    // (2) a packed bool array must satisfy its own declared type
    var flags: bool[] = fill(4, true)
    print(len(flags))
    print(" ")
    print(flags[0])
    print(" ")
    print(flags[3])
    print("\n")

    // bool writes keep the packed carrier and read back exactly
    flags[1] = false
    flags[2] = false
    print(flags[0])
    print(" ")
    print(flags[1])
    print(" ")
    print(flags[2])
    print(" ")
    print(flags[3])
    print("\n")

    // false-filled arrays take the same lane
    var zeros: bool[] = fill(3, false)
    zeros[1] = true
    print(zeros[0])
    print(" ")
    print(zeros[1])
    print("\n")

    // (3)+(4) an UNTYPED fill(n, bool) is widenable: the non-bool write must
    // widen instead of coercing, and widening must preserve the earlier bools
    // as bools (not as integers read at the wrong stride)
    var mixed = fill(3, true)
    mixed[1] = false
    mixed[2] = "mixed"
    print(mixed[0])
    print(" ")
    print(mixed[1])
    print(" ")
    print(mixed[2])
    print("\n")

    // a packed bool lane is deliberately NOT numeric: it satisfies bool only
    print((flags is BoolSeq))
    print(" ")
    print((flags is IntSeq))
    print("\n")

    // sized lanes still behave: int and float fills are unaffected
    var ints: int[] = fill(3, 7)
    var floats: float[] = fill(3, 1.5)
    print(ints[2])
    print(" ")
    print(floats[2])
    print("\n")
}
