import physics: lambda.physics

fn world(enabled, bounce) => <scene3d <physics gravity:[0.0,0.0,0.0],timestep:0.01>
    <'rigid-body' id:"wall",type:'fixed',position:[3.0,0.0,0.0],
        <collider id:"wall-shape",shape:'box',size:[0.02,4.0,4.0],restitution:bounce,friction:0.0>>
    <'rigid-body' id:"ball",type:'dynamic',ccd:enabled,'linear-velocity':[1000.0,0.0,0.0],
        <collider id:"ball-shape",shape:'sphere',radius:0.5,restitution:bounce,friction:0.0>>
>
let stop=physics.simulate(world(true,0.0),{steps:2,bodies:["ball"]})^;
let discrete=physics.simulate(world(false,0.0),{steps:1,bodies:["ball"]})^;
let bounce=physics.simulate(world(true,1.0),{steps:1,bodies:["ball"],commands:[
    {tick:0,commands:[{type:"force",body:"ball",value:[0.0,100.0,0.0]}]}]})^;
let sample=bounce.frames[1].bodies[0];
let spheres=<scene3d <physics gravity:[0.0,0.0,0.0],timestep:0.01>
    <'rigid-body' id:"left",type:'dynamic',position:[-3.0,0.0,0.0],'linear-velocity':[1000.0,0.0,0.0],ccd:true,
        <collider id:"left-shape",shape:'sphere',restitution:1.0>>
    <'rigid-body' id:"right",type:'dynamic',position:[3.0,0.0,0.0],'linear-velocity':[-1000.0,0.0,0.0],
        <collider id:"right-shape",shape:'sphere',restitution:1.0>>
>;
let exchange=physics.simulate(spheres,{steps:1})^;
let filtered=<scene3d <physics gravity:[0.0,0.0,0.0],timestep:0.01>
    <'rigid-body' id:"wall",type:'fixed',position:[3.0,0.0,0.0],
        <collider id:"wall-shape",shape:'box',size:[0.02,4.0,4.0],groups:{membership:2,filter:2}>>
    <'rigid-body' id:"ball",type:'dynamic',ccd:true,'linear-velocity':[1000.0,0.0,0.0],
        <collider id:"ball-shape",shape:'sphere',groups:{membership:1,filter:1}>>
>;
let through=physics.simulate(filtered,{steps:1,bodies:["ball"]})^;
let bad=<scene3d <physics><'rigid-body' id:"bad",type:'dynamic',ccd:true,
    <collider id:"bad-shape",shape:'capsule'>>>;
let checks={
    valid:physics.validate(world(true,0.0))^,
    stop:abs(stop.frames[1].bodies[0].position[0]-2.49)<1e-7,
    stationary:abs(stop.frames[2].bodies[0].position[0]-2.49)<1e-7 and abs(stop.frames[2].bodies[0].velocity[0])<1e-7,
    event:len(stop.events)==1 and stop.events[0].type=="contact-start" and stop.events[0].tick==1,
    discrete:abs(discrete.frames[1].bodies[0].position[0]-10.0)<1e-8,
    remaining:abs(sample.position[0]+5.02)<1e-6 and abs(sample.velocity[0]+1000.0)<1e-6,
    force:abs(sample.velocity[1]-1.0/sample.mass)<1e-9,
    exchange:abs(exchange.frames[1].bodies[0].position[0]+8.0)<1e-6 and abs(exchange.frames[1].bodies[1].position[0]-8.0)<1e-6,
    filters:abs(through.frames[1].bodies[0].position[0]-10.0)<1e-8 and len(through.events)==0,
    replay:stop==physics.simulate(world(true,0.0),{steps:2,bodies:["ball"]})^,
    rejected:physics_validate(bad) is error
};
[for (key,passed in checks where passed!=true) string(key)]
