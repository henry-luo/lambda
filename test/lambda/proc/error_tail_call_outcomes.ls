// S7.4.2v3/S7.7.3/D8.2.6: rejected tail-call arguments remain error outcomes.
fn returned(value: int) int^ =>
    if (value < 0) error("returned") else value + 1;
fn raised(value: int) int^ {
    if (value < 0) raise error("raised") else value + 1
}
fn expression_tail(steps: int, value: int, next: fn(int) int^) int^ =>
    if (steps == 0) value
    else expression_tail(steps - 1, next(value) ^ { ^ }, next);
pn procedure_tail(steps: int, value, next: fn(int) int^) any^ {
    if (steps == 0) { return value }
    return procedure_tail(steps - 1, next(value) ^ { ^ }, next)
}
pn main() {
    let boxed_function = expression_tail
    let boxed_procedure = procedure_tail;
    [expression_tail(2, -1, returned) ^ { ^.message },
     expression_tail(2, -1, raised) ^ { ^.message },
     expression_tail(3, 1, returned),
     procedure_tail(2, -1, returned) ^ { ^.message },
     procedure_tail(2, -1, raised) ^ { ^.message },
     procedure_tail(3, 1, returned),
     boxed_function(2, -1, returned) ^ { ^.message },
     boxed_procedure(2, -1, raised) ^ { ^.message }]
}
