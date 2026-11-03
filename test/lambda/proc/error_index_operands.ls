// error operands propagate; invalid non-error reads remain absence (S7.1.1v3, S7.9.3).
pn select(base: any, key: any) any { return base[key] }
fn failed(value: any) bool { return match value { case error: true default: false } }
pn main() {
    let fault = int("bad")
    print([failed(select(fault, 0)), failed(select([1], fault)),
        failed(select(null, fault)), failed(select(fault, fault)),
        failed(select(fault, "field"))]); print("\n")
    print([select(null, 0), select([1], "field"), select([1], -1), select([1], 8)]); print("\n")
    let diagnostic = error("indexed-metadata")
    print([select(diagnostic, "message"), select(diagnostic, 'message'),
        select(diagnostic, "code") == diagnostic.code,
        select(diagnostic, "source"), failed(select(diagnostic, fault))]); print("\n")
}
