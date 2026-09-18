// Result47: an untyped caller may route a typed scalar callee through its
// boxed adapter. The enclosing native consumer must check the Item error arm
// and then reopen the typed result instead of demanding a lane from `any`.
pn safe_add(x: int, y: int) {
    var ux: u32 = x
    var uy: u32 = y
    return int(ux + uy)
}

pn dynamic(value) {
    return value
}

pn caller(value) {
    var acc = 1
    acc = safe_add(safe_add(acc, dynamic(value)), 1)
    return [acc]
}

pn main() {
    print(caller(2))
}
