// Tune26: a dynamic integral source subscript preserves its nullable float
// lane; the destination store sends only FLOAT_LANE_NULL to the checked path.

let TUNE26_OFFSET = 1

pn tune26_nullable_float_store(var dst: float[], src: float[], n: int) float {
    var i: int = 0
    while (i < n) {
        dst[i] = dst[i] + src[i + TUNE26_OFFSET]
        i = i + 1
    }
    return dst[0] + dst[n - 1]
}

pn tune26_nullable_float_oob(var dst: float[], src: float[]) any^ {
    var i: int = 0
    // The nullable native lane from this read must not escape into dst.
    dst[i] = dst[i] + src[i + TUNE26_OFFSET]
}

pn main() {
    var dst: float[] = fill(4, 1.0)
    let snapshot = dst
    let src: float[] = [0.0, 2.0, 3.0, 4.0, 5.0]
    print(tune26_nullable_float_store(dst, src, 4)); print(" ")
    print(snapshot[0]); print(" ")
    print(dst[0]); print("\n")

    var rejected = false
    var rejected_dst: float[] = [1.0]
    let rejected_snapshot = rejected_dst
    let short_src: float[] = [2.0]
    tune26_nullable_float_oob(rejected_dst, short_src) ^ { rejected = true }
    print(rejected); print(" ")
    print(rejected_dst[0]); print(" ")
    print(rejected_snapshot[0]); print("\n")
}
