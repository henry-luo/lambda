// S4.1/D3.3.3v3: optional system integers reject null at a plain-int boundary.
pn decode(value: any) int {
    let code: int = ord(value)
    return code
}

pn main() {
    var rejected = false
    var wrong_kind = false
    var error_value = false
    let code = decode("A")
    decode("") ^ { rejected = true }
    // keep the open ord path observable beside the closed string calls.
    decode(17) ^ { wrong_kind = true }
    decode(error("ord operand")) ^ { error_value = true }
    print([code, rejected, wrong_kind, error_value])
}
