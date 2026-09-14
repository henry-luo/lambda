fn boxed_error() any => error("boxed assignment")

pn error_to_native() int^ {
    var value = 7
    value = boxed_error()
    value
}

pn inferred_loop_float() {
    var value = 1
    var once = true
    while once {
        value = 1.5
        once = false
    }
    value
}

pn annotated_int_rejects_float() int^ {
    var value: int = 7
    value = 1.5
    value
}

pn main() {
    var boxed_error_failed = false
    error_to_native() ^ { boxed_error_failed = true }
    var widening_failed = false
    annotated_int_rejects_float() ^ { widening_failed = true }
    print([boxed_error_failed, inferred_loop_float(), widening_failed])
}
