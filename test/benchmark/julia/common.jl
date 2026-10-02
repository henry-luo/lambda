# shared timing and deterministic input helpers for the native Julia ports.
using Printf

function run_benchmark(prepare, workload, verify, report)
    # warm the same callable with fresh inputs; mutated warmup state is never reused.
    output = IOBuffer()
    warmup = parse(Int, get(ENV, "JULIA_BENCH_WARMUP", "1"))
    @assert warmup in (0, 1) "JULIA_BENCH_WARMUP must be 0 or 1"
    if warmup == 1
        warm_result = workload(output, prepare())
        @assert verify(warm_result) "Julia benchmark warmup returned a wrong result"
    end
    truncate(output, 0)
    seekstart(output)
    state = prepare()
    started = time_ns()
    result = workload(output, state)
    elapsed_ms = (time_ns() - started) / 1e6
    @assert verify(result) "Julia benchmark returned a wrong result"
    print(String(take!(output)))
    report(result)
    println("__TIMING__:", elapsed_ms)
end

function checked_benchmark(label, workload, expected; prepare=()->nothing)
    run_benchmark(prepare, (io, state)->workload(state),
                  result->result == expected, result->println(label, ": PASS"))
end

next_lcg(seed) = (seed * 1664525 + 1013904223) % 1000000

mutable struct TreeNode
    left::Union{Nothing,TreeNode}
    right::Union{Nothing,TreeNode}
end
make_tree(depth) = depth == 0 ? TreeNode(nothing, nothing) :
                              TreeNode(make_tree(depth - 1), make_tree(depth - 1))
check_tree(node) = node.left === nothing ? 1 :
                  1 + check_tree(node.left) + check_tree(node.right)

function prime_sieve(limit)
    flags = ones(UInt8, limit + 1)
    flags[1] = flags[2] = 0
    i = 2
    while i * i <= limit
        if flags[i + 1] != 0
            j = i * i
            while j <= limit
                flags[j + 1] = 0
                j += i
            end
        end
        i += 1
    end
    count = 0
    for i in 2:limit
        count += flags[i + 1]
    end
    return count
end

benchmark_repeats(default) = isempty(ARGS) ? default : parse(Int, ARGS[1])
