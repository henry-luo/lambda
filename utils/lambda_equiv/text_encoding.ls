// Shared byte and hexadecimal helpers for native Lambda build utilities.
pub pn utf8_bytes(source: string) {
    var bytes = []
    var i = 0
    while (i < len(source)) {
        let code = ord(slice(source, i, i + 1))
        if (code < 128) {
            bytes = bytes ++ [code]
        } else if (code < 2048) {
            bytes = bytes ++ [bor(192, shr(code, 6)), bor(128, band(code, 63))]
        } else if (code < 65536) {
            bytes = bytes ++ [bor(224, shr(code, 12)), bor(128, band(shr(code, 6), 63)),
                              bor(128, band(code, 63))]
        } else {
            bytes = bytes ++ [bor(240, shr(code, 18)), bor(128, band(shr(code, 12), 63)),
                              bor(128, band(shr(code, 6), 63)), bor(128, band(code, 63))]
        }
        i = i + 1
    }
    return bytes
}

pub fn hex8(value) {
    let digits = "0123456789ABCDEF"
    join([for (shift in [28, 24, 20, 16, 12, 8, 4, 0]) {
        let index = band(shr(value, shift), 15)
        slice(digits, index, index + 1)
    }], "")
}
