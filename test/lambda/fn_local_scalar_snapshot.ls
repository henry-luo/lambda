// D5.2.1v5: a later call cannot overwrite the scalar retained by an earlier let.
fn boxed(value: float) any { return value }
fn snapshot() {
    var value: any = boxed(1e-310)
    let saved: any = value
    value = boxed(2e-310)
    return saved == 1e-310
}
[snapshot(), snapshot(), snapshot(), snapshot(), snapshot(), snapshot()]
