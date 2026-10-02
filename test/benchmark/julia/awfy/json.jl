include("../common.jl")
include("som.jl")
include("json_core.jl")

function main(inner, outer)
    run_benchmark(()->C_Json(), (io, bench)->begin
        for _ in 1:outer
            m_inner_benchmark_loop(bench, inner) || return false
        end
        return true
    end, identity, result->println("Json: PASS"))
end
main(isempty(ARGS) ? 1 : parse(Int, ARGS[1]), length(ARGS) < 2 ? 1 : parse(Int, ARGS[2]))
