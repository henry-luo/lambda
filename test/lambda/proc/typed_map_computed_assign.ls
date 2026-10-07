// Computed-key writes through a map-typed `var`:
// - the open `map` annotation is the plain TYPE_MAP singleton, which the field
//   lookup read as a TypeMap (segfault on `var m: map = {}; m["a"] = 1`);
// - an undeclared key already held as an open member was appended again on
//   every write instead of updated (S2.1.4), so counting duplicated entries.
type T = {a: int}

pn main() {
    var m: map = {}
    m["a"] = 1
    var counts: map = {}
    let keys = ["a", "b", "a", "c", "b", "a"]
    var i = 0
    while (i < len(keys)) {
        let k = keys[i]
        counts[k] = (counts[k] or 0) + 1
        i = i + 1
    }
    var t: T = {a: 1}
    t["x"] = 1
    t["x"] = 2
    t["x"] = 3
    print("m=" ++ join([for (k, v at m) string(k) ++ ":" ++ string(v)], ",") ++
        " counts=" ++ join([for (k, v at counts) string(k) ++ ":" ++ string(v)], ",") ++
        " t=" ++ join([for (k, v at t) string(k) ++ ":" ++ string(v)], ",") ++ "\n")
}
