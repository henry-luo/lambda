let scene=<scene3d
    <physics gravity: [0.0,-10.0,0.0], timestep: 0.01>
    <'rigid-body' id: "ball", type: 'dynamic', position: [0.0,10.0,0.0],
        <collider id: "ball-shape", shape: 'sphere', radius: 0.5, density: 2.0>
    >
>;
let trace=physics_simulate(scene,{steps:100,sample_every:25});
let final_state=trace.frames[len(trace.frames)-1].bodies[0];
let selected=physics_simulate(scene,{steps:0,bodies:['ball']});
let checks={selection:len(selected.frames[0].bodies)==1,frames:len(trace.frames)==5,tick:trace.ticks==100,
    height:abs(final_state.position[1]-4.95)<1e-9,velocity:abs(final_state.velocity[1]+10.0)<1e-9,
    mass:abs(final_state.mass-1.0471975511965976)<1e-12,events:len(trace.events)==0};
[for (key,passed in checks where passed!=true) string(key)]
