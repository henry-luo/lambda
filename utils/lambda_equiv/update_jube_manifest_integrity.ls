// Native Lambda core of utils/update_jube_manifest_integrity.py.
// CLI argument forwarding is unavailable; callers pass the module directory directly.
fn hex4(code: int) {
    let digits = "0123456789abcdef"
    join([for (shift in [12, 8, 4, 0]) {
        let index = band(shr(code, shift), 15)
        slice(digits, index, index + 1)
    }], "")
}

// Python json.dumps defaults to ensure_ascii=true; Lambda's JSON formatter emits UTF-8.
pn ascii_json(source: string) {
    var escaped = ""
    var i = 0
    while (i < len(source)) {
        let ch = slice(source, i, i + 1)
        let code = ord(ch)
        if (code < 128) {
            escaped = escaped ++ ch
        } else if (code <= 65535) {
            escaped = escaped ++ "\\u" ++ hex4(code)
        } else {
            let pair = code - 65536
            escaped = escaped ++ "\\u" ++ hex4(55296 + shr(pair, 10)) ++
                      "\\u" ++ hex4(56320 + band(pair, 1023))
        }
        i = i + 1
    }
    return escaped
}

pub pn update_manifest(module_dir: string) {
    let manifest_path = module_dir ++ "/module.json"
    let manifest = input(manifest_path, "json")^
    var clean = {}

    for (key, value at manifest) {
        if (key != 'host_build_id' and key != 'sha256_macos' and
            key != 'sha256_linux' and key != 'sha256_windows') {
            clean[key] = value
        }
    }

    let json = format(clean, {type: "json", indent: 2})
    let written = output(ascii_json(json) ++ "\n", manifest_path, "text")^
    return written
}
