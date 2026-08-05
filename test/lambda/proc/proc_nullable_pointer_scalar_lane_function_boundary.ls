// Nullable symbol and binary values cross a direct function boundary as their
// raw pointer lane; only the result collection reboxes a NULL lane as null.
fn pass_symbol(value: symbol?) symbol? => value
fn pass_binary(value: binary?) binary? => value

pn main() {
    let missing_symbol: symbol? = pass_symbol(null)
    let present_symbol: symbol? = pass_symbol('lane')
    let missing_binary: binary? = pass_binary(null)
    let present_binary: binary? = pass_binary(b'CAFE')
    print([missing_symbol, present_symbol, missing_binary, string(present_binary)])
}
