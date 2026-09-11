// Tune26 T26-4: matching source/destination subscripts make a successful
// float[] write's read in-bounds too, so its COW/OOB arm is compact.

pn tune26_var_float_store(var values: float[], n: int) float {
    var i: int = 0
    while (i < n) {
        values[i] = values[i] + 0.5
        i = i + 1
    }
    return values[0] + values[n - 1]
}

pn main() {
    var values: float[] = [1.0, 2.0, 3.0]
    print(floor(tune26_var_float_store(values, 3))); print("\n")
}
