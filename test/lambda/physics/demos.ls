import scenes: ~~.~~.demo.scene3d.physics._scenes
import physics: lambda.physics

let descriptions=[scenes.domino(),scenes.salvage(),scenes.sorter(),scenes.bounce()];
let domino=physics.simulate(descriptions[0],{steps:720,sample_every:720,
    commands:[{tick:0,commands:scenes.domino_commands()}]})^;
let cargo=physics.simulate(descriptions[1],{steps:120,sample_every:120,
    commands:[{tick:0,commands:scenes.salvage_commands()}]})^;
let sorter=physics.simulate(descriptions[2],{steps:1200,sample_every:1200})^;
let bounce=physics.simulate(descriptions[3],{steps:150,sample_every:150,bodies:scenes.bounce_ids})^;
let checks={
    valid:all([for (scene in descriptions) physics.validate(scene)^]),
    cascade:len([for (body in domino.frames[1].bodies where starts_with(body.id,"tile-") and body.position[1]<0.5) body])==32,
    compounds:len(cargo.frames[1].bodies)==10 and abs(cargo.frames[1].bodies[1].quaternion[3]-1.0)>0.001,
    sensor_events:len([for (event in sorter.events where event.type=="sensor-enter") event])>=12,
    kinematic:abs(sorter.frames[1].bodies[1].quaternion[3]-1.0)>0.001,
    bounce_response:bounce.frames[1].bodies[0].position[1]<0.4 and bounce.frames[1].bodies[4].position[1]>2.5,
    identical_masses:all([for (body in bounce.frames[0].bodies) body.mass==bounce.frames[0].bodies[0].mass])
};
[for (key,passed in checks where passed!=true) string(key)]
