include("support.jl")
include("fast_diff_core.jl")

function main()
    function prepare()
        return read_fixture("fast_diff_pairs.json")
    end
    function workload(io, state)
        checksum = 0
        for _ in 1:ROUNDS, (left, right) in state
            parts = diff_main(left, right, true)
            checksum = mod(checksum + length(parts) * 17, MODULUS)
            for (operation, value) in parts
                checksum = mod(checksum + operation * 31 + length(value), MODULUS)
            end
        end
        return checksum
    end
    run_benchmark(prepare, workload, result->result == 390912, result->println("CHECKSUM:", result))
end
main()
