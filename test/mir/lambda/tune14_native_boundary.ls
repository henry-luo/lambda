// Tune14 A1: native identity boundaries, integer bitwise lanes, and
// same-contract array rebinding must stay on the proved lane. The widened
// control keeps the generic Item path so the optimization cannot erase its
// representation guard.

pn tune14_float_sum(values: float[]) float^ {
    var total: float = 0.0
    let _admitted_total_8: float = total + values[0]
    total = _admitted_total_8^
    let _admitted_total_9: float = total + values[1]
    total = _admitted_total_9^
    return total
}

pn tune14_bitwise(values: int[]) int^ {
    let _admitted_value_14: int = values[0]
    var value: int = _admitted_value_14^
    value = shr(value, 1)
    value = band(value, 255)
    return value
}

pn tune14_rebind(left: int[], right: int[]) int {
    var prev: int[] = left
    var curr: int[] = right
    var tmp: int[] = prev
    prev = curr
    curr = tmp
    return prev[0] + curr[0]
}

pn tune14_widened(values: any[]) int^ {
    return values[0] + 1
}

pn main() {
    print(string(tune14_float_sum([1.5, 2.5])) ++ "\n")
    print(string(tune14_bitwise([1024])) ++ "\n")
    print(string(tune14_rebind([3], [4])) ++ "\n")
    print(string(tune14_widened([7])) ++ "\n")
}
