include("../common.jl")
include("../awfy/som.jl")
include("../awfy/deltablue_core.jl")
run_benchmark(()->nothing, (io, state)->begin
    for _ in 1:benchmark_repeats(20)
        C__Planner__chain_test(100, false)
        C__Planner__projection_test(100, 0)
    end
    return true
end, identity, result->nothing)
