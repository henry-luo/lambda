// D2.6.2-D2.6.3: int[] stores admit poison and reject the full-width null lane.
// A 32-bit test aliases INT_LANE_NULL with finite zero; the guard must be BEQ.

pn tune17b_swap(var v: int[], i: int, j: int) any {
    var tmp = v[i]
    v[i] = v[j]
    v[j] = tmp
}

pn main() {
    var v: int[] = [10, 20, 30, 40]
    tune17b_swap(v, 0, 1)
    print(v[0])
    print("\n")
    var rejected = false
    tune17b_swap(v, 0, 99) ^ { rejected = true }
    print(string([rejected, v[0]]) ++ "\n")
    var zero: int[] = [0, 5]
    tune17b_swap(zero, 0, 1)
    print(string(zero) ++ "\n")
}
