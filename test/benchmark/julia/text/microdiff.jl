include("support.jl")
include("microdiff_core.jl")

function main()
    function prepare()
        return [(make_snapshot(i % 2 == 0), make_snapshot(i % 2 != 0)) for i in 0:3]
    end
    function workload(io, state)
        checksum = 0
        for _ in 1:ROUNDS, (old, new) in state
            differences = diff(old, new)
            checksum = mod(checksum + length(differences) * 19, MODULUS)
            for difference in differences
                checksum = mod(checksum + length(difference["type"]) * 23 + length(difference["path"]), MODULUS)
            end
        end
        return checksum
    end
    run_benchmark(prepare, workload, result->result == 3278848, result->println("CHECKSUM:", result))
end
main()
