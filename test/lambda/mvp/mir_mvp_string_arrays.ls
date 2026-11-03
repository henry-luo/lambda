// boxed and native pointer lanes share checked string[] reads (D2.6.1v3, S7.1.1v3).
fn fetch_string(values: string[], key: int) string? { return values[key] }
pn change(var values: string[]) any { values[0] = "changed" }
pn main() {
    var values: string[] = ["a", string(42)]
    print(fetch_string(values, 0)); print(" "); print(fetch_string(values, 1)); print(" ")
    print(fetch_string(values, -1)); print(" "); print(fetch_string(values, 2)); print(" ")
    print(fetch_string(values, 4294967296)); print("\n")
    let saved = values
    change(values)
    print(fetch_string(saved, 0)); print(" "); print(fetch_string(values, 0)); print("\n")
    let empty: string[] = []
    print(fetch_string(empty, 0)); print("\n")
}
