include("support.jl")
include("richards_core.jl")
run_benchmark(()->nothing, (io, state)->begin
    for _ in 1:benchmark_repeats(50)
        run_richards() || return false
    end
    return true
end, identity, result->nothing)
