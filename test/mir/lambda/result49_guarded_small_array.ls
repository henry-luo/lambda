// S7.1.1v3, D5.2: an absent source read takes the declaration's checked path.
pn guarded_pair(v0: float[], v1: float[]) float {
    let pair: float[] = [v0[1] + v1[0], v0[0] - v1[1]]
    pair[0] + pair[1]
}

pn escaping_pair(v0: float[], v1: float[]) float[] {
    let pair: float[] = [v0[1] + v1[0], v0[0] - v1[1]]
    pair
}

pn main() {
    print(guarded_pair([1.0, 2.0], [4.0, 5.0])); print(" ")
    print(guarded_pair([1.0], [4.0, 5.0]) is error); print(" ")
    print(guarded_pair([1.0, 2.0], [4.0]) is error); print(" ")
    print(escaping_pair([1.0, 2.0], [4.0, 5.0])[0]); print("\n")
}
