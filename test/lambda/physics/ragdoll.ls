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
    live_clock:match (physics.simulate(stage([<ragdoll *:physical,mode:'animated'>]),{steps:1}) ^ { ^ }) { case error: true default: false },
    unknown:match (physics.validate(stage([<ragdoll *:physical,bones:["missing"]>])) ^ { ^ }) { case error: true default: false },
    non_bone:match (physics.validate(stage([<ragdoll *:physical,bones:["base-body"]>])) ^ { ^ }) { case error: true default: false },
    duplicate:match (physics.validate(stage([<ragdoll *:physical,bones:["base","base"]>])) ^ { ^ }) { case error: true default: false },
    one_owner:match (physics.validate(stage([physical,<ragdoll id:"other",bones:["base"],mode:'physical'>])) ^ { ^ }) { case error: true default: false },
    empty:match (physics.validate(stage([<ragdoll *:physical,bones:[]>])) ^ { ^ }) { case error: true default: false },
    mode:match (physics.validate(stage([<ragdoll *:physical,mode:'broken'>])) ^ { ^ }) { case error: true default: false },
    typed_mode:match (physics.validate(stage([<ragdoll *:physical,mode:7>])) ^ { ^ }) { case error: true default: false },
    duration:match (physics.validate(stage([<ragdoll *:physical,'recovery-duration':0.0>])) ^ { ^ }) { case error: true default: false },
    child:match (physics.validate(stage([<ragdoll *:physical, <bone>>])) ^ { ^ }) { case error: true default: false }
};
[for (key,passed in checks where passed!=true) string(key)]
