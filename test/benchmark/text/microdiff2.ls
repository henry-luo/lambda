// Typed text benchmark: nested document snapshot change accounting.

let microdiff_path_lengths: int[] = [2, 6, 6, 7, 2, 3, 1, 1, 1, 2]
let microdiff_type_lengths: int[] = [6, 6, 6, 6, 6, 6, 6, 6, 6, 6]

pn snapshot_score(version: int) int {
    var score: int = 0
    var i: int = 0
    while (i < len(microdiff_path_lengths)) {
        let slot: int = (i + version) % len(microdiff_path_lengths)
        score = score + 19 + microdiff_type_lengths[slot] * 23 + microdiff_path_lengths[slot]
        i = i + 1
    }
    return score
}

pn main() {
    var checksum: int = 0
    var round: int = 0
    let t0 = clock()
    while (round < 512) {
        checksum = checksum + snapshot_score(round % 2)
        checksum = checksum + snapshot_score((round + 1) % 2)
        checksum = checksum + snapshot_score(round % 2)
        checksum = checksum + snapshot_score((round + 1) % 2)
        round = round + 1
    }
    print("microdiff: CHECKSUM:" ++ checksum ++ "\n")
    print("__TIMING__:" ++ ((clock() - t0) * 1000.0) ++ "\n")
}
