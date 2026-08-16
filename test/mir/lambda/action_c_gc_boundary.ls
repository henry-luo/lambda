// Action C: a pending result must be materialized before an array/root
// publication boundary can expose it to a collecting runtime call.

pn pending_item(value) { return value }
pn main() {
    let values = [pending_item(7i64), 9i64]
    print(string(values[0]) ++ "," ++ string(values[1]) ++ "\n")
}
