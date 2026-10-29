import physics: lambda.physics

let scene=<scene3d
    physics.world({gravity:[0.0,-10.0,0.0],timestep:0.01})
    physics.joint("rod",'distance',"anchor","weight",{})
    physics.body("anchor",[physics.collider("anchor-shape",'box',{size:[0.2,0.2,0.2]})],{type:'fixed'})
    physics.body("weight",[physics.collider("weight-shape",'sphere',{radius:0.5})],{position:[0.0,-2.0,0.0]})
>;
let trace=physics.simulate(scene,{steps:100,sample_every:100})^;
let final_frame=trace.frames[1];
let spring=<scene3d <physics gravity:[0.0,0.0,0.0],timestep:0.01>
    <joint id:"spring",type:'spring','body-a':"spring-anchor",'body-b':"spring-body",length:1.0,stiffness:20.0,damping:3.0>
    <'rigid-body' id:"spring-anchor",type:'fixed'>
    physics.attach(<group id:"visual">,{id:"spring-body",type:'dynamic',colliders:[{id:"spring-shape",shape:'sphere',radius:0.5}]})^
>;
// metadata transforms come from the visual node; the joint precedes the referenced metadata body.
let metadata=<scene3d <physics gravity:[0.0,0.0,0.0]>
    <joint id:"metadata-joint",type:'distance','body-a':"metadata-anchor",'body-b':"logical-body">
    <'rigid-body' id:"metadata-anchor",type:'fixed'>
    physics.attach(<group id:"visual-body",position:[2.0,0.0,0.0]>,{id:"logical-body",type:'dynamic',colliders:[{id:"metadata-shape",shape:'sphere'}]})^
>;
let breaking=<scene3d <physics gravity:[0.0,-10.0,0.0],timestep:0.01>
    <joint id:"breakable",type:'distance','body-a':"anchor",'body-b':"ball",'break-force':0.01>
    <'rigid-body' id:"anchor",type:'fixed'>
    <'rigid-body' id:"ball",type:'dynamic',position:[0.0,-2.0,0.0],<collider id:"ball-shape",shape:'sphere'>>
>;
let broken=physics.simulate(breaking,{steps:2})^;
let invalid=<scene3d <physics><joint id:"missing",type:'distance','body-a':"absent",'body-b':"also-absent">>;
fn articulated(kind, options) => <scene3d <physics gravity:[0.0,0.0,0.0],timestep:0.01>
    physics.joint("articulation",kind,"base","moving",options)
    physics.body("base",[],{type:'fixed'})
    physics.body("moving",[physics.collider("moving-shape",'sphere')])
>
let point_scene=articulated('spherical',{}),fixed_scene=articulated('fixed',{});
let motor_options={limits:true,'lower-limit':-0.2,'upper-limit':0.2,motor:true,'motor-speed':1.0,'motor-max-force':1.0};
let hinge_trace=physics.simulate(articulated('hinge',motor_options),{steps:100,sample_every:100})^;
let slider_trace=physics.simulate(articulated('prismatic',motor_options),{steps:100,sample_every:100})^;
let cone_trace=physics.simulate(articulated('cone-twist',{*:motor_options,'cone-angle':0.4}),{steps:100,sample_every:100})^;
let checks={
    valid:physics.validate(scene)^,
    point:physics.validate(point_scene)^ and physics.simulate(point_scene,{steps:1})^.frames[1].joints[0].coordinate==null,
    fixed:physics.validate(fixed_scene)^,
    hinge:abs(hinge_trace.frames[1].joints[0].coordinate-0.2)<0.005,
    slider:abs(slider_trace.frames[1].joints[0].coordinate-0.2)<0.005,
    cone:abs(cone_trace.frames[1].joints[0].coordinate-0.2)<0.005,
    inferred:abs(trace.frames[0].joints[0].coordinate-2.0)<1e-10,
    supported:abs(final_frame.bodies[1].position[1]+2.0)<1e-10 and final_frame.joints[0].linear_error<1e-10,
    reaction:abs(final_frame.joints[0].force[1]-final_frame.bodies[1].mass*10.0)<1e-8,
    enabled:final_frame.joints[0].enabled and not final_frame.joints[0].broken,
    metadata:physics.validate(metadata)^,
    break_event:len(broken.events)==1 and broken.events[0].type=="joint-break" and broken.events[0].joint=="breakable",
    detached:broken.frames[1].joints[0].broken and not broken.frames[2].joints[0].enabled and broken.frames[2].bodies[1].position[1]< -2.0,
    bad:physics_validate(invalid) is error,
    coincident:physics_validate(spring) is error
};
[for (key,passed in checks where passed!=true) string(key)]
