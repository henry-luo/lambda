import physics: lambda.physics
let scene=<scene3d
    <physics gravity:[0.0,-10.0,0.0],timestep:0.01>
    <'rigid-body' id:"floor",type:'fixed',position:[0.0,-0.5,0.0],
        <collider id:"floor-shape",shape:'box',size:[20.0,1.0,20.0]>>
    <'rigid-body' id:"box",type:'dynamic',position:[0.0,3.0,0.0],
        <collider id:"box-shape",shape:'box',size:[1.0,1.0,1.0]>>
>;
let trace=physics.simulate(scene,{steps:1000,sample_every:100,bodies:["box"]})^;
let settled=trace.frames[10].bodies[0];
let annotated=<scene3d *[
    <physics gravity:[0.0,0.0,0.0]>,
    physics.attach(<mesh id:"visual">,
        {id:"logical",type:'dynamic',colliders:[{id:"shape",shape:'box',size:[1.0,1.0,1.0]}]})^]
>;
let metadata=physics.simulate(annotated,{steps:0})^;
let checks={height:abs(settled.position[1]-0.5)<0.01,
    asleep:settled.sleeping and abs(settled.velocity[1])<0.01,
    begin:len(trace.events)>0 and trace.events[0].type=="contact-start",
    filter:all([for (frame in trace.frames) len(frame.bodies)==1 and frame.bodies[0].id=="box"]),
    metadata:metadata.frames[0].bodies[0].id=="logical"};
[for (key,passed in checks where passed!=true) string(key)]
