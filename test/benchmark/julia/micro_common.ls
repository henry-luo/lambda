// Shared untyped scalar workloads; SUITE.md defines counts, loop order and checks.
pn decimal_text(value) {
    let negative = value < 0
    var remaining = if (negative) -value else value
    var divisor = 1
    while (remaining div divisor >= 10) { divisor = divisor * 10 }
    var text = if (negative) "-" else ""
    while (divisor > 0) {
        text = text ++ chr(48 + remaining div divisor)
        remaining = remaining % divisor
        divisor = divisor div 10
    }
    return text
}
pn decimal_value(text) {
    let negative = text[0] == "-"
    var index = if (negative) 1 else 0
    var value = 0
    while (index < len(text)) {
        value = value * 10 + ord(text[index]) - 48
        index = index + 1
    }
    return if (negative) -value else value
}
pn parse_integers() {
    var seed = 42
    var checksum = 0
    var size = 0
    var errors = 0
    var index = 0
    while (index < 100000) {
        seed = seed * 16807 % 2147483647
        let value = if (index % 8 == 0) 0 else if (index % 8 == 1) -seed else seed
        let text = decimal_text(value)
        let parsed = decimal_value(text)
        if (parsed != value) { errors = errors + 1 }
        size = size + len(text)
        checksum = (checksum * 31 + parsed + 2147483647) % 1000000007
        index = index + 1
    }
    return [checksum, size, seed, errors]
}
pn gram(matrix, rows, columns) {
    var result = fill(columns * columns, 0.0)
    var i = 0
    while (i < columns) {
        var j = 0
        while (j < columns) {
            var total = 0.0
            var k = 0
            while (k < rows) {
                total = total + matrix[k * columns + i] * matrix[k * columns + j]
                k = k + 1
            }
            result[i * columns + j] = total
            j = j + 1
        }
        i = i + 1
    }
    return result
}
pn square(matrix, n) {
    var result = fill(n * n, 0.0)
    var i = 0
    while (i < n) {
        var j = 0
        while (j < n) {
            var total = 0.0
            var k = 0
            while (k < n) {
                total = total + matrix[i * n + k] * matrix[k * n + j]
                k = k + 1
            }
            result[i * n + j] = total
            j = j + 1
        }
        i = i + 1
    }
    return result
}
pn trace_fourth(matrix, rows, columns) {
    let fourth = square(square(gram(matrix, rows, columns), columns), columns)
    var total = 0.0
    var i = 0
    while (i < columns) {
        total = total + fourth[i * columns + i]
        i = i + 1
    }
    return total
}
pn variation(values) {
    var total = 0.0
    var i = 0
    while (i < len(values)) { total = total + values[i]; i = i + 1 }
    let mean = total / float(len(values))
    total = 0.0
    i = 0
    while (i < len(values)) {
        let delta = values[i] - mean
        total = total + delta * delta
        i = i + 1
    }
    return math.sqrt(total / float(len(values) - 1)) / mean
}
pn matrix_statistics() {
    var seed = 42
    var digest = 0
    var v = fill(1000, 0.0)
    var w = fill(1000, 0.0)
    var iteration = 0
    while (iteration < 1000) {
        var blocks = fill(100, 0.0)
        var p = fill(100, 0.0)
        var q = fill(100, 0.0)
        var i = 0
        while (i < 100) {
            seed = seed * 16807 % 2147483647
            blocks[i] = float(seed) / 2147483647.0 * 2.0 - 1.0
            i = i + 1
        }
        var block = 0
        while (block < 4) {
            var row = 0
            while (row < 5) {
                var column = 0
                while (column < 5) {
                    let value = blocks[block * 25 + row * 5 + column]
                    p[row * 20 + block * 5 + column] = value
                    q[(block div 2 * 5 + row) * 10 + block % 2 * 5 + column] = value
                    column = column + 1
                }
                row = row + 1
            }
            block = block + 1
        }
        v[iteration] = trace_fourth(p, 5, 20)
        w[iteration] = trace_fourth(q, 10, 10)
        digest = (digest * 31 + int(floor(v[iteration] * 1000.0))) % 1000000007
        digest = (digest * 31 + int(floor(w[iteration] * 1000.0))) % 1000000007
        iteration = iteration + 1
    }
    return [int(floor(variation(v) * 1e9)), int(floor(variation(w) * 1e9)), digest, seed]
}
pn iteration_pi_sum() {
    var values = fill(500, 0.0)
    var iteration = 0
    while (iteration < 500) {
        var total = 0.0
        var k = 1
        while (k <= 10000 + iteration) {
            total = total + 1.0 / (float(k) * float(k))
            k = k + 1
        }
        values[iteration] = total
        iteration = iteration + 1
    }
    var digest = 0.0
    var i = 0
    while (i < 500) { digest = digest + values[i] * float(i + 1); i = i + 1 }
    return [int(floor(values[0] * 1e12)), int(floor(values[499] * 1e12)), int(floor(digest * 1e6)), 5124750]
}
pn formatted_output() {
    var size = 0
    var digest = 0
    var writes = 0
    var buffer = ""
    let sink = if (sys.os.name == "Windows") "NUL" else "/dev/null"
    var i = 1
    while (i <= 100000) {
        let line = decimal_text(i) ++ " " ++ decimal_text(i + 1) ++ "\n"
        var j = 0
        while (j < len(line)) {
            digest = (digest * 31 + ord(line[j])) % 1000000007
            j = j + 1
        }
        size = size + len(line)
        buffer = buffer ++ line
        if (i % 256 == 0 or i == 100000) {
            let written = output(buffer, sink) or -1
            if (written != len(buffer)) { return [-1, digest, writes, i] }
            writes = writes + 1
            buffer = ""
        }
        i = i + 1
    }
    return [size, digest, writes, 100000]
}
pn micro_workload(bench_name) {
    if (bench_name == "parse_integers") { return parse_integers() }
    if (bench_name == "matrix_statistics") { return matrix_statistics() }
    if (bench_name == "iteration_pi_sum") { return iteration_pi_sum() }
    if (bench_name == "formatted_output") { return formatted_output() }
    return [-1, -1, -1, -1]
}
pn micro_verify(bench_name, result) {
    var expected = [0, 0, 0, 0]
    if (bench_name == "parse_integers") { expected = [592470661, 854479, 1966931148, 0] }
    if (bench_name == "matrix_statistics") { expected = [464726438, 486656926, 47509838, 1966931148] }
    if (bench_name == "iteration_pi_sum") { expected = [1644834071848, 1644838824217, 206015869118, 5124750] }
    if (bench_name == "formatted_output") { expected = [1177795, 584298900, 391, 100000] }
    return result[0] == expected[0] and result[1] == expected[1] and result[2] == expected[2] and result[3] == expected[3]
}
pub pn run_micro_benchmark(bench_name) {
    let warm = micro_workload(bench_name)
    if (not micro_verify(bench_name, warm)) { print(bench_name ++ ": FAIL warmup " ++ string(warm) ++ "\n"); return null }
    let started = clock()
    let result = micro_workload(bench_name)
    let elapsed = (clock() - started) * 1000.0
    if (not micro_verify(bench_name, result)) { print(bench_name ++ ": FAIL " ++ string(result) ++ "\n"); return null }
    print(bench_name ++ ": PASS " ++ result[0] ++ " " ++ result[1] ++ " " ++ result[2] ++ " " ++ result[3] ++ "\n")
    print("__TIMING__:" ++ elapsed ++ "\n")
}
