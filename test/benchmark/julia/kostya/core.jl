include("../common.jl")

const B64_TABLE = "ABCDEFGHIJKLMNOPQRSTUVWXYZabcdefghijklmnopqrstuvwxyz0123456789+/"
function b64_encode(bytes)
    parts = String[]
    n = length(bytes)
    i = 1
    while i + 2 <= n
        b0, b1, b2 = Int(bytes[i]), Int(bytes[i + 1]), Int(bytes[i + 2])
        push!(parts, string(B64_TABLE[(b0 >> 2) + 1]),
              string(B64_TABLE[(((b0 & 3) << 4) | (b1 >> 4)) + 1]),
              string(B64_TABLE[(((b1 & 15) << 2) | (b2 >> 6)) + 1]),
              string(B64_TABLE[(b2 & 63) + 1]))
        i += 3
    end
    if i == n
        b0 = Int(bytes[i])
        push!(parts, string(B64_TABLE[(b0 >> 2) + 1]), string(B64_TABLE[((b0 & 3) << 4) + 1]), "==")
    elseif i + 1 == n
        b0, b1 = Int(bytes[i]), Int(bytes[i + 1])
        push!(parts, string(B64_TABLE[(b0 >> 2) + 1]),
              string(B64_TABLE[(((b0 & 3) << 4) | (b1 >> 4)) + 1]),
              string(B64_TABLE[((b1 & 15) << 2) + 1]), "=")
    end
    return join(parts)
end

function base64_workload()
    bytes = fill(UInt8(97), 10000)
    encoded, decoded_len = "", 0
    for _ in 1:100
        encoded = b64_encode(bytes)
        decoded_len = (length(encoded) >> 2) * 3
        endswith(encoded, "=") && (decoded_len -= 1)
        endswith(encoded, "==") && (decoded_len -= 1)
    end
    return (length(encoded), decoded_len)
end

function build_jumps(prog)
    jumps = zeros(Int32, length(prog))
    stack = Int[]
    for (i, c) in enumerate(prog)
        if c == '['
            push!(stack, i)
        elseif c == ']'
            j = pop!(stack)
            jumps[i], jumps[j] = j, i
        end
    end
    return jumps
end

function run_bf(prog, jumps)
    tape = zeros(UInt8, 30000)
    dp, ip = 1, 1
    output = Char[]
    while ip <= length(prog)
        op = prog[ip]
        if op == '+'
            tape[dp] += UInt8(1)
        elseif op == '-'
            tape[dp] -= UInt8(1)
        elseif op == '>'
            dp += 1
        elseif op == '<'
            dp -= 1
        elseif op == '.'
            push!(output, Char(tape[dp]))
        elseif op == '[' && tape[dp] == 0
            ip = Int(jumps[ip])
        elseif op == ']' && tape[dp] != 0
            ip = Int(jumps[ip])
        end
        ip += 1
    end
    return join(output)
end

function brainfuck_workload()
    prog = "++++++++[>++++[>++>+++>+++>+<<<<-]>+>+>->>+[<]<-]>>.>---.+++++++..+++.>>.<-.<.+++.------.--------.>>+.>++."
    jumps = build_jumps(prog)
    output = ""
    for _ in 1:10000
        output = run_bf(prog, jumps)
    end
    return output
end

function matmul_inputs()
    n = 200
    a, b, c = zeros(n * n), zeros(n * n), zeros(n * n)
    seed = 42
    for i in eachindex(a)
        seed = next_lcg(seed)
        a[i] = (seed % 2000) / 1000.0 - 1.0
        seed = next_lcg(seed)
        b[i] = (seed % 2000) / 1000.0 - 1.0
    end
    return (a, b, c, n)
end

function matmul_workload(state)
    a, b, c, n = state
    # preserve the row-major i/j/k loops rather than substituting a BLAS call.
    for i in 0:n-1, j in 0:n-1
        s = 0.0
        for k in 0:n-1
            s += a[i * n + k + 1] * b[k * n + j + 1]
        end
        c[i * n + j + 1] = s
    end
    return c
end

function levenshtein(s1, s2)
    n, m = length(s1), length(s2)
    prev, curr = Int32.(0:m), zeros(Int32, m + 1)
    for i in 1:n
        curr[1] = i
        c1 = s1[i]
        for j in 1:m
            cost = c1 == s2[j] ? 0 : 1
            curr[j + 1] = min(prev[j + 1] + 1, curr[j] + 1, prev[j] + cost)
        end
        prev, curr = curr, prev
    end
    return prev[m + 1]
end

function json_gen()
    seed = 42
    parts = ["["]
    for i in 0:999
        i > 0 && push!(parts, ",")
        seed = next_lcg(seed); id = seed % 10000
        seed = next_lcg(seed); x = trunc(Int, ((seed % 20000) - 10000) / 100.0)
        seed = next_lcg(seed); y = trunc(Int, ((seed % 20000) - 10000) / 100.0)
        seed = next_lcg(seed); score = seed % 100
        coord = "{\"x\":" * string(x) * ",\"y\":" * string(y) * "}"
        obj = "{\"id\":" * string(id) * ",\"score\":" * string(score) *
              ",\"coord\":" * coord * ",\"active\":true}"
        push!(parts, obj)
    end
    push!(parts, "]")
    return length(join(parts))
end

function collatz_len(n)
    steps = 1
    while n != 1
        n = n % 2 == 0 ? n ÷ 2 : 3 * n + 1
        steps += 1
    end
    return steps
end

function kostya_workload(name)
    name == "base64" && return base64_workload()
    name == "brainfuck" && return brainfuck_workload()
    name == "primes" && return prime_sieve(1000000)
    if name == "levenshtein"
        return (levenshtein("kitten", "sitting"), levenshtein("saturday", "sunday"),
                levenshtein(repeat("a", 500), repeat("b", 500)),
                levenshtein(repeat("ab", 200), repeat("ba", 200)))
    end
    if name == "json_gen"
        result = 0
        for _ in 1:10
            result = json_gen()
        end
        return result
    end
    if name == "collatz"
        max_len, max_start = 0, 0
        for i in 1:999999
            clen = collatz_len(i)
            if clen > max_len
                max_len, max_start = clen, i
            end
        end
        return max_start
    end
    error("unknown Kostya benchmark: " * name)
end

function main(name)
    if name == "matmul"
        run_benchmark(()->nothing, (io, state)->begin
                          values = matmul_workload(matmul_inputs())
                          total = 0.0
                          for v in values; total += v; end
                          return floor(Int, total)
                      end,
                      isfinite,
                      result->begin
                          println("matmul: sum=", result)
                          println("matmul: DONE")
                      end)
        return
    end
    expected = Dict{String,Any}("base64"=>(13336, 10000), "brainfuck"=>"Hello World!\n",
                               "primes"=>78498, "levenshtein"=>(3, 3, 500, 2),
                               "json_gen"=>nothing, "collatz"=>837799)
    run_benchmark(()->nothing, (io, state)->kostya_workload(name),
                  result->name == "json_gen" ? result > 0 : result == expected[name],
                  result->begin
                      if name == "base64"
                          println("base64: encoded_len=", result[1], " decoded_len=", result[2])
                      elseif name == "brainfuck"
                          println(result)
                          return
                      elseif name == "primes"
                          println("primes: PASS (", result, ")")
                          return
                      elseif name == "collatz"
                          println("collatz: PASS (start=", result, ")")
                          return
                      elseif name == "json_gen"
                          println("json_gen: length=", result)
                      elseif name == "levenshtein"
                          labels = ("kitten,sitting", "saturday,sunday", "aaa...,bbb...", "ababab...,babab...")
                          for i in 1:4
                              println("levenshtein: d(", labels[i], ")=", result[i])
                          end
                      end
                      println(name, ": PASS")
                  end)
end
