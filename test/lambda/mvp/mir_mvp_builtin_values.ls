// S5.5.1v2/S11.1.5: bare builtins retain registry identity and callable colour.
fn named_builtin() { len }
fn inspect_value(value: any) { value is fn }
pn main() {
    print([len == len, len != name, name(len) == 'len']); print("\n")
    print([inspect_value(len), print is pn, not (print is fn)]); print("\n")
    let saved = named_builtin()
    let other = named_builtin()
    print([saved == other, name(saved) == 'len']); print("\n")
    // Full publishes foreign identity values without a Core dynamic-call ABI.
    let attempted = saved([1, 2])
    print(attempted is error); print("\n")
}
