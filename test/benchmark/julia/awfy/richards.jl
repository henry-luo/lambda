include("../common.jl")
include("som.jl")
include("richards_core.jl")

function main(inner, outer)
    run_benchmark(()->C_Richards(), (io, bench)->begin
        for _ in 1:outer
            m_inner_benchmark_loop(bench, inner) || return false
        end
        return true
    end, identity, result->println("Richards: PASS"))
end
main(isempty(ARGS) ? 50 : parse(Int, ARGS[1]), length(ARGS) < 2 ? 1 : parse(Int, ARGS[2]))
