import .js_array_props_tail_module

let int64_value = 9223372036854775807i64
let datetime_value = t'2025-04-26T10:30:45'
let float_value = 0.1
let replacement = -9223372036854775807i64
let shared = [int64_value, datetime_value, float_value]
let promoted = promoteIncomingArray(shared, replacement)

[
    verifyArrayPropsTailBridge(int64_value, datetime_value, float_value),
    promoted[0] == replacement,
    shared[0] == replacement,
    promoted[1] == replacement,
    shared[1] == replacement,
    len(promoted) == 67,
    len(shared) == 67
]
