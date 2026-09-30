// S5.1.1–S5.1.2: packed int equality is a word comparison only after
// both Item tags are proven; cross-family numeric values need fn_eq.
pn one(x: any) bool {
    return x == 1
}

pn not_one(x: any) bool {
    return x != 1
}

pn main() {
    print([one(1), one(2), one(1.0), one(1i64), one(1u32),
        one(nan), one(inf), one(null), one(shl(1u32, -1))])
    print("\n")
    print([not_one(1), not_one(2), not_one(1.0), not_one(1i64),
        not_one(1u32), not_one(nan), not_one(inf), not_one(null),
        not_one(shl(1u32, -1))])
    print("\n")
}
