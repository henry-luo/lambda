// S5.1.4v2/D2.4.1: empty literal values can use the immutable shared String.
fn empty() string => ""
fn filled() string => "filled"

pn main() {
    print([empty(), len(empty()), empty() == "", filled()])
    print("\n")
}
