// An implicit parameter is `any \ error`; its error arm is unreachable.
fn implicit_case(value) int {
    match value {
        case error: 0
        case any: 1
    }
}

// An explicit `any` parameter admits an error value, so this arm is intentional.
fn explicit_case(value: any) int {
    match value {
        case error: 0
        case any: 1
    }
}

[implicit_case(9), explicit_case(9)]
