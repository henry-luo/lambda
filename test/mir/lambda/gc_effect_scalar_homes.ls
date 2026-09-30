// D5.2.2v3/D5.3.2: scalar-home pushes do not collect GC objects. Keep a
// string live across each call so an invented safepoint would add a root.
pn gc_scalar_home_i64(x: i64) any { return x }
pn gc_scalar_home_u64(x: u64) any { return x }
pn gc_scalar_home_float(x: float) any { return x }

pn gc_scalar_use_i64(s: string, x: i64) int {
    let held = gc_scalar_home_i64(x)
    return len(s)
}

pn gc_scalar_use_u64(s: string, x: u64) int {
    let held = gc_scalar_home_u64(x)
    return len(s)
}

pn gc_scalar_use_float(s: string, x: float) int {
    let held = gc_scalar_home_float(x)
    return len(s)
}

pn main() {
    print(gc_scalar_use_i64("abc", 9223372036854775807i64)); print(" ")
    print(gc_scalar_use_u64("def", 18446744073709551615u64)); print(" ")
    print(gc_scalar_use_float("ghi", 1e-320)); print("\n")
}
