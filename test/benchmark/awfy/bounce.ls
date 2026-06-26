// AWFY Benchmark: Bounce
// Expected result: 1331
// 100 balls bouncing in a 500x500 box for 50 frames
// Uses flat arrays instead of maps for ball data to avoid type mismatch

pn random_next(seed_arr) {
    var s = seed_arr[0]
    s = s * 1309 + 13849
    s = s % 65536
    seed_arr[0] = s
    return s
}

pn benchmark() {
    var seed_arr = [74755]
    var ball_count = 100
    var bounces = 0

    // 4 arrays for ball properties: x, y, xVel, yVel
    var bx = fill(ball_count, 0)
    var by = fill(ball_count, 0)
    var bxv = fill(ball_count, 0)
    var byv = fill(ball_count, 0)

    // initialize balls
    for i in 0 to ball_count - 1 {
        bx[i] = random_next(seed_arr) % 500
        by[i] = random_next(seed_arr) % 500
        bxv[i] = (random_next(seed_arr) % 300) - 150
        byv[i] = (random_next(seed_arr) % 300) - 150
    }

    // simulate 50 frames
    for i in 0 to 49 {
        for j in 0 to ball_count - 1 {
            bx[j] = bx[j] + bxv[j]
            by[j] = by[j] + byv[j]

            var bounced = 0
            if (bx[j] > 500) {
                bx[j] = 500
                var axv = abs(bxv[j])
                bxv[j] = 0 - axv
                bounced = 1
            }
            if (bx[j] < 0) {
                bx[j] = 0
                var axv2 = abs(bxv[j])
                bxv[j] = axv2
                bounced = 1
            }
            if (by[j] > 500) {
                by[j] = 500
                var ayv = abs(byv[j])
                byv[j] = 0 - ayv
                bounced = 1
            }
            if (by[j] < 0) {
                by[j] = 0
                var ayv2 = abs(byv[j])
                byv[j] = ayv2
                bounced = 1
            }
            bounces = bounces + bounced
        }
    }
    return bounces
}

pn main() {
    let result = benchmark()
    if (result == 1331) {
        print("Bounce: PASS\n")
    } else {
        print("Bounce: FAIL result=")
        print(result)
        print("\n")
    }
}
