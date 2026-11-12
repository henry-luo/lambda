// Cross-module var call for the caller's projected-place publication regression.
pub pn change(var child) map^ {
    child.value = 2
    return {answer:3, nested:[4,5]}
}

pub pn scalar(var child, mode: int) {
    child.value = child.value + 1
    return if (mode == 0) 1.25 else 9223372036854775807i64
}
