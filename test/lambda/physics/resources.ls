import physics: lambda.physics
import scene3d: lambda.scene3d

let vertices=[[-8.0,0.0,-8.0],[8.0,0.0,-8.0],[-8.0,0.0,8.0],[8.0,0.0,8.0]]
let indices=[[0,2,1],[1,2,3]]
let tetra=[[0.0,0.0,0.0],[1.0,0.0,0.0],[0.0,1.0,0.0],[0.0,0.0,1.0]]
let grid={rows:2,columns:2,heights:[[0.0,0.0],[0.0,2.0]],'cell-size':[2.0,2.0]}
fn shape(id:string,kind:symbol,resource:string,data:map,referenced:bool) element => physics.collider(id,kind,if (referenced) {geometry:resource} else data)
fn world(referenced:bool) element^ => <scene3d camera:"camera",
    <camera id:"camera",type:'perspective',position:[0.0,4.0,8.0]>
    <physics timestep:0.01>
    <'rigid-body' id:"floor",type:'fixed',shape("floor-shape",'mesh',"floor-data",{vertices:vertices,indices:indices},referenced)>
    <'rigid-body' id:"ball",type:'dynamic',position:[0.0,3.0,0.0],physics.collider("ball-shape",'sphere',{radius:0.5})>;
    *[for (i in 0 to 1) physics.body("hull-"++string(i),
        [shape("hull-shape-"++string(i),'convex',"hull-data",{vertices:tetra},referenced)],
        {position:[float(i)*4.0-2.0,3.0,0.0]})];
    physics.attach(<group id:"grid-visual">,{id:"grid",type:'fixed',
        colliders:[{id:"grid-shape",shape:'heightfield',*:if (referenced) {geometry:"grid-data"} else grid}]})^;
    // definitions follow their consumers; the same buffer also remains a valid render resource.
    <resources <geometry id:"floor-data",type:'buffer',positions:[for (v in vertices) *v],indices:[for (tri in indices) *tri]>
        physics.geometry("hull-data",'convex',{vertices:tetra});
        physics.geometry("grid-data",'heightfield',grid)>
>
let referenced=world(true)^,inline=world(false)^
let requests=[{type:"distance",collider_a:"floor-shape",collider_b:"ball-shape"},
    {type:"ray",origin:[1.5,4.0,1.5],direction:[0.0,-1.0,0.0]}]
let ref_trace=physics.simulate(referenced,{steps:400,sample_every:100})^
let in_trace=physics.simulate(inline,{steps:400,sample_every:100})^;
[physics.validate(referenced)^,scene3d.validate(referenced)^,
 ref_trace==in_trace,physics.query(referenced,requests)^==physics.query(inline,requests)^,
 abs(ref_trace.frames[0].bodies[2].mass-1.0/6.0)<1e-10]

let flat=<scene3d <physics><'rigid-body' id:"floor",type:'fixed',<collider id:"shape",shape:'mesh',geometry:"triangle">>
    <resources <geometry id:"triangle",type:'buffer',positions:[0,0,0,0,0,2,2,0,0]>>>
let hit=physics.query(flat,[{type:"ray",origin:[0.5,2.0,0.5],direction:[0.0,-1.0,0.0]}])^;
[physics.validate(flat)^,hit[0].body=="floor",abs(hit[0].distance-2.0)<1e-8,
 physics.validate(<scene3d <physics><'rigid-body' id:"floor",type:'fixed',<collider id:"shape",shape:'mesh',geometry:"triangle">>
    <resources <geometry id:"triangle",type:'buffer',positions:[0,0,0,0,0,2,2,0,0],indices:[]>>>)^]

fn rejected(definitions:array,options:map={}) bool => physics_validate(<scene3d <physics>
    <'rigid-body' id:"body",type:'fixed',<collider *:{id:"shape",shape:'convex',geometry:"data",*:options}>>
    <resources *definitions>>) is error
let definition=physics.geometry("data",'convex',{vertices:tetra})
let buffer=<geometry id:"data",type:'buffer',positions:[for (p in tetra) *p]>;
[rejected([]),rejected([definition,definition]),rejected([<material id:"data",type:'basic'>]),
 rejected([<geometry id:"data",type:'box'>]),
 rejected([physics.geometry("data",'heightfield',grid)]),
 rejected([physics.geometry("data",'convex',{vertices:tetra,version:2})]),
 rejected([<'collision-geometry' id:"data",shape:'convex',vertices:tetra>]),
 rejected([definition],{vertices:tetra}),rejected([definition],{geometry:42}),rejected([definition],{geometry:""}),
 rejected([physics.geometry("data",'convex',{vertices:tetra,geometry:"other"})]),
 rejected([<geometry *:{*:map(buffer),'morph-positions':[0,0,0]},*content(buffer)>]),
 rejected([buffer],{shape:'heightfield'}),
 rejected([physics.geometry("data",'mesh',{vertices:vertices,indices:[0,2,9]})],{shape:'mesh'}),
 rejected([physics.geometry("data",'heightfield',{*:grid,heights:[0,1]})],{shape:'heightfield'}),
 rejected([buffer],{shape:'mesh'}),
 (physics_validate(<scene3d <physics> definition>) is error)]
