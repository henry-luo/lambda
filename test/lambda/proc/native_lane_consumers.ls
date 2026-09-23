// Native-lane arrays (D3.2.6): admission into a nullable scalar or pointer
// element contract -- `int?[]`, `float?[]`, `bool?[]`, `string?[]` -- publishes
// a native lane whose slots are lane words, not Items. These consumers read the
// slots as Items, so an admitted array crashed (printing `let e: int?[] =
// [1, 2]` dereferenced the int `1`) or answered as if it held raw words. Every
// line prints what the same values print in a plain array, on every tier.

pn main() {
    let I: int?[] = [3, null, 1]
    let F: float?[] = [1.5, null, 2.5]
    let B: bool?[] = [true, null, false]
    let S: string?[] = ["b", null, "a"]
    let N: int?[] = [4, 2]
    let T: string?[] = ["x", "y"]

    // printing and formatting
    print([I, F, B, S])
    print("\n")
    print(string(S) ++ " " ++ format(I, 'json'))
    print("\n")

    // equality, hashing, membership, search
    print([I == [3, null, 1], F == [1.5, null, 2.5], B == [true, null, false], S == ["b", null, "a"], I == N])
    print("\n")
    print([1 in I, null in S, "a" in S, false in B, contains(F, 2.5), index_of(I, 1), index_of(S, "a")])
    print("\n")
    print(len(unique([I, [3, null, 1], S, ["b", null, "a"]])))
    print("\n")

    // composition keeps every element, and a lane is never folded into a tensor
    print([I ++ [7], [*S, "c"], [I, N], [*N, *I], join(T, "-")])
    print("\n")

    // numeric folds and element-wise arithmetic; a null element propagates
    print([sum(N), avg(N), min(N), max(N), -N])
    print("\n")
    print([sum(I), min(I), min(F), max(F), avg(F, skip_null: true)])
    print("\n")

    // queries
    print([I?int, I[int], S?string])
    print("\n")

    // admission into a converting contract rebuilds the lane
    let small: i8[] = N
    let wide: float[] = N
    print([small, wide, type(wide[0])])
    print("\n")
}
