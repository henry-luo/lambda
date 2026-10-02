include("../common.jl")
include("../nbody.jl")

function binarytrees(io, n)
    max_depth = max(6, n)
    stretch = max_depth + 1
    println(io, "stretch tree of depth ", stretch, "\t check: ", check_tree(make_tree(stretch)))
    long_lived = make_tree(max_depth)
    checks = Int[]
    for depth in 4:2:max_depth
        iterations = 1 << (max_depth - depth + 4)
        total = 0
        for _ in 1:iterations; total += check_tree(make_tree(depth)); end
        push!(checks, total)
        println(io, iterations, "\t trees of depth ", depth, "\t check: ", total)
    end
    final = check_tree(long_lived)
    println(io, "long lived tree of depth ", max_depth, "\t check: ", final)
    return (final, checks)
end

function fannkuch(io, n)
    perm, perm1, count = zeros(Int32, n), Int32.(0:n-1), zeros(Int32, n)
    max_flips, checksum, perm_count, r = 0, 0, 0, n
    while true
        while r != 1
            count[r] = r
            r -= 1
        end
        for i in 1:n; perm[i] = perm1[i]; end
        flips, k = 0, Int(perm[1])
        while k != 0
            lo, hi = 1, k + 1
            while lo < hi
                perm[lo], perm[hi] = perm[hi], perm[lo]
                lo += 1; hi -= 1
            end
            flips += 1
            k = Int(perm[1])
        end
        max_flips = max(max_flips, flips)
        checksum += perm_count % 2 == 0 ? flips : -flips
        perm_count += 1
        found, r = false, 1
        while r < n
            perm0 = perm1[1]
            for i in 1:r; perm1[i] = perm1[i + 1]; end
            perm1[r + 1] = perm0
            count[r + 1] -= 1
            if count[r + 1] > 0
                found = true
                break
            end
            r += 1
        end
        !found && break
    end
    println(io, checksum)
    println(io, "Pfannkuchen(", n, ") = ", max_flips)
    return (checksum, max_flips)
end

const ALU = "GGCCGGGCGCGGTGGCTCACGCCTGTAATCCCAGCACTTTGGGAGGCCGAGGCGGGCGGATCACCTGAGGTCAGGAGTTCGAGACCAGCCTGGCCAACATGGTGAAACCCCGTCTCTACTAAAAATACAAAAATTAGCCGGGCGTGGTGGCGCGCGCCTGTAATCCCAGCTACTCGGGAGGCTGAGGCAGGAGAATCGCTTGAACCCGGGAGGCGGAGGTTGCAGTGAGCCGAGATCGCGCCACTGCACTCCAGCCTGGGCGACAGAGCGAGACTCCGTCTCAAAAA"
function fasta_repeat(io, n)
    println(io, ">ONE Homo sapiens alu")
    k, line = 1, ""
    for _ in 1:n
        line *= ALU[k]
        k = k % length(ALU) + 1
        if length(line) == 60
            println(io, line); line = ""
        end
    end
    !isempty(line) && println(io, line)
end
function fasta_random(io, id, desc, chars, probabilities, n, seed)
    println(io, ">", id, " ", desc)
    total = 0.0
    for i in eachindex(probabilities)
        total += probabilities[i]
        probabilities[i] = total
    end
    line = ""
    for _ in 1:n
        seed = (seed * 3877 + 29573) % 139968
        r = seed / 139968
        for i in eachindex(chars)
            if r < probabilities[i]
                line *= chars[i]
                break
            end
        end
        if length(line) == 60
            println(io, line); line = ""
        end
    end
    !isempty(line) && println(io, line)
    return seed
end
function fasta(io, n)
    fasta_repeat(io, n * 2)
    seed = fasta_random(io, "TWO", "IUB ambiguity codes", collect("acgtBDHKMNRSVWY"),
                        [0.27,0.12,0.12,0.27,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02,0.02], n * 3, 42)
    return fasta_random(io, "THREE", "Homo sapiens frequency", collect("acgt"),
                        [0.3029549426680,0.1979883004921,0.1975473066391,0.3015094502008], n * 5, seed)
end

function extract_three(text)
    seq, in_three = "", false
    for line in split(text, '\n')
        if startswith(line, ">")
            in_three && return seq
            startswith(line, ">THREE") && (in_three = true)
        elseif in_three && !isempty(line)
            seq *= uppercase(line)
        end
    end
    return seq
end
function count_kmers(seq, k)
    counts = Dict{String,Int}()
    for i in 1:length(seq)-k+1
        kmer = seq[i:i+k-1]
        counts[kmer] = get(counts, kmer, 0) + 1
    end
    return counts
end
function knucleotide(io, seq)
    for k in 1:2
        counts = count_kmers(seq, k)
        entries = sort!(collect(counts); by=p->(-p.second, p.first))
        for (kmer, count) in entries
            @printf(io, "%s %.3f\n", kmer, count * 100.0 / (length(seq) - k + 1))
        end
        println(io)
    end
    results = Int[]
    for kmer in ("GGT", "GGTA", "GGTATT", "GGTATTTTAATT", "GGTATTTTAATTTATAGT")
        count = get(count_kmers(seq, length(kmer)), kmer, 0)
        push!(results, count)
        println(io, count, '\t', kmer)
    end
    return results
end

function mandelbrot_checksum(n)
    checksum = 0
    for y in 0:n-1
        bits, bit_num = 0, 0
        for x in 0:n-1
            cr, ci = 2.0 * x / n - 1.5, 2.0 * y / n - 1.0
            zr, zi, inside = 0.0, 0.0, 1
            for _ in 1:50
                zr, zi = zr * zr - zi * zi + cr, 2.0 * zr * zi + ci
                if zr * zr + zi * zi > 4.0
                    inside = 0
                    break
                end
            end
            bits = (bits << 1) | inside
            bit_num += 1
            if bit_num == 8
                checksum = xor(checksum, bits)
                bits, bit_num = 0, 0
            end
        end
        if bit_num > 0
            checksum = xor(checksum, bits << (8 - bit_num))
        end
    end
    return checksum
end

function pidigits(io, n)
    q, r, s, t, k = big(1), big(0), big(0), big(1), big(0)
    i, digits, all_digits = 0, "", ""
    while i < n
        k += 1
        k2 = k * 2 + 1
        q, r, s, t = q * k, (2 * q + r) * k2, s * k, (2 * s + t) * k2
        if q <= r
            fd3, fd4 = fld(3 * q + r, 3 * s + t), fld(4 * q + r, 4 * s + t)
            if fd3 == fd4
                digits *= string(fd3)
                all_digits *= string(fd3)
                i += 1
                if i % 10 == 0
                    println(io, digits, "\t:", i)
                    digits = ""
                end
                r = (r - fd3 * t) * 10
                q *= 10
            end
        end
    end
    !isempty(digits) && println(io, rpad(digits, 10), "\t:", i)
    return all_digits
end

const DNA_PATTERNS = ["agggtaaa|tttaccct", "[cgt]gggtaaa|tttaccc[acg]", "a[act]ggtaaa|tttacc[agt]t",
                      "ag[act]gtaaa|tttac[agt]ct", "agg[act]taaa|ttta[agt]cct", "aggg[acg]aaa|ttt[cgt]ccct",
                      "agggt[cgt]aa|tt[acg]taccct", "agggta[cgt]a|t[acg]ataccct", "agggtaa[cgt]|[acg]aataccct"]
function regexredux(text)
    original = length(text)
    text = replace(text, r">[^\n]*\n"=>"")
    text = replace(text, "\n"=>"")
    clean = length(text)
    counts = [length(collect(eachmatch(Regex(p), text))) for p in DNA_PATTERNS]
    for (re, repl) in [(r"B","(c|g|t)"),(r"D","(a|g|t)"),(r"H","(a|c|t)"),(r"K","(g|t)"),
                       (r"M","(a|c)"),(r"N","(a|c|g|t)"),(r"R","(a|g)"),(r"S","(c|g)"),
                       (r"V","(a|c|g)"),(r"W","(a|t)"),(r"Y","(c|t)")]
        text = replace(text, re=>repl)
    end
    return (counts, original, clean, length(text))
end

const COMPLEMENT = Dict(zip(collect("ATCGMKRYVBHDWSN"), collect("TAGCKMYRBVDHWSN")))
function reverse_complement(io, header, seq)
    println(io, header)
    upper, comp = uppercase(seq), ""
    for ch in upper
        comp *= get(COMPLEMENT, ch, ch)
    end
    rev = join(reverse(collect(comp)))
    for pos in 1:60:length(rev)
        println(io, rev[pos:min(pos + 59, length(rev))])
    end
    return length(rev)
end
function revcomp(io, lines)
    header, seq, count = "", "", 0
    for line in lines
        if startswith(line, ">")
            !isempty(seq) && (count += reverse_complement(io, header, seq))
            header, seq = line, ""
        elseif !isempty(line)
            seq *= uppercase(line)
        end
    end
    !isempty(seq) && (count += reverse_complement(io, header, seq))
    return count
end

spectral_a(i, j) = 1.0 / ((i + j) * (i + j + 1) / 2 + i + 1)
function spectral_multiply!(n, v, av, transposed)
    for i in 0:n-1
        av[i + 1] = 0.0
        for j in 0:n-1
            av[i + 1] += (transposed ? spectral_a(j, i) : spectral_a(i, j)) * v[j + 1]
        end
    end
end
function spectral_atav!(n, v, atav)
    u = zeros(n)
    spectral_multiply!(n, v, u, false)
    spectral_multiply!(n, u, atav, true)
end
function spectralnorm(n)
    u, v = ones(n), zeros(n)
    for _ in 1:10
        spectral_atav!(n, u, v)
        spectral_atav!(n, v, u)
    end
    vbv, vv = 0.0, 0.0
    for i in 1:n
        vbv += u[i] * v[i]; vv += v[i] * v[i]
    end
    return sqrt(vbv / vv)
end

function main(name)
    numeric_defaults = Dict("binarytrees"=>10, "fannkuch"=>7, "fasta"=>1000,
                            "mandelbrot"=>500, "nbody"=>36000, "pidigits"=>30, "spectralnorm"=>100)
    n = haskey(numeric_defaults, name) ? (isempty(ARGS) ? numeric_defaults[name] : parse(Int, ARGS[1])) : 0
    input = isempty(ARGS) ? "test/benchmark/beng/input/fasta_1000.txt" : ARGS[1]
    prepare() = name == "knucleotide" ? extract_three(read(input, String)) :
                name == "revcomp" ? split(read(input, String), '\n') :
                name == "regexredux" ? read(input, String) :
                name == "nbody" ? make_bodies() : nothing
    function workload(io, state)
        name == "binarytrees" && return binarytrees(io, n)
        name == "fannkuch" && return fannkuch(io, n)
        name == "fasta" && return fasta(io, n)
        name == "knucleotide" && return knucleotide(io, state)
        name == "mandelbrot" && return mandelbrot_checksum(n)
        name == "pidigits" && return pidigits(io, n)
        name == "regexredux" && return regexredux(state)
        name == "revcomp" && return revcomp(io, state)
        name == "spectralnorm" && return spectralnorm(n)
        if name == "nbody"
            offset_momentum!(state)
            initial = body_energy(state)
            @printf(io, "%.9f\n", initial)
            for _ in 1:n; advance_bodies!(state, 0.01); end
            final = body_energy(state)
            @printf(io, "%.9f\n", final)
            return (initial, final)
        end
        error("unknown BENG benchmark: " * name)
    end
    function verify(result)
        name == "binarytrees" && return result[1] == (1 << (max(6, n) + 1)) - 1
        name == "fannkuch" && return n == 7 ? result == (228, 16) : result[2] >= 0
        name == "pidigits" && return length(result) == n && startswith(result, first("314159265358979323846264338327950288419716939937510", min(n, 51)))
        name == "spectralnorm" && return 1 < result < 2
        name == "nbody" && return all(isfinite, result)
        name == "regexredux" && return result[2] >= result[3] && result[4] >= result[3]
        name == "knucleotide" && return all(x->x >= 0, result)
        return result >= 0
    end
    run_benchmark(prepare, workload, verify, result->begin
        if name == "mandelbrot"; println(result)
        elseif name == "spectralnorm"; @printf("%.9f\n", result)
        elseif name == "regexredux"
            for (pattern, count) in zip(DNA_PATTERNS, result[1]); println(pattern, " ", count); end
            println()
            for v in result[2:4]; println(v); end
        end
    end)
end
