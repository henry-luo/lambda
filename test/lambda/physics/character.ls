import physics: lambda.physics

fn arena(stair_height = 0.2, ceiling = 10.0) => <scene3d
    physics.world({gravity:[0.0,0.0,0.0],timestep:0.01})
    physics.body("floor",[physics.collider("floor-shape",'box',{size:[20.0,1.0,20.0]})],{type:'fixed',position:[0.0,-0.5,0.0]})
    physics.body("hero",[physics.collider("hero-shape",'capsule',{radius:0.5,height:1.0})],{type:'kinematic-position',position:[0.0,1.001,0.0]})
    physics.body("stair",[physics.collider("stair-shape",'box',{size:[2.0,stair_height,6.0]})],{type:'fixed',position:[2.0,stair_height/2.0,0.0]})
    physics.body("ceiling",[physics.collider("ceiling-shape",'box',{size:[20.0,0.1,20.0]})],{type:'fixed',position:[0.0,ceiling,0.0]})
>
let scene=arena(),low=arena(0.2,2.1),tall=arena(0.8);
let stair=physics.move(scene,"hero",[2.0,0.0,0.0])^;
let blocked=physics.move(tall,"hero",[2.0,0.0,0.0])^;
let low_ceiling=physics.move(low,"hero",[2.0,0.0,0.0])^;
let jump=physics.move(scene,"hero",[0.0,20.0,0.0])^;
let mixed=physics.query(scene,[{type:'move',body:"hero",displacement:[0.0,0.0,1.0]},
    {type:'ray',origin:[0.0,5.0,0.0],direction:[0.0,-1.0,0.0],distance:10.0},
    {type:'cast',collider_a:"hero-shape",direction:[0.0,-1.0,0.0],distance:3.0,all:true}])^;
let untouched=physics.simulate(scene,{steps:0})^;
let checks={
    stair:stair.grounded and stair.stepped and abs(stair.position[0]-2.0)<1e-5 and abs(stair.position[1]-1.201)<1e-5,
    tall:blocked.wall and blocked.grounded and not blocked.stepped and blocked.position[0]<1.0,
    low:low_ceiling.wall and not low_ceiling.stepped and low_ceiling.position[0]<1.0,
    ceiling:jump.ceiling and not jump.grounded and abs(jump.position[1]-8.949)<1e-5,
    copied:len(stair.contacts)>0 and stair.platform=="stair-shape" and stair.platform_pose.quaternion==[0.0,0.0,0.0,1.0],
    mixed:len(mixed)==3 and mixed[0].grounded and mixed[1].body=="hero" and len(mixed[2])>=1,
    pure:untouched.ticks==0 and untouched.frames[0].bodies[1].position==[0.0,1.001,0.0],
    bad_body:physics.move(scene,"absent",[1.0,0.0,0.0]) ^ { true } ~ { false },
    bad_motion:physics.move(scene,"floor",[1.0,0.0,0.0]) ^ { true } ~ { false },
    bad_up:physics.move(scene,"hero",[1.0,0.0,0.0],{up:[0.0,0.0,0.0]}) ^ { true } ~ { false },
    budget:physics.move(scene,"hero",[1.0,0.0,0.0],{geometry_budget:1}) ^ { true } ~ { false },
    bad_platform:physics.move(scene,"hero",[0.0,0.0,0.0],{platform:"floor-shape"}) ^ { true } ~ { false }
};
[for (key,passed in checks where passed!=true) string(key)]
