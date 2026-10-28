import physics: lambda.physics

let scene=<scene3d
    <physics gravity:[0.0,-10.0,0.0],timestep:0.01>
    <'rigid-body' id:"floor",type:'fixed',position:[0.0,-0.5,0.0],
        <collider id:"floor-shape",shape:'box',size:[20.0,1.0,20.0]>>
    <'rigid-body' id:"capsule",type:'dynamic',position:[0.0,3.0,0.0],
        <collider id:"capsule-shape",shape:'capsule',radius:0.25,height:1.0>>
>;
let trace=physics.simulate(scene,{steps:1000,sample_every:100,bodies:["capsule"]})^;
let settled=trace.frames[10].bodies[0];
let queries=<scene3d <physics>
    <'rigid-body' id:"capsule",type:'fixed',
        <collider id:"shape",shape:'capsule',radius:0.25,height:1.0>>
>;
let hits=physics.query(queries,[
    {type:"ray",origin:[0.0,3.0,0.0],direction:[0.0,-1.0,0.0],distance:4.0},
    {type:"ray",origin:[2.0,0.0,0.0],direction:[-1.0,0.0,0.0],distance:4.0},
    {type:"point",origin:[0.0,0.7,0.0]},
    {type:"point",origin:[0.24,0.74,0.0]},
    {type:"aabb",origin:[0.24,0.74,0.0],size:[0.0,0.0,0.0]}
])^;
let metadata=<scene3d *[<physics gravity:[0.0,0.0,0.0]>,
    physics.attach(<mesh>,{id:"logical",type:'dynamic',
        colliders:[{id:"metadata-shape",shape:'capsule',radius:0.25,height:0.0}]})^]
>;
let sphere=physics.simulate(metadata,{steps:0})^;
let invalid=<scene3d <physics>
    <'rigid-body' id:"bad",type:'dynamic',<collider id:"bad-shape",shape:'capsule',height:-1.0>>
>;
let checks={
    valid:physics.validate(scene)^,
    height:abs(settled.position[1]-0.75)<0.01,
    sleeping:settled.sleeping and abs(settled.velocity[1])<0.01,
    mass:abs(settled.mass-3.141592653589793/12.0)<0.000000001,
    contact:len(trace.events)>0 and trace.events[0].type=="contact-start",
    cap:hits[0].collider=="shape" and abs(hits[0].distance-2.25)<0.000000001,
    side:abs(hits[1].distance-1.75)<0.000000001,
    point:hits[2].body=="capsule" and hits[3]==null,
    broadphase:hits[4].body=="capsule",
    metadata:abs(sphere.frames[0].bodies[0].mass-3.141592653589793/48.0)<0.000000001,
    invalid:physics_validate(invalid) is error
};
[for (key,passed in checks where passed!=true) string(key)]
