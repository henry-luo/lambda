include("support.jl")

function load_words()
    words = Set(["Lambda", "Radiant"])
    for line in eachline("lmd/package/doc/spelling/data/en-US.dict")
        previous = ""
        delta = false
        for token in eachmatch(r"([^;|]+)([;|]?)", line)
            encoded, delimiter = token.captures
            if delta
                parts = split(encoded, ','; limit=2)
                if length(parts) == 2
                    previous = join(Iterators.take(previous, parse(Int, parts[1]))) * parts[2]
                else
                    previous *= encoded
                end
            else
                previous = String(encoded)
            end
            push!(words, previous)
            delta = delimiter == ";"
        end
    end
    return words
end

function prepare()
    (load_words(), read("test/benchmark/text/spelling_document.txt", String),
     read_fixture("spelling_expected.json"))
end

function workload(io, state)
    words, text, expected = state
    issues = []
    checked = 0
    for m in eachmatch(r"[A-Za-z]+(?:'[A-Za-z]+)*", text)
        word = String(m.match)
        checked += 1
        lower = lowercase(word)
        folded = word == uppercase(word) || word == uppercase(first(word)) * lower[2:end]
        if !(word in words) && !(folded && lower in words)
            push!(issues, Dict("word"=>word, "start"=>m.offset-1, "end"=>m.offset-1+ncodeunits(word)))
        end
    end
    return checked == 7110 && issues == expected
end

run_benchmark(prepare, workload, identity,
              _ -> println("spelling: CHECKED:7110 ISSUES:720"))
