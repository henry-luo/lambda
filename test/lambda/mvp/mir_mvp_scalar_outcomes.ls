// scalar outcome facts preserve selected tags, absence and owned number homes (S11.2.1, D5.2.3).
fn scalar_checked(value: any) float^ { return value }
fn scalar_checked_int(value: any) int^ { return value }
fn scalar_ordered(value: any) any {
    let checked = scalar_checked_int(value)
    return match checked { case float: false case int: true default: null }
}
fn scalar_match(value: any) float^ {
    let checked = scalar_checked(value)
    let recovered: float = match checked { case error: -1.0 case float: checked }
    return recovered
}
fn scalar_partial(value: any) any { return match value { case float: value } }
pn scalar_compare(values: any, raw: any) any {
    let threshold: float = raw
    return values[0] < threshold
}
fn scalar_tag(value: any) any { return match value { case int: value default: 2.5 } }
fn scalar_home(value: any) any {
    let kept = scalar_checked(value)
    let churn = [scalar_checked(4.0), scalar_checked(5.0), scalar_checked(6.0)]
    return [kept, kept, churn[0]]
}
pn main() {
    print([scalar_match(3.0), scalar_match("bad")]); print("\n")
    print([scalar_partial(2.5), scalar_partial("bad")]); print("\n")
    print([scalar_tag(3) is int, scalar_tag("bad") is float]); print("\n")
    print([scalar_ordered(3), scalar_ordered("bad")]); print("\n")
    print([scalar_compare([0.5], 1.0), scalar_compare([0.5], "bad") is error,
        scalar_compare([null], 1.0)]); print("\n")
    print(scalar_home(1.0 / 0.0)); print("\n")
}
