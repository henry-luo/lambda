include("../common.jl")
include("../nbody.jl")
run_benchmark(()->nothing, (io, state)->begin
    for _ in 1:benchmark_repeats(8)
        energy = 0.0
        for n in (3, 6, 12, 24)
            bodies = make_bodies()
            offset_momentum!(bodies)
            energy += body_energy(bodies)
            for _ in 1:n*100; advance_bodies!(bodies, 0.01, true); end
            energy += body_energy(bodies)
        end
        @assert energy == -1.3524862408537381
    end
    return true
end, identity, result->nothing)
