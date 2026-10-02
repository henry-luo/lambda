include("support.jl")
include("cube3d_core.jl")
run_benchmark(()->nothing, (io, state)->begin
    for _ in 1:benchmark_repeats(8)
        run() || return false
    end
    return true
end, identity, result->nothing)
