import physics: lambda.physics

let floor_scene=<scene3d <physics timestep:0.01>
    <'rigid-body' id:"floor",type:'fixed',<collider id:"floor-shape",shape:'mesh',
        vertices:[[-10.0,0.0,-10.0],[10.0,0.0,-10.0],[-10.0,0.0,10.0],[10.0,0.0,10.0]],indices:[[0,2,1],[1,2,3]]>>
    <'rigid-body' id:"ball",type:'dynamic',position:[0.0,3.0,0.0],<collider id:"ball-shape",shape:'sphere',radius:0.5>>
>;
let hits=physics.query(floor_scene,[
    {type:"ray",origin:[2.0,4.0,0.0],direction:[0.0,-1.0,0.0]},
    {type:"ray",origin:[2.0,-4.0,0.0],direction:[0.0,1.0,0.0]},
    {type:"point",origin:[2.0,0.0,0.0]},
    {type:"point",origin:[2.0,-0.1,0.0]},
    {type:"distance",collider_a:"floor-shape",collider_b:"ball-shape"}
])^;
let rest=physics.simulate(floor_scene,{steps:1000,sample_every:100,bodies:["ball"]})^;
let final=rest.frames[10].bodies[0];
let grid=<scene3d *[<physics>,physics.attach(<group>,{id:"grid",type:'fixed',colliders:[
    {id:"grid-shape",shape:'heightfield',rows:2,columns:2,heights:[[0.0,0.0],[0.0,2.0]],'cell-size':[2.0,2.0],'double-sided':true}
]})^]>;
let samples=physics.query(grid,[
    {type:"ray",origin:[0.5,3.0,0.5],direction:[0.0,-1.0,0.0]},
    {type:"ray",origin:[1.5,3.0,1.5],direction:[0.0,-1.0,0.0]},
    {type:"ray",origin:[1.5,-3.0,1.5],direction:[0.0,1.0,0.0]}
])^;
let text=<scene3d <physics><'rigid-body' id:"text",type:'fixed',<collider id:"text-shape",shape:'mesh',
    vertices:"0 0 0 2 0 0 0 0 2",indices:"0 2 1">>>;
let bad_index=<scene3d <physics><'rigid-body' id:"bad",type:'fixed',<collider id:"bad-shape",shape:'mesh',
    vertices:[0,0,0,1,0,0,0,0,1],indices:[0,2,9]>>>;
let bad_samples=<scene3d <physics><'rigid-body' id:"bad",type:'fixed',<collider id:"bad-shape",shape:'heightfield',
    rows:2,columns:2,heights:[0,1,2]>>>;
let dynamic_mesh=<scene3d <physics><'rigid-body' id:"bad",type:'dynamic',<collider id:"bad-shape",shape:'mesh',
    vertices:[0,0,0,1,0,0,0,0,1],indices:[0,2,1]>>>;
let checks={
    valid:physics.validate(floor_scene)^ and physics.validate(grid)^ and physics.validate(text)^,
    ray:hits[0].body=="floor" and abs(hits[0].distance-4.0)<1e-8,
    backface:hits[1]==null,
    surface:hits[2].body=="floor" and hits[3]==null,
    distance:abs(hits[4].distance-2.5)<1e-7 and abs(hits[4].point_b[1]-2.5)<1e-7,
    rest:abs(final.position[1]-0.5)<0.01 and final.sleeping,
    height:abs(samples[0].point[1])<1e-8 and abs(samples[1].point[1]-1.0)<1e-8,
    normal:abs(samples[1].normal[1]-1.0/sqrt(3.0))<1e-8 and samples[2].normal[1]<0.0,
    invalid_index:physics_validate(bad_index) is error,
    invalid_samples:physics_validate(bad_samples) is error,
    dynamic:physics_validate(dynamic_mesh) is error
};
[for (key,passed in checks where passed!=true) string(key)]
