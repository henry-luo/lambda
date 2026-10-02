include("support.jl")
include("hyphen_core.jl")

function main()
    function prepare()
        tables, cases = read_fixture("hyphen_tables.json"), read_fixture("hyphen_cases.json")
        verifier = C_Hyphenator(tables)
        for (source, expected) in cases
            @assert m_hyphenate_text(verifier, source) == expected
        end
        return tables, cases
    end
    function workload(io, state)
        tables, cases = state
        checksum = 0
        for _ in 1:ROUNDS
            hyphenator = C_Hyphenator(tables)
            for (index, (source, _)) in enumerate0(cases)
                result = m_hyphenate_text(hyphenator, source)
                checksum = mod(checksum + length(result) * 29, MODULUS)
                if !isempty(result)
                    checksum = mod(checksum + ord0(get0(result, index % length(result))), MODULUS)
                end
            end
        end
        return checksum
    end
    run_benchmark(prepare, workload, result->result == 1183296, result->println("CHECKSUM:", result))
end
main()
