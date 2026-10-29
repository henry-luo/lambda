import physics: lambda.physics

fn stage(controls) => <scene3d id:"world", *[
    *controls,
    <skeleton id:"rig",
        physics.bind_bone(<bone id:"base">,"base-body")^
        physics.bind_bone(<bone id:"tip">,"tip-body")^
    >,
    physics.world({gravity:[0.0,-10.0,0.0],timestep:0.1}),
    physics.body("base-body",[physics.collider("base-shape",'sphere',{radius:0.1})],{position:[0.0,2.0,0.0]}),
    physics.body("tip-body",[physics.collider("tip-shape",'sphere',{radius:0.1})],{position:[0.0,3.0,0.0]}),
    physics.joint("link",'distance',"base-body","tip-body",{length:1.0})
]>
let physical=physics.ragdoll("person",["base","tip"],{mode:'physical'});
let trace=physics.simulate(stage([physical]),{steps:1})^;
let checks={
    physical:physics.validate(stage([physical]))^,
    animated:physics.validate(stage([<ragdoll *:physical,mode:'animated'>]))^,
    recover:physics.validate(stage([<ragdoll *:physical,mode:'recover'>]))^,
    fall:abs(trace.frames[1].bones[0].position[1]-1.9)<1e-9 and abs(trace.frames[1].bones[1].position[1]-2.9)<1e-9,
    live_clock:physics.simulate(stage([<ragdoll *:physical,mode:'animated'>]),{steps:1}) ^ { true } ~ { false },
    unknown:physics.validate(stage([<ragdoll *:physical,bones:["missing"]>])) ^ { true } ~ { false },
    non_bone:physics.validate(stage([<ragdoll *:physical,bones:["base-body"]>])) ^ { true } ~ { false },
    duplicate:physics.validate(stage([<ragdoll *:physical,bones:["base","base"]>])) ^ { true } ~ { false },
    one_owner:physics.validate(stage([physical,<ragdoll id:"other",bones:["base"],mode:'physical'>])) ^ { true } ~ { false },
    empty:physics.validate(stage([<ragdoll *:physical,bones:[]>])) ^ { true } ~ { false },
    mode:physics.validate(stage([<ragdoll *:physical,mode:'broken'>])) ^ { true } ~ { false },
    typed_mode:physics.validate(stage([<ragdoll *:physical,mode:7>])) ^ { true } ~ { false },
    duration:physics.validate(stage([<ragdoll *:physical,'recovery-duration':0.0>])) ^ { true } ~ { false },
    child:physics.validate(stage([<ragdoll *:physical, <bone>>])) ^ { true } ~ { false }
};
[for (key,passed in checks where passed!=true) string(key)]
