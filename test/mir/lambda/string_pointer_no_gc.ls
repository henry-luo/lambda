// D5.3.2: a string pointer comparison has no GC safepoint in its raw leaf.
fn equal(left: string, right: string) bool => left == right

pn main() {
    print([equal("red", "red"), equal("red", "blue")])
    print("\n")
}
