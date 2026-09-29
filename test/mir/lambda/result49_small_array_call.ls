// S11.4.1v3, D5.2: a direct short float[] producer can stay in scalar lanes
// only while the caller observes fixed elements; an absent read keeps the
// ordinary checked return path.
pn vector_mix(v0: float[], v1: float[]) float[] {
    return [v0[0] + v1[1], v0[1] - v1[0], v0[2] * v1[2]]
}

pn fixed_result(v0: float[], v1: float[]) float {
    var result: float[] = vector_mix(v0, v1)
    result[0] + result[1] + result[2]
}

pn escaping_result(v0: float[], v1: float[]) float[] {
    var result: float[] = vector_mix(v0, v1)
    result
}

pn rebound_result(v0: float[], v1: float[]) float {
    var result: float[] = vector_mix(v0, v1)
    result = [7.0, 8.0, 9.0]
    result[0]
}

pn main() {
    print(fixed_result([1.0, 2.0, 3.0], [4.0, 5.0, 6.0])); print(" ")
    print(escaping_result([1.0, 2.0, 3.0], [4.0, 5.0, 6.0])[2]); print(" ")
    print(fixed_result([1.0], [4.0, 5.0, 6.0]) is error); print(" ")
    print(rebound_result([1.0, 2.0, 3.0], [4.0, 5.0, 6.0])); print("\n")
}
