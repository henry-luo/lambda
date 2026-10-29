// S9.1.2/S9.1.6: computed writes retain a parsed element copy's replacement.
pn main() {
    let document = parse("<r><i k=\"1\"/></r>", 'xml')^
    var child = document[0]
    child["z"] = "4"
    child["w"] = "5"
    print([child[0].k, child.z, child.w, document[0].z, document[0].w]); print("\n")
    let key = "z"
    child[key] = "6"
    print([child.z, document[0].z]); print("\n")
}
