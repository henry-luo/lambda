include("support.jl")
include("text_search_core.jl")

function main()
    function prepare()
        corpus = join(("record-$i alpha aaaaaaaaaaaaaaaaaaaaaaaa token-$(i % 23) omega needle-$(i % 11)" for i in 0:511), "\n")
        patterns = ["record-0 alpha", "record-2048 alpha", "token-22 omega", "needle-10", "omega needle-7", "alpha aaaaaaaaaaaaaaaaaaaaaaaa token-3", "missing-marker", "record-2047 omega"]
        return Int.(collect(corpus)), [Int.(collect(pattern)) for pattern in patterns]
    end
    function workload(io, state)
        corpus, patterns = state
        checksum = 0
        for round in 0:ROUNDS-1, (index, pattern) in enumerate0(patterns)
            start = (round * 17 + index * 13) % 97
            naive = naive_search(corpus, pattern, start)
            kmp = kmp_search(corpus, pattern, start)
            bm = boyer_moore_search(corpus, pattern, start)
            @assert naive == kmp == bm
            checksum = mod(checksum + (naive + 2) * (index + 3) + (round + 1) * 7, MODULUS)
        end
        return checksum
    end
    run_benchmark(prepare, workload, result->result == 91395120, result->println("text_search: CHECKSUM:", result))
end
main()
