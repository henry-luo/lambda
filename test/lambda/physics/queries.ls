import physics: lambda.physics
let scene=<scene3d <physics>
    <'rigid-body' id:"box",type:'fixed',position:[2.0,0.0,0.0],
        <collider id:"box-shape",shape:'box',size:[2.0,2.0,2.0]>>
>;
let hits=physics.query(scene,[
    {type:"ray",origin:[2.0,5.0,0.0],direction:[0.0,-2.0,0.0],distance:10.0},
    {type:"ray",origin:[20.0,5.0,0.0],direction:[0.0,-1.0,0.0],distance:10.0},
    {type:"point",origin:[2.0,0.0,0.0]},
    {type:"aabb",origin:[2.0,0.0,0.0],size:[0.1,0.1,0.1]}
])^;
let checks={valid:physics.validate(scene)^,ray:hits[0].body=="box" and hits[0].distance==4.0,
    normal:hits[0].normal==[0.0,1.0,0.0],miss:hits[1]==null,
    point:hits[2].collider=="box-shape",aabb:hits[3].body=="box"};
[for (key,passed in checks where passed!=true) string(key)]
