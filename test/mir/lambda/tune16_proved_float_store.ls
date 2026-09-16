// T28-7: a literal index into a fixed-length local is proven in bounds, so the
// checked cold store is pinned through a caller-chosen index instead.
pn tune16_proved_float_store(slot: int) float {
    var values: float[] = [1.0, 2.0]
    var k: int = slot
    values[k] = 3.5
    return values[k]
}

pn main() {
    print(tune16_proved_float_store(1))
    print("\n")
}
