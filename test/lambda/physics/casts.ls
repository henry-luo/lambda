import physics: lambda.physics

let scene=<scene3d <physics gravity:[0.0,0.0,0.0]>
    <'rigid-body' id:"probe",type:'fixed',<collider id:"probe-shape",shape:'sphere',radius:0.5,sensor:true,groups:{membership:0}>>
    <'rigid-body' id:"near",type:'fixed',position:[3.0,0.0,0.0],<collider id:"near-shape",shape:'box',size:[0.02,4.0,4.0],groups:{membership:1}>>
    <'rigid-body' id:"far",type:'fixed',position:[6.0,0.0,0.0],<collider id:"far-shape",shape:'box',size:[0.02,4.0,4.0],groups:{membership:2}>>
>;
let queries=[
    {type:"cast",collider_a:"probe-shape",direction:[1.0,0.0,0.0],distance:10.0},
    {type:"cast",collider_a:"probe-shape",direction:[1.0,0.0,0.0],distance:10.0,all:true},
    {type:"ray",origin:[-1.0,0.0,0.0],direction:[1.0,0.0,0.0],distance:10.0,all:true},
    {type:"overlap",collider_a:"probe-shape",offset:[3.0,0.0,0.0],all:true},
    {type:"cast",collider_a:"probe-shape",direction:[1.0,0.0,0.0],offset:[1.0,0.0,0.0],distance:10.0},
    {type:"cast",collider_a:"probe-shape",direction:[1.0,0.0,0.0],distance:10.0,groups:{filter:2}},
    {type:"aabb",origin:[2.6,2.4,0.0],size:[1.0,1.0,1.0]},
    {type:"overlap",collider_a:"probe-shape",offset:[2.6,2.4,0.0]},
    {type:"cast",collider_a:"probe-shape",offset:[3.0,0.0,0.0],distance:0.0},
    {type:"cast",collider_a:"probe-shape",direction:[-1.0,0.0,0.0],distance:10.0,all:true}
]
let before=physics.simulate(scene,{steps:0})^,hits=physics.query(scene,queries)^,after=physics.simulate(scene,{steps:0})^;
[hits[0].body=="near",abs(hits[0].distance-2.49)<1e-7,abs(hits[0].fraction-0.249)<1e-8,
 abs(hits[0].normal[0]+1.0)<1e-7,not hits[0].initial_overlap,
 (hits[1] |> ~.body)==["near","far"],abs(hits[1][1].distance-5.49)<1e-7,
 (hits[2] |> ~.body)==["near","far"],abs(hits[2][0].distance-3.99)<1e-7,
 len(hits[3])==1 and hits[3][0].overlapping,abs(hits[4].distance-1.49)<1e-7,
 hits[5].body=="far",hits[6]!=null and hits[7]==null,
 hits[8].initial_overlap and hits[8].distance==0.0 and hits[8].normal==[0.0,0.0,0.0],
 len(hits[9])==0,before==after];
[physics_query(scene,[{type:"cast",collider_a:"probe-shape",direction:[1,0,0],distance:10,max_iterations:1}]) is error,
 physics_query(scene,[{type:"ray",origin:[-1,0,0],direction:[1,0,0],all:true,max_hits:1}]) is error,
 physics_query(scene,[{type:"cast",collider_a:"missing",direction:[1,0,0]}]) is error,
 physics_query(scene,[{type:"cast",collider_a:"probe-shape",direction:[0,0,0]}]) is error,
 physics_query(scene,[{type:"point",all:"yes"}]) is error,
 physics_query(scene,[{type:"point",all:true,max_hits:0}]) is error,
 physics_query(scene,[{type:"point",max_iterations:129}]) is error]
