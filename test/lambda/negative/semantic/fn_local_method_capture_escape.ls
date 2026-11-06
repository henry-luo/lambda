// S9.1.4v2: returning an instance also retains its pn methods and captures.
fn make() {
    var shift = 10
    type Box {
        value: int,
        pn mutate() { shift = shift + 1 }
    }
    return <Box value:5>
}
make()
