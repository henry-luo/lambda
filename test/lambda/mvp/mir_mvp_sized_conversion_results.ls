// D4.3.1/S7.7.3: sized numeric conversions retain scalar success and Error separately.
fn widened(value: u32) any => float(value)
fn narrowed(value: u32) any => int(value)
pn random_value(var owner) {
    var seed: u32 = owner.seed
    seed = seed * 1103515245u32 + 12345u32
    owner.seed = int(seed)^
    return float(seed) / 4294967296.0
}
fn scalar_tag(depth: int, payload) {
    if (depth == 0) { return payload }
    return scalar_tag(depth - 1, payload)
}
fn float_record(value) any => {value: value, marker: 123}
pn main() {
    print([widened(0u32) == 0.0, widened(4294967295u32) == 4294967295.0,
        narrowed(4294967295u32) == 4294967295]); print("\n")
    var owner = {seed: 1}
    let sample = random_value(owner)
    print([owner.seed == 1103527590, scalar_tag(3, sample) == sample]); print("\n")
    let negative = float_record(-0.0)
    let small = float_record(1e-310)
    let nan_value = float_record(math.sqrt(-1.0))
    print([1.0 / negative.value < 0.0, small.value == 1e-310,
        nan_value.value != nan_value.value, negative.marker == 123]); print("\n")
    let problem: any = error({code: 7021, message: "conversion"})
    print([widened(problem).message, narrowed(problem).message,
        scalar_tag(2, problem).message]); print("\n")
}
