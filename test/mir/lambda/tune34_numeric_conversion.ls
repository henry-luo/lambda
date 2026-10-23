// S4.2/D2.4: native conversion guards preserve negative, poison and null paths.
pn truncate_value(value: float) { return int(value) }
pn floor_value(value: float) { return int(floor(value)) }
pn optional_value(value: float?) {
    if (value == null) { return null }
    return int(floor(value))
}
pn main() {
    print([truncate_value(2.75), truncate_value(-2.75), truncate_value(-0.0)]); print("\n")
    print([floor_value(2.75), floor_value(-2.75), floor_value(-0.0)]); print("\n")
    print([floor_value(inf), floor_value(-inf), type(floor_value(nan))]); print("\n")
    print([truncate_value(9007199254740991.0), truncate_value(-9007199254740991.0)]); print("\n")
    print([optional_value(null), optional_value(3.5)]); print("\n")
}
