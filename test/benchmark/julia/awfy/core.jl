include("../common.jl")
include("../nbody.jl")

# SOM microbenchmarks, ported from ref/are-we-fast-yet/benchmarks/JavaScript.
# Copyright (c) 2001-2021 Stefan Marr and contributors; see LICENSE.md.
mutable struct SOMRandom
    seed::Int
end
function som_next!(random)
    random.seed = (random.seed * 1309 + 13849) & 65535
    return random.seed
end

mutable struct Ball
    x::Int
    y::Int
    vx::Int
    vy::Int
end
function bounce!(ball)
    bounced = false
    ball.x += ball.vx; ball.y += ball.vy
    if ball.x > 500; ball.x = 500; ball.vx = -abs(ball.vx); bounced = true; end
    if ball.x < 0; ball.x = 0; ball.vx = abs(ball.vx); bounced = true; end
    if ball.y > 500; ball.y = 500; ball.vy = -abs(ball.vy); bounced = true; end
    if ball.y < 0; ball.y = 0; ball.vy = abs(ball.vy); bounced = true; end
    return bounced
end

mutable struct ListNode
    value::Int
    next::Union{Nothing,ListNode}
end
make_list(n) = n == 0 ? nothing : ListNode(n, make_list(n - 1))
list_length(node) = node.next === nothing ? 1 : 1 + list_length(node.next)
function shorter_than(x, y)
    while y !== nothing
        x === nothing && return true
        x, y = x.next, y.next
    end
    return false
end
function list_tail(x, y, z)
    if shorter_than(y, x)
        return list_tail(list_tail(x.next, y, z), list_tail(y.next, z, x), list_tail(z.next, x, y))
    end
    return z
end

mutable struct TowerDisk
    size::Int
    next::Union{Nothing,TowerDisk}
end
function tower_push!(piles, disk, pile)
    top = piles[pile]
    @assert top === nothing || disk.size < top.size
    disk.next = top
    piles[pile] = disk
end
function tower_move!(piles, from, to)
    top = piles[from]
    @assert top !== nothing
    piles[from] = top.next
    top.next = nothing
    tower_push!(piles, top, to)
end
function tower_disks!(piles, disks, from, to)
    if disks == 1
        tower_move!(piles, from, to)
        return 1
    end
    other = 6 - from - to
    count = tower_disks!(piles, disks - 1, from, other)
    tower_move!(piles, from, to)
    return count + 1 + tower_disks!(piles, disks - 1, other, to)
end

function permute!(values, n, count)
    count[] += 1
    if n != 0
        n1 = n - 1
        permute!(values, n1, count)
        for i in n1:-1:0
            values[n1 + 1], values[i + 1] = values[i + 1], values[n1 + 1]
            permute!(values, n1, count)
            values[n1 + 1], values[i + 1] = values[i + 1], values[n1 + 1]
        end
    end
end

function place_queen!(column, free_rows, free_maxs, free_mins, queen_rows)
    for row in 0:7
        a, b = column + row + 1, column - row + 8
        if free_rows[row + 1] && free_maxs[a] && free_mins[b]
            queen_rows[row + 1] = column
            free_rows[row + 1] = free_maxs[a] = free_mins[b] = false
            (column == 7 || place_queen!(column + 1, free_rows, free_maxs, free_mins, queen_rows)) && return true
            free_rows[row + 1] = free_maxs[a] = free_mins[b] = true
        end
    end
    return false
end

function storage_tree(depth, random, count)
    count[] += 1
    depth == 1 && return Vector{Any}(undef, som_next!(random) % 10 + 1)
    arr = Vector{Any}(undef, 4)
    for i in 1:4; arr[i] = storage_tree(depth - 1, random, count); end
    return arr
end

function awfy_mandelbrot(size)
    sum, byte_acc, bit_num = 0, 0, 0
    for y in 0:size-1
        ci = (2.0 * y / size) - 1.0
        for x in 0:size-1
            zrzr, zi, zizi = 0.0, 0.0, 0.0
            cr = (2.0 * x / size) - 1.5
            escape = 0
            for _ in 1:50
                zr = zrzr - zizi + cr
                zi = 2.0 * zr * zi + ci
                zrzr, zizi = zr * zr, zi * zi
                if zrzr + zizi > 4.0
                    escape = 1
                    break
                end
            end
            byte_acc = (byte_acc << 1) + escape
            bit_num += 1
            if bit_num == 8
                sum = xor(sum, byte_acc)
                byte_acc, bit_num = 0, 0
            elseif x == size - 1
                sum = xor(sum, byte_acc << (8 - bit_num))
                byte_acc, bit_num = 0, 0
            end
        end
    end
    return sum
end

function awfy_micro(name)
    if name == "sieve"
        flags, count = fill(true, 5000), 0
        for i in 2:5000
            if flags[i]
                count += 1
                k = i + i
                while k <= 5000
                    flags[k] = false
                    k += i
                end
            end
        end
        return count == 669
    elseif name == "permute"
        count = Ref(0)
        permute!(zeros(Int, 6), 6, count)
        return count[] == 8660
    elseif name == "queens"
        result = true
        for _ in 1:10
            result = result && place_queen!(0, fill(true, 8), fill(true, 16), fill(true, 16), fill(-1, 8))
        end
        return result
    elseif name == "towers"
        piles = Union{Nothing,TowerDisk}[nothing, nothing, nothing]
        for i in 13:-1:0; tower_push!(piles, TowerDisk(i, nothing), 1); end
        return tower_disks!(piles, 13, 1, 2) == 8191
    elseif name == "bounce"
        random = SOMRandom(74755)
        balls = [Ball(som_next!(random) % 500, som_next!(random) % 500,
                      som_next!(random) % 300 - 150, som_next!(random) % 300 - 150) for _ in 1:100]
        bounces = 0
        for _ in 1:50, ball in balls; bounce!(ball) && (bounces += 1); end
        return bounces == 1331
    elseif name == "list"
        return list_length(list_tail(make_list(15), make_list(10), make_list(6))) == 10
    elseif name == "storage"
        count = Ref(0)
        storage_tree(7, SOMRandom(74755), count)
        return count[] == 5461
    end
    error("unknown AWFY microbenchmark: " * name)
end

function main(name, inner, outer)
    label = name == "nbody" ? "NBody" : uppercasefirst(name)
    prepare() = nothing
    function workload(io, state)
        for _ in 1:outer
            if name == "mandelbrot"
                checksum = awfy_mandelbrot(inner)
                @assert checksum == Dict(500=>191, 750=>50, 1=>128)[inner]
            elseif name == "nbody"
                bodies = make_bodies()
                offset_momentum!(bodies)
                for _ in 1:inner; advance_bodies!(bodies, 0.01, true); end
                @assert body_energy(bodies) == Dict(1=>-0.16907495402506745,
                    36000=>-0.16901424478751628, 250000=>-0.1690859889909308)[inner]
            else
                for _ in 1:inner
                    awfy_micro(name) || return false
                end
            end
        end
        return true
    end
    run_benchmark(prepare, workload, identity, result->println(label, ": PASS"))
end
