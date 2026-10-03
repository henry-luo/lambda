# Scalar Julia-inspired workloads; no BLAS, Statistics or package dependencies.
function decimal_text(value)
    negative = value < 0
    value = abs(value)
    divisor = 1
    while value ÷ divisor >= 10
        divisor *= 10
    end
    text = negative ? "-" : ""
    while divisor > 0
        text *= string(Char(48 + value ÷ divisor))
        value %= divisor
        divisor ÷= 10
    end
    return text
end
function decimal_value(text)
    negative = text[1] == '-'
    index, value = negative ? 2 : 1, 0
    while index <= length(text)
        value = value * 10 + Int(text[index]) - 48
        index += 1
    end
    return negative ? -value : value
end
function parse_integers()
    seed, checksum, size, errors = 42, 0, 0, 0
    for index in 0:99999
        seed = seed * 16807 % 2147483647
        value = index % 8 == 0 ? 0 : (index % 8 == 1 ? -seed : seed)
        text = decimal_text(value)
        parsed = decimal_value(text)
        errors += parsed != value
        size += length(text)
        checksum = (checksum * 31 + parsed + 2147483647) % 1000000007
    end
    return [checksum, size, seed, errors]
end
function gram(matrix, rows, columns)
    result = zeros(columns * columns)
    for i in 0:columns-1, j in 0:columns-1
        total = 0.0
        for k in 0:rows-1
            total += matrix[k * columns + i + 1] * matrix[k * columns + j + 1]
        end
        result[i * columns + j + 1] = total
    end
    return result
end
function square(matrix, n)
    result = zeros(n * n)
    for i in 0:n-1, j in 0:n-1
        total = 0.0
        for k in 0:n-1
            total += matrix[i * n + k + 1] * matrix[k * n + j + 1]
        end
        result[i * n + j + 1] = total
    end
    return result
end
function trace_fourth(matrix, rows, columns)
    fourth = square(square(gram(matrix, rows, columns), columns), columns)
    total = 0.0
    for i in 0:columns-1
        total += fourth[i * columns + i + 1]
    end
    return total
end
function variation(values)
    total = 0.0
    for value in values
        total += value
    end
    mean = total / length(values)
    total = 0.0
    for value in values
        delta = value - mean
        total += delta * delta
    end
    return sqrt(total / (length(values) - 1)) / mean
end
function matrix_statistics()
    seed, digest = 42, 0
    v, w = zeros(1000), zeros(1000)
    for iteration in 1:1000
        blocks, p, q = zeros(100), zeros(100), zeros(100)
        for i in 1:100
            seed = seed * 16807 % 2147483647
            blocks[i] = seed / 2147483647.0 * 2.0 - 1.0
        end
        for block in 0:3, row in 0:4, column in 0:4
            value = blocks[block * 25 + row * 5 + column + 1]
            p[row * 20 + block * 5 + column + 1] = value
            q[(block ÷ 2 * 5 + row) * 10 + block % 2 * 5 + column + 1] = value
        end
        v[iteration] = trace_fourth(p, 5, 20)
        w[iteration] = trace_fourth(q, 10, 10)
        digest = (digest * 31 + floor(Int, v[iteration] * 1000)) % 1000000007
        digest = (digest * 31 + floor(Int, w[iteration] * 1000)) % 1000000007
    end
    return [floor(Int, variation(v) * 1e9), floor(Int, variation(w) * 1e9), digest, seed]
end
function iteration_pi_sum()
    values = zeros(500)
    for iteration in 0:499
        total = 0.0
        for k in 1:10000+iteration
            total += 1.0 / (Float64(k) * Float64(k))
        end
        values[iteration + 1] = total
    end
    digest = 0.0
    for i in 1:500
        digest += values[i] * i
    end
    return [floor(Int, values[1] * 1e12), floor(Int, values[500] * 1e12), floor(Int, digest * 1e6), 5124750]
end
function formatted_output()
    size, digest, writes, buffer = 0, 0, 0, ""
    for i in 1:100000
        line = decimal_text(i) * " " * decimal_text(i + 1) * "\n"
        for char in line
            digest = (digest * 31 + Int(char)) % 1000000007
        end
        size += length(line)
        buffer *= line
        if i % 256 == 0 || i == 100000
            open(Sys.iswindows() ? "NUL" : "/dev/null", "w") do sink
                @assert write(sink, buffer) == sizeof(buffer)
            end
            writes += 1
            buffer = ""
        end
    end
    return [size, digest, writes, 100000]
end
const MICRO_EXPECTED = Dict(
    "parse_integers" => [592470661, 854479, 1966931148, 0],
    "matrix_statistics" => [464726438, 486656926, 47509838, 1966931148],
    "iteration_pi_sum" => [1644834071848, 1644838824217, 206015869118, 5124750],
    "formatted_output" => [1177795, 584298900, 391, 100000])
function run_micro_benchmark(name, workload)
    # This suite fixes one complete warmup in every language, regardless of engine defaults.
    @assert workload() == MICRO_EXPECTED[name] "warmup: FAIL"
    started = time_ns()
    result = workload()
    elapsed = (time_ns() - started) / 1e6
    @assert result == MICRO_EXPECTED[name] "measured workload: FAIL"
    println(name, ": PASS ", join(result, " "))
    println("__TIMING__:", elapsed)
end
