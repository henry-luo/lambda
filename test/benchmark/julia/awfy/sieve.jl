include("core.jl")
main("sieve", isempty(ARGS) ? 1 : parse(Int, ARGS[1]), length(ARGS) < 2 ? 1 : parse(Int, ARGS[2]))
