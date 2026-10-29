import physics: lambda.physics

fn scene(bones) => <scene3d
    <skeleton id:"rig", *bones>
    physics.world({gravity:[0.0,-10.0,0.0],timestep:0.1})
    physics.body("limb",[physics.collider("shape",'sphere',{radius:0.1})],{position:[0.0,2.0,0.0]})
>
let bound=physics.bind_bone(<bone id:"bone",position:[0.0,1.5,0.0]>,"limb",{'physics-offset':[0.0,0.5,0.0]})^;
let trace=physics.simulate(scene([bound]),{steps:1})^;
let disabled=physics.simulate(scene([<bone *:bound,'physics-enabled':false>]),{steps:1})^;
let checks={
    forward:physics.validate(scene([bound]))^,
    ordinary_world:trace.ticks==1 and abs(trace.frames[1].bodies[0].position[1]-1.9)<1e-10,
    copied_pose:trace.frames[1].bones[0].id=="bone" and trace.frames[1].bones[0].body=="limb" and abs(trace.frames[1].bones[0].position[1]-1.4)<1e-10,
    no_false_pose:not disabled.frames[1].bones[0].enabled and disabled.frames[1].bones[0].position==null,
    disabled:physics.validate(scene([<bone *:bound,'physics-enabled':false>]))^,
    kind:physics.bind_bone(<group id:"bad">,"limb") ^ { true } ~ { false },
    missing:physics.validate(scene([<bone id:"bone",'physics-body':"missing">])) ^ { true } ~ { false },
    non_body:physics.validate(scene([<bone id:"bone",'physics-body':"rig">])) ^ { true } ~ { false },
    one_owner:physics.validate(scene([bound,<bone id:"second",'physics-body':"limb">])) ^ { true } ~ { false },
    offset:physics.validate(scene([<bone *:bound,'physics-quaternion':[0.0,0.0,0.0,0.0]>])) ^ { true } ~ { false },
    rigid:physics.validate(scene([<bone *:bound,scale:[2.0,1.0,1.0]>])) ^ { true } ~ { false },
    identified:physics.validate(scene([<bone 'physics-body':"limb">])) ^ { true } ~ { false }
};
[for (key,passed in checks where passed!=true) string(key)]
