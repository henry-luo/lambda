include("core.jl")
main("nbody", isempty(ARGS) ? 36000 : parse(Int, ARGS[1]), length(ARGS) < 2 ? 1 : parse(Int, ARGS[2]))
