// AWFY Benchmark: Towers of Hanoi (Typed version)
// Expected result: 8191

type TState = {moves: int}

pn push_disk(var piles: int[], var tops: int[], disk_size: int, pile: int) {
    let t: int = tops[pile]
    piles[pile * 14 + t] = disk_size
    tops[pile] = t + 1
}

pn pop_disk_from(piles: int[], var tops: int[], pile: int) {
    let t: int = tops[pile] - 1
    tops[pile] = t
    let disk_size = piles[pile * 14 + t]
    return disk_size
}

pn move_top_disk(var piles: int[], var tops: int[], var state: TState, from_pile: int, to_pile: int) {
    let disk = pop_disk_from(piles, tops, from_pile)
    push_disk(piles, tops, disk, to_pile)
    state.moves = state.moves + 1
}

pn move_disks(var piles: int[], var tops: int[], var state: TState, disks: int, from_pile: int, to_pile: int) {
    if (disks == 1) {
        move_top_disk(piles, tops, state, from_pile, to_pile)
        return 0
    }
    var other_pile: int = (3 - from_pile) - to_pile
    move_disks(piles, tops, state, disks - 1, from_pile, other_pile)
    move_top_disk(piles, tops, state, from_pile, to_pile)
    move_disks(piles, tops, state, disks - 1, other_pile, to_pile)
}

pn benchmark() {
    var piles:int[] = fill(42, 0)
    var tops:int[] = [0, 0, 0]
    var i: int = 12
    while (i >= 0) {
        push_disk(piles, tops, i, 0)
        i = i - 1
    }
    var state: TState = {moves: 0}
    move_disks(piles, tops, state, 13, 0, 1)
    return state.moves
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 8191) {
        print("Towers: PASS\n")
    } else {
        print("Towers: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
