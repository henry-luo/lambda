// Action C: a clean checked scalar return uses the native value/error lanes,
// not the pending companion protocol.

fn checked_double(value: float) float^ => value * 2.0

pn main() {
    let value^err = checked_double(3.5)
    print(string(value) ++ "," ++ string(err) ++ "\n")
}
