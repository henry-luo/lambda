// S7.6.3v3/S11.4.6v3/D2.4.1: a plain admitted local fixes its success lane.
fn opaque(value) => value
fn checked_float(value) float^ {
    let checked: float = value
    checked^
}
fn checked_int(value) int^ {
    let checked: int = value
    checked^
}
fn preserved_union(value) float^ {
    let checked: float^ = value
    checked^
}
fn captured(value) float^ {
    let checked: float = value
    fn inner() float^ => checked^
    inner()
}
fn nullable(value) float?^ {
    let checked: float? = value
    checked^
}
fn literal(value) int^ {
    let checked: 1 = value
    checked^
}
pn changed(value) float^ {
    var checked: float = value
    checked = opaque("bad")
    return checked^
}
pn accumulate(values: float[], count: int) float^ {
    var total: float = 0.0
    var index: int = 0
    while (index < count) {
        let checked: float = total + values[index]
        total = checked^
        index = index + 1
    }
    return total
}
pn delayed(values: float[], var trace: array) float^ {
    let checked: float = values[1]
    push(trace, "after")^
    return checked^
}
pn observed(values: float[], var trace: array) float^ {
    let checked: float = values[1]
    let refused = checked is error
    push(trace, refused)^
    return checked^
}
pn nested(values: float[], var trace: array) float^ {
    let normal = {
        let checked: float = values[1]
        checked^
    }
    push(trace, "after")^
    return normal
}
pn read_int(values: int[]) int^ {
    let checked: int = values[1]
    return checked^
}
pn main() {
    let callback = checked_float
    let accumulated = accumulate
    var delayed_trace: array = []
    var observed_trace: array = []
    var nested_trace: array = []
    let delayed_error = delayed([1.5], delayed_trace)
    let observed_error = observed([1.5], observed_trace)
    let nested_error = nested([1.5], nested_trace)
    let indexed_error = read_int([1])
    let loop_error = accumulate([1.5], 2);
    [type(checked_float(7)) == float, checked_float("bad") is error,
     type(checked_int(1.0)) == int, checked_int(1.5) is error,
     type(preserved_union(7)) == float, type(preserved_union(7.5)) == float,
     type(captured(7)) == float, captured("bad") is error,
     nullable(null) == null, nullable("bad") is error,
     literal(1) == 1, literal(2) is error, changed(7) is error,
     callback(7) == 7.0, callback("bad") is error,
     accumulate([1.5, 2.5], 2) == 4.0,
     accumulate([1.5], 2) is error,
     accumulated([1.5, 2.5], 2) == 4.0,
     accumulated([1.5], 2) is error,
     delayed_error is error, delayed_trace == ["after"],
     observed_error is error, observed_trace == [true],
     nested_error is error, nested_trace == [],
     read_int([1, 2]) == 2, indexed_error is error,
     indexed_error.column == 9, loop_error.line == 37, loop_error.column == 13]
}
