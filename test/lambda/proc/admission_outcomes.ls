// S7.7.2v2/S7.7.4v2: failed local validation stores an outcome and continues.
fn opaque(value) => value
fn opaque_any(value) any => value
pn inferred_return(value) {
    if (true) { let checked: int = value; return checked }
    return 7
}
pn main() {
    let bad: int = opaque("bad")
    let good: int = opaque(7)
    var changed: int = 1
    changed = opaque("new failure")
    let comma_bad: int = opaque("bad"), comma_good: int = opaque(8)
    let nested = { let value: int = opaque("bad"); value is error }
    let arr: int[] = opaque([1, "bad"])
    let small: u32 = opaque(-1)
    let inferred = error("captured")
    let broad: any = error("captured too");
    let checked_after_handler: int = opaque("bad") ^ { 99 }
    let after_binding = checked_after_handler ^ { 99 };
    let broad_source: int = opaque_any("bad");
    let returned = inferred_return(null);
    [bad is error, good, changed is error, comma_bad is error, comma_good,
     nested, arr is error, small is error, inferred.message, broad.message,
     checked_after_handler is error, after_binding, broad_source is error, returned is error]
}
