import physics: lambda.physics

let tetra=<scene3d <physics gravity:[0.0,0.0,0.0]>
    <'rigid-body' id:"tetra",type:'dynamic',
        <collider id:"tetra-shape",shape:'convex',density:6.0,
            vertices:[[0.0,0.0,0.0],[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]]>>
    <'rigid-body' id:"sphere",type:'fixed',position:[3.0,0.0,0.0],
        <collider id:"sphere-shape",shape:'sphere',radius:0.25>>
>;
let frame=physics.simulate(tetra,{steps:0,bodies:["tetra"]})^;
let hits=physics.query(tetra,[
    {type:"distance",collider_a:"tetra-shape",collider_b:"sphere-shape"},
    {type:"distance",collider_a:"tetra-shape",collider_b:"tetra-shape"},
    {type:"point",origin:[0.2,0.2,0.2]},
    {type:"point",origin:[0.6,0.6,0.6]},
    {type:"ray",origin:[2.0,0.2,0.2],direction:[-1.0,0.0,0.0],distance:4.0}
])^;
let metadata=<scene3d *[<physics gravity:[0.0,-10.0,0.0],timestep:0.01>,
    <'rigid-body' id:"floor",type:'fixed',position:[0.0,-0.5,0.0],<collider id:"floor-shape",shape:'box',size:[20.0,1.0,20.0]>>,
    physics.attach(<mesh>,{id:"cube",type:'dynamic',colliders:[{id:"cube-shape",shape:'convex',
        vertices:[-1.0,-1.0,-1.0,1.0,-1.0,-1.0,-1.0,1.0,-1.0,1.0,1.0,-1.0,
            -1.0,-1.0,1.0,1.0,-1.0,1.0,-1.0,1.0,1.0,1.0,1.0,1.0]}]})^]
>;
let resting=physics.simulate(metadata,{steps:1000,sample_every:100,bodies:["cube"]})^;
let final=resting.frames[10].bodies[0];
let flat=<scene3d <physics><'rigid-body' id:"bad",type:'fixed',
    <collider id:"bad-shape",shape:'convex',vertices:[[0,0,0],[1,0,0],[0,1,0],[1,1,0]]>>>;
let checks={
    valid:physics.validate(tetra)^,
    mass:abs(frame.frames[0].bodies[0].mass-1.0)<1e-10,
    centre:all([for (v in frame.frames[0].bodies[0].centre_of_mass) abs(v-0.25)<1e-10]),
    gap:abs(hits[0].distance-1.75)<1e-8 and hits[0].overlapping==false,
    witness:abs(hits[0].point[0]-1.0)<1e-8 and abs(hits[0].point_b[0]-2.75)<1e-8,
    intersect:hits[1].distance==0.0 and hits[1].overlapping,
    point:hits[2].body=="tetra" and hits[3]==null,
    ray:abs(hits[4].distance-1.4)<1e-8,
    rest:abs(final.position[1]-1.0)<0.01 and final.sleeping,
    metadata:abs(final.mass-8.0)<1e-10,
    invalid:physics_validate(flat) is error,
    unknown:physics_query(tetra,[{type:"distance",collider_a:"missing",collider_b:"tetra-shape"}]) is error
};
[for (key,passed in checks where passed!=true) string(key)]
