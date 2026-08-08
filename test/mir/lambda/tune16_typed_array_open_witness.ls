// Tune16 C4.1: an open/boxed source must be admitted at the caller while the
// typed-array call remains on the native edge instead of being demoted to _b.

pn tune16_open_consumer(values: int[]) int {
    return values[0] + values[1]
}

pn main() {
    var values: any = [3, 4]
    print(tune16_open_consumer(values))
    print("\n")
}
