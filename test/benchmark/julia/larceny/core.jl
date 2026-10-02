include("../common.jl")

mutable struct ExprNode
    kind::Int
    value::Int
    left::Union{Nothing,ExprNode}
    right::Union{Nothing,ExprNode}
end
expr_const(v) = ExprNode(0, v, nothing, nothing)
expr_var() = ExprNode(1, 0, nothing, nothing)
expr_binary(kind, l, r) = ExprNode(kind, 0, l, r)
function deriv(e)
    e.kind == 0 && return expr_const(0)
    e.kind == 1 && return expr_const(1)
    e.kind == 2 && return expr_binary(2, deriv(e.left), deriv(e.right))
    dl, dr = deriv(e.left), deriv(e.right)
    return expr_binary(2, expr_binary(3, e.left, dr), expr_binary(3, dl, e.right))
end
count_nodes(e) = e.kind <= 1 ? 1 : 1 + count_nodes(e.left) + count_nodes(e.right)
function make_expr()
    c3, c2, c5 = expr_const(3), expr_const(2), expr_const(5)
    m1 = expr_binary(3, c3, expr_var())
    m2 = expr_binary(3, m1, expr_var())
    m3 = expr_binary(3, m2, expr_var())
    m4 = expr_binary(3, c2, expr_var())
    m5 = expr_binary(3, m4, expr_var())
    return expr_binary(2, expr_binary(2, expr_binary(2, m3, m5), expr_var()), c5)
end

function subtract_div(x, y)
    q = 0
    while x >= y
        x -= y
        q += 1
    end
    return q
end
function subtract_mod(x, y)
    while x >= y; x -= y; end
    return x
end
recursive_div(x, y) = x < y ? 0 : 1 + recursive_div(x - y, y)
recursive_mod(x, y) = x < y ? x : recursive_mod(x - y, y)

ms2(r) = (Int(r) * (Int(r) + 1)) >> 1
ms3(r) = Int(r) * (Int(r) + 1) * (Int(r) + 2) ÷ 6
ms4(r) = Int(r) * (Int(r) + 1) * (Int(r) + 2) * (Int(r) + 3) ÷ 24
function count_radicals(rcount, k)
    count = 0
    target = k - 1
    nc1 = 0
    while nc1 * 3 <= target
        nc2 = nc1
        while nc1 + nc2 * 2 <= target
            nc3 = target - nc1 - nc2
            if nc3 >= nc2
                r1, r2, r3 = Int(rcount[nc1 + 1]), Int(rcount[nc2 + 1]), Int(rcount[nc3 + 1])
                if nc1 == nc2 == nc3; count += ms3(r1)
                elseif nc1 == nc2; count += ms2(r1) * r3
                elseif nc2 == nc3; count += r1 * ms2(r2)
                else; count += r1 * r2 * r3
                end
            end
            nc2 += 1
        end
        nc1 += 1
    end
    return count
end
function count_ccp(rcount, n)
    m, max_rad, count = n - 1, (n - 1) >> 1, 0
    nc1 = 0
    while nc1 * 4 <= m
        nc2 = nc1
        while nc1 + nc2 * 3 <= m
            remain = m - nc1 - nc2
            nc3 = nc2
            while nc3 * 2 <= remain
                nc4 = remain - nc3
                if nc3 <= nc4 <= max_rad
                    r1, r2 = Int(rcount[nc1 + 1]), Int(rcount[nc2 + 1])
                    r3, r4 = Int(rcount[nc3 + 1]), Int(rcount[nc4 + 1])
                    if nc1 == nc2 == nc3 == nc4; count += ms4(r1)
                    elseif nc1 == nc2 == nc3; count += ms3(r1) * r4
                    elseif nc2 == nc3 == nc4; count += r1 * ms3(r2)
                    elseif nc1 == nc2 && nc3 == nc4; count += ms2(r1) * ms2(r3)
                    elseif nc1 == nc2; count += ms2(r1) * r3 * r4
                    elseif nc2 == nc3; count += r1 * ms2(r2) * r4
                    elseif nc3 == nc4; count += r1 * r2 * ms2(r3)
                    else; count += r1 * r2 * r3 * r4
                    end
                end
                nc3 += 1
            end
            nc2 += 1
        end
        nc1 += 1
    end
    return count
end
function paraffins(n)
    n < 1 && return 0
    half = n >> 1
    rcount = zeros(Int32, half + 1)
    rcount[1] = 1
    for k in 1:half
        rcount[k + 1] = count_radicals(rcount, k)
    end
    bcp = n % 2 == 0 ? ms2(rcount[(n >> 1) + 1]) : 0
    return bcp + count_ccp(rcount, n)
end

function pnpoly(xs, ys, testx, testy)
    inside = false
    j = length(xs)
    for i in eachindex(xs)
        yi, yj = ys[i], ys[j]
        if (yi > testy) != (yj > testy)
            xtest = (xs[j] - xs[i]) * (testy - yi) / (yj - yi) + xs[i]
            testx < xtest && (inside = !inside)
        end
        j = i
    end
    return inside
end

function puzzle_solve(row, cols, diag1, diag2, n)
    row == n && return 1
    count = 0
    for col in 0:n-1
        d1, d2 = row + col + 1, row - col + n
        if !cols[col + 1] && !diag1[d1] && !diag2[d2]
            cols[col + 1] = diag1[d1] = diag2[d2] = true
            count += puzzle_solve(row + 1, cols, diag1, diag2, n)
            cols[col + 1] = diag1[d1] = diag2[d2] = false
        end
    end
    return count
end

function quicksort!(arr, lo, hi)
    lo >= hi && return
    pivot, i = arr[hi], lo
    for j in lo:hi-1
        if arr[j] <= pivot
            arr[i], arr[j] = arr[j], arr[i]
            i += 1
        end
    end
    arr[i], arr[hi] = arr[hi], arr[i]
    quicksort!(arr, lo, i - 1)
    quicksort!(arr, i + 1, hi)
end

function sphere_intersect(ox, oy, oz, dx, dy, dz, cx, cy, cz, r)
    ex, ey, ez = ox - cx, oy - cy, oz - cz
    a = dx * dx + dy * dy + dz * dz
    b = 2.0 * (ex * dx + ey * dy + ez * dz)
    c = ex * ex + ey * ey + ez * ez - r * r
    disc = b * b - 4.0 * a * c
    disc < 0.0 && return -1.0
    t = (-b - sqrt(disc)) / (2.0 * a)
    t > 0.001 && return t
    t = (-b + sqrt(disc)) / (2.0 * a)
    return t > 0.001 ? t : -1.0
end

function triangl()
    mfrom = [0,0,1,1,2,2,3,3,3,3,4,4,5,5,5,5,6,6,7,7,8,8,9,9,10,10,11,11,12,12,12,12,13,13,14,14] .+ 1
    mover = [1,2,3,4,4,5,1,4,6,7,7,8,2,4,8,9,3,7,4,8,4,7,5,8,6,11,7,12,7,8,11,13,8,12,9,13] .+ 1
    mto = [3,5,6,8,7,9,0,5,10,12,11,13,0,3,12,14,1,8,2,9,1,6,2,7,3,12,4,13,3,5,10,14,4,11,5,12] .+ 1
    board = ones(UInt8, 15)
    board[1] = 0
    solutions, pegs, depth = 0, 14, 1
    stack = ones(Int, 14)
    while depth >= 1
        if pegs == 1
            solutions += 1
            depth -= 1
            depth < 1 && break
            last_m = stack[depth]
            board[mfrom[last_m]] = board[mover[last_m]] = 1
            board[mto[last_m]] = 0
            pegs += 1
            stack[depth] = last_m + 1
        end
        found = false
        mi = stack[depth]
        while mi <= 36
            if board[mfrom[mi]] != 0 && board[mover[mi]] != 0 && board[mto[mi]] == 0
                board[mfrom[mi]] = board[mover[mi]] = 0
                board[mto[mi]] = 1
                pegs -= 1
                stack[depth] = mi
                depth += 1
                depth <= 14 && (stack[depth] = 1)
                found = true
                break
            end
            mi += 1
        end
        if !found
            depth -= 1
            if depth >= 1
                last_m = stack[depth]
                board[mfrom[last_m]] = board[mover[last_m]] = 1
                board[mto[last_m]] = 0
                pegs += 1
                stack[depth] = last_m + 1
            end
        end
    end
    return solutions
end

function larceny_workload(io, name)
    if name == "array1"
        arr = zeros(Int32, 10000)
        for i in eachindex(arr); arr[i] = i - 1; end
        total = 0
        for _ in 1:100
            s = 0
            for i in eachindex(arr); s += arr[i]; end
            total = s
        end
        return total
    elseif name == "deriv"
        result = 0
        for _ in 1:5000; result = count_nodes(deriv(make_expr())); end
        return result
    elseif name == "diviter" || name == "divrec"
        result = 0
        for _ in 1:1000
            if name == "diviter"
                result += subtract_div(1000000, 2)
                result -= subtract_mod(1000000, 2)
            else
                result += recursive_div(1000, 2)
                result -= recursive_mod(1000, 2)
            end
        end
        return result
    elseif name == "gcbench"
        println(io, "stretch tree of depth 15 check: ", check_tree(make_tree(15)))
        long_lived = make_tree(14)
        for depth in 4:2:14
            iterations = 1 << (18 - depth)
            total = 0
            for _ in 1:iterations; total += check_tree(make_tree(depth)); end
            println(io, iterations, " trees of depth ", depth, " check: ", total)
        end
        return long_lived
    elseif name == "paraffins"
        result = 0
        for _ in 1:10, n in 1:23; result = paraffins(n); end
        return result
    elseif name == "pnpoly"
        xs = [0.0,1.0,1.0,0.0,0.0,1.0,-0.5,-1.0,-1.0,-2.0,-2.5,-2.0,-1.5,-0.5,0.5,1.0,0.5,0.0,-0.5,-1.0]
        ys = [0.0,0.0,1.0,1.0,2.0,3.0,2.0,3.0,0.0,-0.5,0.5,1.5,2.0,3.0,3.0,2.0,1.0,0.5,-1.0,-1.0]
        count, total = 0, 0
        for ix in 0:499, iy in 0:199
            pnpoly(xs, ys, -2.5 + ix * 0.008, -1.5 + iy * 0.025) && (count += 1)
            total += 1
        end
        return (total, count)
    elseif name == "primes"
        return prime_sieve(1000000)
    elseif name == "puzzle"
        return puzzle_solve(0, fill(false, 10), fill(false, 20), fill(false, 20), 10)
    elseif name == "quicksort"
        arr, seed = zeros(Int32, 5000), 42
        for i in eachindex(arr)
            seed = next_lcg(seed)
            arr[i] = seed
        end
        quicksort!(arr, 1, 5000)
        return arr
    elseif name == "ray"
        sx, sy = [0.0,-2.0,2.0,0.0], [0.0,0.0,0.0,2.0]
        sz, sr = fill(5.0, 4), ones(4)
        hits = 0
        for py in 0:99, px in 0:99
            dx, dy, dz = (px - 50.0) / 50.0, (py - 50.0) / 50.0, 1.0
            len = sqrt(dx * dx + dy * dy + dz * dz)
            dx /= len; dy /= len; dz /= len
            min_t = 999999.0
            for si in 1:4
                t = sphere_intersect(0, 0, 0, dx, dy, dz, sx[si], sy[si], sz[si], sr[si])
                0.0 < t < min_t && (min_t = t)
            end
            min_t < 999999.0 && (hits += 1)
        end
        return hits
    elseif name == "triangl"
        return triangl()
    end
    error("unknown Larceny benchmark: " * name)
end

function main(name)
    expected = Dict("array1"=>49995000, "deriv"=>45, "diviter"=>500000000,
                    "divrec"=>500000, "paraffins"=>5731580, "primes"=>78498,
                    "puzzle"=>724, "triangl"=>29760)
    verify(result) = name == "gcbench" ? check_tree(result) == 32767 :
                     name == "quicksort" ? issorted(result) :
                     name == "ray" ? 0 < result < 10000 :
                     name == "pnpoly" ? result[1] == 100000 && 0 < result[2] < result[1] :
                     result == expected[name]
    run_benchmark(()->nothing, (io, state)->larceny_workload(io, name), verify,
                  result->begin
                      if name == "gcbench"
                          println("long lived tree of depth 14 check: ", check_tree(result))
                          return
                      elseif name == "paraffins"
                          println("paraffins: nb(23) = ", result)
                      elseif name == "pnpoly"
                          println("pnpoly: total=", result[1], " inside=", result[2])
                          println("pnpoly: DONE")
                          return
                      elseif name == "ray"
                          println("ray: hits=", result)
                      elseif name == "triangl"
                          println("triangl: solutions=", result)
                      end
                      println(name, ": PASS")
                  end)
end
