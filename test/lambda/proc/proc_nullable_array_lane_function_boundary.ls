// A nullable container pointer does not become an Item at a direct call.
fn pass_values(value: array?) array? => value

pn main() {
    let missing: array? = pass_values(null)
    let values: array? = pass_values([4, 5])
    print([missing, len(values)])
}
