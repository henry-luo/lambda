// S7.7.2–S7.7.3 and S3.5: boundaries contain errors; expression consumers retain them.
// MVP-only regression: production currently loses shl's error at the annotated int binding.
pn guarded(x: int) int {
    print("unexpected callee entry\n")
    return x
}
pn rejected_local() int {
    let x: int = shl(1, -1)
    print("unexpected local tail\n")
    return x
}
pn main() {
    let failed = rejected_local()
    if (failed == 1) { print("unexpected true branch\n") }
    else { print("unexpected false branch\n") }
    print("after local\n")
    let input: any = "bad"
    let call_result = guarded(input)
    if (call_result) { print("unexpected call branch\n") }
    print("after call\n")
    if (true) {
        let inner: int = shl(1, -1)
        print("unexpected inner tail\n")
    }
    print("after block\n")
}
