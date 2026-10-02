include("../common.jl")
include("som.jl")
include("deltablue_core.jl")

function main(inner, outer)
    run_benchmark(()->C_DeltaBlue(), (io, bench)->begin
        for _ in 1:outer
            m_inner_benchmark_loop(bench, inner) || return false
        end
        return true
    end, identity, result->println("DeltaBlue: PASS"))
end
main(isempty(ARGS) ? 100 : parse(Int, ARGS[1]), length(ARGS) < 2 ? 20 : parse(Int, ARGS[2]))
