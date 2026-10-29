import physics: lambda.physics

fn controlled(enabled) => <scene3d
    physics.world({gravity:[0.0,0.0,0.0],timestep:0.01})
    physics.joint("link",'distance',"base","limb",{length:2.0,enabled:enabled})
    physics.body("base",[],{type:'kinematic-position',position:[0.0,5.0,0.0]})
    physics.body("limb",[physics.collider("limb-shape",'sphere')],{type:'kinematic-position',position:[0.0,3.0,0.0]})
>
let trace=physics.simulate(controlled(false),{steps:1,commands:[{tick:0,commands:[{type:'target',body:"limb",position:[1.0,3.0,0.0],rotation:[0.0,0.1,0.0]}]}]})^;
let fixed=<scene3d physics.world({gravity:[0.0,0.0,0.0]}) physics.body("fixed",[],{type:'fixed'})>;
let checks={
    inactive:physics.validate(controlled(false))^ and not trace.frames[1].joints[0].enabled,
    target:trace.ticks==1 and trace.frames[1].bodies[1].position==[1.0,3.0,0.0],
    measured:abs(trace.frames[1].bodies[1].velocity[0]-100.0)<1e-9 and abs(trace.frames[1].bodies[1].angular_velocity[1]-10.0)<1e-9,
    active_requires_dynamic:physics.validate(controlled(true)) ^ { true } ~ { false },
    fixed_stops:physics.simulate(fixed,{steps:1,commands:[{tick:0,commands:[{type:'velocity',body:"fixed",value:[1.0,0.0,0.0]}]}]}) ^ { true } ~ { false },
    fixed_does_not_spin:physics.simulate(fixed,{steps:1,commands:[{tick:0,commands:[{type:'angular_velocity',body:"fixed",value:[0.0,1.0,0.0]}]}]}) ^ { true } ~ { false }
};
[for (key,passed in checks where passed!=true) string(key)]
