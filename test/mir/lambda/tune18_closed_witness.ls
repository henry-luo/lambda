// Tune18 follow-on: a closed inferred ArrayNum entry reuses the caller witness
// without rebuilding the boxed admission branch in its raw body.

pn closed_witness18(values, scale: float) {
    return values[0] + scale
}

pn main() {
    var values = [7.0, 7.0, 7.0, 7.0]
    print(closed_witness18(values, 0.0))
}
