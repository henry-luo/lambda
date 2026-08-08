pn tune16_proved_float_store() float {
    var values: float[] = [1.0, 2.0]
    values[1] = 3.5
    return values[1]
}

pn main() {
    print(tune16_proved_float_store())
    print("\n")
}
