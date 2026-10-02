include("../common.jl")

fib(n) = n < 2 ? n : fib(n - 1) + fib(n - 2)
fibfp(n) = n < 2.0 ? n : fibfp(n - 1.0) + fibfp(n - 2.0)
tak(x, y, z) = y >= x ? z : tak(tak(x - 1, y, z), tak(y - 1, z, x), tak(z - 1, x, y))
function ack(m, n)
    m == 0 && return n + 1
    n == 0 && return ack(m - 1, 1)
    return ack(m - 1, ack(m, n - 1))
end

function descending_sum(n)
    s = zero(n)
    while n >= zero(n)
        s += n
        n -= one(n)
    end
    return s
end

function queens_ok(row, dist, placed, placed_len)
    dist > placed_len && return true
    p = placed[placed_len - dist + 1]
    (p == row + dist || p == row - dist) && return false
    return queens_ok(row, dist + 1, placed, placed_len)
end

function queens_solve(candidates, cand_len, rest, rest_len, placed, placed_len)
    cand_len == 0 && return rest_len == 0 ? 1 : 0
    row = candidates[1]
    count = 0
    if queens_ok(row, 1, placed, placed_len)
        new_cands = zeros(Int32, cand_len - 1 + rest_len)
        ni = 0
        for ci in 2:cand_len
            ni += 1
            new_cands[ni] = candidates[ci]
        end
        for ri in 1:rest_len
            ni += 1
            new_cands[ni] = rest[ri]
        end
        placed[placed_len + 1] = row
        count += queens_solve(new_cands, ni, zeros(Int32, 1), 0, placed, placed_len + 1)
    end
    # preserve Node's fresh candidate/rest allocations at every search branch.
    new_rest = zeros(Int32, rest_len + 1)
    for ri in 1:rest_len
        new_rest[ri] = rest[ri]
    end
    new_rest[rest_len + 1] = row
    new_cands = zeros(Int32, cand_len - 1)
    for ci in 2:cand_len
        new_cands[ci - 1] = candidates[ci]
    end
    return count + queens_solve(new_cands, cand_len - 1, new_rest, rest_len + 1, placed, placed_len)
end

function four1!(data)
    n = length(data)
    j = 0
    for i in 0:2:n-1
        if i < j
            data[i + 1], data[j + 1] = data[j + 1], data[i + 1]
            data[i + 2], data[j + 2] = data[j + 2], data[i + 2]
        end
        m = n >> 1
        while m >= 2 && j >= m
            j -= m
            m >>= 1
        end
        j += m
    end
    mmax = 2
    while mmax < n
        theta = 6.28318530717959 / mmax
        sin_half = sin(0.5 * theta)
        wpr = -2.0 * sin_half * sin_half
        wpi = sin(theta)
        wr, wi = 1.0, 0.0
        for m in 0:2:mmax-1
            for i in m:2*mmax:n-1
                jj = i + mmax
                tempr = wr * data[jj + 1] - wi * data[jj + 2]
                tempi = wr * data[jj + 2] + wi * data[jj + 1]
                data[jj + 1] = data[i + 1] - tempr
                data[jj + 2] = data[i + 2] - tempi
                data[i + 1] += tempr
                data[i + 2] += tempi
            end
            new_wr = wr * wpr - wi * wpi + wr
            wi = wi * wpr + wr * wpi + wi
            wr = new_wr
        end
        mmax *= 2
    end
    return data[1]
end

function mbrot_count(r, i, step, x, y)
    cr, ci = r + x * step, i + y * step
    zr, zi = cr, ci
    for c in 0:63
        zr2, zi2 = zr * zr, zi * zi
        zr2 + zi2 > 16.0 && return c
        zr, zi = zr2 - zi2 + cr, 2.0 * zr * zi + ci
    end
    return 64
end

function r7rs_workload(name)
    name == "fib" && return fib(27)
    name == "fibfp" && return fibfp(27.0)
    name == "tak" && return tak(18, 12, 6)
    if name == "cpstak"
        result = tak(18, 12, 6)
        result = tak(18, 12, 6)
        return result
    end
    if name == "sum"
        result = 0
        for _ in 1:100
            result = descending_sum(10000)
        end
        return result
    end
    name == "sumfp" && return descending_sum(100000.0)
    name == "ack" && return ack(3, 8)
    name == "nqueens" && return queens_solve(Int32.(1:8), 8, zeros(Int32, 1), 0, zeros(Int32, 8), 0)
    name == "fft" && return four1!(zeros(4096))
    if name == "mbrot"
        matrix = [zeros(Int32, 75) for _ in 1:75]
        for y in 74:-1:0, x in 74:-1:0
            matrix[x + 1][y + 1] = mbrot_count(-1.0, -0.5, 0.005, x, y)
        end
        return matrix[1][1]
    end
    error("unknown R7RS benchmark: " * name)
end

function main(name)
    expected = Dict("fib"=>196418, "fibfp"=>196418.0, "tak"=>7, "cpstak"=>7,
                    "sum"=>50005000, "sumfp"=>5000050000.0, "ack"=>2045,
                    "nqueens"=>92, "fft"=>0.0, "mbrot"=>5)
    checked_benchmark(name, state->r7rs_workload(name), expected[name])
end
