// Tune28 T28-5 / S17.1.1: literal separators keep leftmost non-overlapping
// matches, including a first-byte near miss and a retained delimiter.
pn main() {
    print(split("aXbXXc", "XX"))
    print("\n")
    print(split("aXbXXc", "XX", true))
    print("\n")
    print(split("a,b,,c", ","))
    print("\n")
}
