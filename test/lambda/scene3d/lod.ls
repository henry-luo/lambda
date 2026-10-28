// S12.1.1v3 / D7.2.4v4: levels, bounds and culling remain pure scene descriptions.
import s: lambda.scene3d
let mesh=s.mesh(s.box(),s.material())
let levels=[s.group([mesh]),s.group([s.points(s.geometry([0,0,0]),s.material())]),s.group([s.sprite(s.material())])]
fn scene(node,opts={}) => s.scene([s.camera(),node],opts)
fn message(node,opts={}) => s.normalize(scene(node,opts)) ^ { ^.message };
[
 s.validate(scene(s.lod(levels,[0,10,30])))^,
 s.validate(scene(s.lod([s.group([s.lod(levels,[0,5,20])])],[0])))^,
 s.validate(scene(s.mesh(s.box(),s.material(),{bounds:[-1,-1,-1,1,1,1],'frustum-culling':false}),{'frustum-culling':false}))^,
 s.validate(scene(s.lod(levels,[0,10,30],{hysteresis:0.0})))^,
 contains(message(s.lod(levels,[1,10,30])),"distances"),
 contains(message(s.lod(levels,[0,10,10])),"distances"),
 contains(message(s.lod(levels,[0,30,10])),"distances"),
 contains(message(s.lod(levels,[0,10])),"distances"),
 contains(message(s.lod([],[])),"distances"),
 contains(message(s.lod(levels,[0,"near",30])),"distances"),
 contains(message(s.lod(levels,[0,10,30],{hysteresis:0.5})),"hysteresis"),
 contains(message(s.lod(levels,[0,10,30],{hysteresis:-0.1})),"hysteresis"),
 contains(message(s.lod([mesh],[0])),"child"),
 contains(message(s.lod([s.group([s.light('ambient')])],[0])),"geometric"),
 contains(message(s.mesh(s.box(),s.material(),{bounds:[1,0,0,-1,1,1]})),"bounds"),
 contains(message(s.mesh(s.box(),s.material(),{bounds:[0,0,0]})),"bounds"),
 contains(message(mesh,{'frustum-culling':1}),"frustum-culling"),
 contains(message(s.mesh(s.box(),s.material(),{'frustum-culling':1})),"frustum-culling"),
 format(scene(s.lod(levels,[0,10,30])),'gltf') ^ { contains(^.message,"distance LOD") }
]
