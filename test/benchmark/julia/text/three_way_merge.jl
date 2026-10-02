include("support.jl")
include("three_way_merge_core.jl")

function main()
    function prepare()
        base = ["section $i records the base document with stable words for merging and review" for i in 0:LINE_COUNT-1]
        return base, make_variant(base, "left"), make_variant(base, "right")
    end
    function workload(io, state)
        checksum = 0
        for round in 0:ROUNDS-1
            merged = merge_lines(state...)
            checksum = mod(checksum + length(merged) * 31 + ord0(get0(merged, (round * 37) % length(merged))), MODULUS)
        end
        return checksum
    end
    run_benchmark(prepare, workload, result->result == 342313356, result->println("three_way_merge: CHECKSUM:", result))
end
main()
