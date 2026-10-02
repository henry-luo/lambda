include("support.jl")
include("navier_stokes_core.jl")
mutable struct FluidState
    dens::Vector{Float64}; dens_prev::Vector{Float64}
    u::Vector{Float64}; u_prev::Vector{Float64}
    v::Vector{Float64}; v_prev::Vector{Float64}
    frames_till_add::Int; frames_between::Int
end
prepare_fluid() = FluidState((zeros(GRID_SIZE) for _ in 1:6)..., 0, 5)
function update_fluid!(state)
    fill!(state.u_prev, 0); fill!(state.v_prev, 0); fill!(state.dens_prev, 0)
    if state.frames_till_add == 0
        add_points(state.dens_prev, state.u_prev, state.v_prev)
        state.frames_till_add = state.frames_between; state.frames_between += 1
    else
        state.frames_till_add -= 1
    end
    vel_step(state.u, state.v, state.u_prev, state.v_prev, 0.1, 20)
    dens_step(state.dens, state.dens_prev, state.u, state.v, 0.1, 20)
    return state
end
function density_digest(state)
    digest = UInt32(2166136261)
    for value in state.dens
        digest = xor(digest, floor(Int, value * 1000) % UInt32)
        digest = (digest << 5) - digest + (digest >> 7)
    end
    return reinterpret(Int32, digest)
end
function verify_fluid(state)
    for _ in 2:15; update_fluid!(state); end
    return sum(trunc(Int, state.dens[i+1] * 10) for i in 7000:7099) == 77 && density_digest(state) == -257786486
end
run_benchmark(prepare_fluid, (io, state)->update_fluid!(state), verify_fluid,
              result->println("__NAVIER_DENSITY_DIGEST__:", density_digest(result)))
