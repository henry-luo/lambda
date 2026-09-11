// Tune26 T26-1: typed-array-only formals select the raw witness body even
// when the result is an array, keeping indexed reads out of boxed item_at.

pn tune26_array_only_params(left: float[], right: float[]) float[] {
    var total: float[] = fill(2, 0.0)
    total[0] = left[0] + right[0]
    total[1] = left[1] + right[1]
    return total
}

pn main() {
    var left: float[] = [1.0, 2.0]
    var right: float[] = [3.0, 4.0]
    var total: float[] = tune26_array_only_params(left, right)
    print(total[0]); print(" ")
    print(total[1]); print("\n")
}
