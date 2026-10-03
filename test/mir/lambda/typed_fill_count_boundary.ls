// S7.7.2-S7.7.3/D3.3.3v3: destination construction retains fill's count gate.
pn build(count) int[] {
    var values: int[] = fill(count, 0)
    return values
}

pn take(values: int[]) int {
    return len(values)
}

pn call_fill(count) {
    return take(fill(count, 0))
}

pn native_fill(count: int) int[] { return fill(count, 4) }

pn main() {
    print([len(build(0)), len(build(0.0)), build(-1) is error,
        build(0.5) is error, build(null) is error])
    print("\n")
    print([call_fill(0), call_fill(0.0), call_fill(-1) is error,
        call_fill(0.5) is error, call_fill(null) is error])
    print("\n")
    // a failing producer is the call value; subsequent expressions still run.
    var failed = take(fill(-1, 0))
    print([failed is error, 7])
    print("\n")
    print([native_fill(0), native_fill(2), native_fill(-1) is error,
        native_fill(int(inf)) is error, native_fill(int(nan)) is error])
    print("\n")
}
