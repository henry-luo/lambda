// Nullable arguments and returns retain their lane until the public Item ABI.
fn add_one(value: int?) int? => value + 1
fn add_half(value: float?) float? => value + 0.5
fn pass_float(value: float?) float? => value
fn pass_bool(value: bool?) bool? => value
fn pass_string(value: string?) string? => value

pn main() {
    print([add_one(null), add_one(2), pass_float(null), add_half(null), add_half(1.5),
        pass_bool(null), pass_bool(true), pass_string(null), pass_string("yes")])
}
