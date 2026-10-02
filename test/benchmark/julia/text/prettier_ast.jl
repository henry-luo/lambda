include("support.jl")
include("prettier_ast_core.jl")

function main()
    function prepare()
        return read_fixture("prettier_ast.json")
    end
    function workload(io, state)
        formatted = ""
        for _ in 1:ITERATIONS
            formatted = print_program(state)
        end
        return formatted
    end
    run_benchmark(prepare, workload, result->foldl((a,c)->mod(a * 31 + Int(c), 1000000007), result; init=0) == 56483873, result->begin print(result); println("prettier_ast: CHECKSUM:", foldl((a,c)->mod(a * 31 + Int(c), 1000000007), result; init=0)); end)
end
main()
