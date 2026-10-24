// S11.4.5: exact float membership is unchanged; numeric conversions remain checked.
pn admit(value) float { var result: float = value; return result }
pn main() {
    let values = [0.0, -0.0, 1.5, 1.0 / 0.0, 0.0 / 0.0, 3, 9007199254740993i64, "wrong", null]
    for (value in values) {
        let result = admit(value)
        print(if (result is error) type(result) else [type(result), result]); print("\n")
    }
    print([admit(1e-300) == 1e-300, 1.0 / admit(0.0), 1.0 / admit(-0.0)]); print("\n")
    return null
}
