// A plain pointer array is descriptor-clean too: null cannot widen string[]
// into string?[] through a dynamic assignment.
fn dynamic(value) any { value }

pn main() {
    var values: string[] = ["one", "two"]
    values[0] = dynamic(null)
}
