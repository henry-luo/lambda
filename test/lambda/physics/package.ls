import physics: lambda.physics
let scene=<scene3d
    *[physics.world({gravity:[0.0,0.0,0.0],timestep:0.1}),
      physics.body("body",[physics.collider("shape",'box')])]
>;
let trace=physics.simulate(scene,{steps:10,sample_every:3,
    commands:[{tick:0,commands:[{type:"impulse",body:"body",value:[1.0,0.0,0.0]}]}]})^;
let path=physics.trajectory(trace,"body");
let chart=physics.chart_data(trace,{body:"body"});
let energy=physics.energy(trace,"body",[0.0,0.0,0.0]);
let tracks=physics.tracks(trace,"body","visual");
let checks={frames:len(path)==5,tail:path[4].tick==10 and abs(path[4].position[0]-1.0)<1e-12,
    chart:chart[4].speed==1.0,energy:energy[4].kinetic==0.5,
    tracks:len(tracks)==2 and tracks[0].path=="visual.position" and len(tracks[1].values)==20};
[for (key,passed in checks where passed!=true) string(key)]
