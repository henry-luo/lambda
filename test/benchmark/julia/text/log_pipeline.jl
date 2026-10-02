include("support.jl")
include("log_pipeline_core.jl")

function main()
    function prepare()
        return [make_log_line(i) for i in 0:COUNT-1]
    end
    function workload(io, state)
        checksum = 0
        for round in 0:ROUNDS-1
            groups, accepted, rejected = process_logs(state)
            checksum = mod(checksum + accepted * 31 + rejected * 17 + groups["api"]["total_latency"] + groups["worker"]["total_bytes"] + round, MODULUS)
        end
        return checksum
    end
    run_benchmark(prepare, workload, result->result == 292634526, result->println("log_pipeline: CHECKSUM:", result))
end
main()
