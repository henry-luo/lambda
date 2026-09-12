// Tune26 / D2.2.2 / D3.2.2: inferred numeric fill sources and scalar swap
// temporaries preserve a declared int[] lane without repeated representation
// guards on the successful store path.

pn tune26_inferred_swap_lane(n: int) int {
    var target: int[] = fill(n, 0)
    var source = fill(n, 0)
    source[0] = 3
    source[1] = 7
    target[0] = source[0]
    target[1] = source[1]
    var tmp = target[0]
    target[0] = target[1]
    target[1] = tmp
    return target[0] * 10 + target[1]
}

pn main() {
    print(tune26_inferred_swap_lane(2))
}
