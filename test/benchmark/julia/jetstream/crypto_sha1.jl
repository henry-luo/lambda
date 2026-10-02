include("support.jl")
include("crypto_sha1_core.jl")
run_benchmark(()->nothing, (io, state)->begin
    for _ in 1:benchmark_repeats(25)
        run() || return false
    end
    return true
end, identity, result->nothing)
