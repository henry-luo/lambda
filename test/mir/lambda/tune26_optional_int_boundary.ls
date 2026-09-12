// S4.1/D3.3.3v3: optional system integers reject null at a plain-int boundary.
pn decode(value: any) int {
    let code: int = ord(value)
    return code
}

pn main() {
    var rejected = false
    let code = decode("A")
    decode("") ^ { rejected = true }
    print([code, rejected])
}
