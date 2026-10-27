// S12.1.1v3 / D7.2.4v4: drawable constructors and validation remain pure.
import s: lambda.scene3d
let one = s.geometry([0.0,0.0,0.0])
let two = s.geometry([-1.0,0.0,0.0,1.0,0.0,0.0])
let three = s.geometry([-1.0,0.0,0.0,0.0,1.0,0.0,1.0,0.0,0.0])
let basic = s.material()
fn scene(node) => s.scene([s.camera(),node])
fn message(node) => s.normalize(scene(node)) ^ { ^.message }
fn animated(node,path,values,kind = 'number') => s.validate(s.scene([s.camera(),node,s.clip("resize",[s.track(path,[0.0,1.0],values,kind)])]))^;
[
 s.validate(scene(s.points(one,basic,{size:8.0})))^,
 s.validate(scene(s.sprite(basic,{size:[24.0,16.0],'size-mode':'screen',center:[0.0,1.0]})))^,
 s.validate(scene(s.line(three,basic,{width:3.0})))^,
 s.validate(scene(s.line_segments(two,basic,{width:0.1,'size-mode':'world'})))^,
 s.validate(s.scene([s.camera(),s.resources([s.geometry([0.0,0.0,0.0],{id:"unused"})])]))^,
 s.validate(s.scene([s.camera(),s.resources([s.geometry([-1.0,0.0,0.0,0.0,1.0,0.0,1.0,0.0,0.0],{id:"g"}),s.material('basic',{id:"m"})]),s.mesh("g","m"),s.points("g","m")]))^,
 s.validate(scene(s.points(s.geometry([0.0,0.0,0.0,1.0,0.0,0.0],{'point-sizes':[1.0,2.0],colors:[1.0,0.0,0.0,0.5,0.0,1.0,0.0,1.0]}),basic)))^,
 contains(message(s.mesh(two,basic)),"triangles"),
 contains(message(s.line(one,basic)),"component"),
 contains(message(s.line_segments(three,basic)),"component"),
 contains(message(<sprite two basic>),"component"),
 contains(message(s.points(one,basic,{size:0.0})),"positive"),
 contains(message(s.sprite(basic,{size:[1.0,-1.0]})),"positive"),
 contains(message(s.line(two,basic,{width:-1.0})),"positive"),
 contains(message(s.points(one,basic,{'size-mode':'device'})),"size-mode"),
 contains(message(s.points(one,s.material('standard'))),"basic color"),
 contains(message(s.points(one,basic,{'cast-shadow':true})),"shadows"),
 contains(message(s.points(one,basic,{'receive-shadow':true})),"shadows"),
 contains(message(s.points(s.geometry([0.0,0.0,0.0],{'point-sizes':[-1.0]}),basic)),"positive"),
 contains(message(s.points(s.geometry([0.0,0.0,0.0],{'point-sizes':[1.0,2.0]}),basic)),"components"),
 contains(message(s.points(s.geometry([0.0,0.0,0.0],{'morph-positions':[0.0,0.0,0.0]}),basic)),"deformation"),
 animated(s.points(one,basic,{id:"cloud"}),"cloud.size",[4.0,12.0]),
 animated(s.sprite(basic,{id:"marker"}),"marker.size",[1.0,1.0,2.0,3.0],'vector'),
 animated(s.sprite(basic,{id:"marker"}),"marker.center",[0.5,0.5,0.0,1.0],'vector'),
 animated(s.line(two,basic,{id:"path"}),"path.width",[1.0,4.0]),
 animated(s.points(one,basic,{id:"cloud"}),"cloud.material.opacity",[1.0,0.5])
]
