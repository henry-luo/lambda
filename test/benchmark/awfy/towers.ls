// AWFY Benchmark: Towers of Hanoi
// Expected result: 8191
// 13 disks, using arrays as stacks
// Each pile is represented as {disks: [...], top: int}

pn make_array(n, val) {
    var arr = [val, val, val, val, val, val, val, val, val, val]
    var sz = 10
    while (sz * 2 <= n) {
        arr = arr ++ arr
        sz = sz * 2
    }
    if (sz < n) {
        var remain = n - sz
        var extra = [val]
        var esz = 1
        while (esz < remain) {
            extra = extra ++ [val]
            esz = esz + 1
        }
        arr = arr ++ extra
    }
    return arr
}

pn push_disk(piles, tops, disk_size, pile) {
    let t = tops[pile]
    piles[pile * 14 + t] = disk_size
    tops[pile] = t + 1
}

pn pop_disk_from(piles, tops, pile) {
    let t = tops[pile] - 1
    tops[pile] = t
    let disk_size = piles[pile * 14 + t]
    return disk_size
}

pn move_top_disk(piles, tops, state, from_pile, to_pile) {
    let disk = pop_disk_from(piles, tops, from_pile)
    push_disk(piles, tops, disk, to_pile)
    state.moves = state.moves + 1
}

pn move_disks(piles, tops, state, disks, from_pile, to_pile) {
    if (disks == 1) {
        move_top_disk(piles, tops, state, from_pile, to_pile)
        return 0
    }
    var other_pile = (3 - from_pile) - to_pile
    move_disks(piles, tops, state, disks - 1, from_pile, other_pile)
    move_top_disk(piles, tops, state, from_pile, to_pile)
    move_disks(piles, tops, state, disks - 1, other_pile, to_pile)
}

pn benchmark() {
    // 3 piles × 14 slots each = 42 slots
    var piles = make_array(42, 0)
    var tops = [0, 0, 0]
    // build tower at pile 0 with 13 disks (size 0..12, pushed largest first)
    var i = 12
    while (i >= 0) {
        push_disk(piles, tops, i, 0)
        i = i - 1
    }
    let state = {moves: 0}
    move_disks(piles, tops, state, 13, 0, 1)
    return state.moves
}

pn main() {
    let result = benchmark()
    if (result == 8191) {
        print("Towers: PASS\n")
    } else {
        print("Towers: FAIL result=")
        print(result)
        print("\n")
    }
}
