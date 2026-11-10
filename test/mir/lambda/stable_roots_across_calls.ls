// D5.3.1/D6.3.1: exact roots survive allocations, mixed joins and overflow branches.
pub pn stable_roots(seed: int, choose: bool) any {
    let kept = [string(seed), string(seed + 1)]
    let scratch = split("a,b", ",")
    let more = split("c,d", ",")
    var carried: any = kept
    if (choose) { carried = more }
    let count = len(carried)
    let squared = seed * seed
    let final = split("e,f", ",")
    return [kept[0], kept[1], carried[0], count, squared, final[1], scratch[1]]
}

pn main() {
    print(stable_roots(3, true))
    print(stable_roots(4, false))
}
