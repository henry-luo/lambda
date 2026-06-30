// AWFY Benchmark: Bounce (Typed version)
// Expected result: 1331

pn random_next(seed_arr: int[]) {
    var s: int = seed_arr[0]
    s = s * 1309 + 13849
    s = s % 65536
    seed_arr[0] = s
    return s
}

pn benchmark() {
    var seed_arr:int[] = [74755]
    var ball_count: int = 100
    var bounces: int = 0

    var bx:int[] = fill(ball_count, 0)
    var by:int[] = fill(ball_count, 0)
    var bxv:int[] = fill(ball_count, 0)
    var byv:int[] = fill(ball_count, 0)

    for i in 0 to ball_count - 1 {
        bx[i] = random_next(seed_arr) % 500
        by[i] = random_next(seed_arr) % 500
        bxv[i] = (random_next(seed_arr) % 300) - 150
        byv[i] = (random_next(seed_arr) % 300) - 150
    }

    for i in 0 to 49 {
        for j in 0 to ball_count - 1 {
            bx[j] = bx[j] + bxv[j]
            by[j] = by[j] + byv[j]

            var bounced: int = 0
            if (bx[j] > 500) {
                bx[j] = 500
                var axv: int = abs(bxv[j])
                bxv[j] = 0 - axv
                bounced = 1
            }
            if (bx[j] < 0) {
                bx[j] = 0
                var axv2: int = abs(bxv[j])
                bxv[j] = axv2
                bounced = 1
            }
            if (by[j] > 500) {
                by[j] = 500
                var ayv: int = abs(byv[j])
                byv[j] = 0 - ayv
                bounced = 1
            }
            if (by[j] < 0) {
                by[j] = 0
                var ayv2: int = abs(byv[j])
                byv[j] = ayv2
                bounced = 1
            }
            bounces = bounces + bounced
        }
    }
    return bounces
}

pn main() {
    var __t0 = clock()
    let result = benchmark()
    var __t1 = clock()
    if (result == 1331) {
        print("Bounce: PASS\n")
    } else {
        print("Bounce: FAIL result=")
        print(result)
        print("\n")
    }
    print("__TIMING__:" ++ ((__t1 - __t0) * 1000.0) ++ "\n")
}
