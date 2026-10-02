include("core.jl")
main("mandelbrot", isempty(ARGS) ? 500 : parse(Int, ARGS[1]), length(ARGS) < 2 ? 1 : parse(Int, ARGS[2]))
