include("support.jl")
include("raytrace3d_core.jl")
run_benchmark(()->nothing, (io, state)->begin
    for _ in 1:benchmark_repeats(8)
        commands = canvas_commands(raytrace_scene())
        @assert length(commands) == 20970
    end
    return true
end, identity, result->nothing)
