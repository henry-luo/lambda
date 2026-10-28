// S12.1.1v3: picking is a pure query over the same source IDs, sizes and camera convention.
import s: lambda.scene3d
import c: lambda.scene3d.camera
import p: lambda.scene3d.picking
let camera=c.prepare({type:"orthographic",position:[0.0,0.0,4.0],target:[0.0,0.0,0.0],view_height:4.0},128,128)
let basic=s.material()
let g=s.geometry([-1.0,0.0,0.0,1.0,0.0,0.0],{indices:[1,0],'point-sizes':[1.0,2.0]})
let cloud=s.points(g,basic,{id:"cloud",size:10.0})
let one=s.geometry([0.0,0.0,0.0])
let marker=s.sprite(basic,{id:"marker",size:[20.0,10.0],center:[0.0,0.0],'size-mode':'screen'})
let line_g=s.geometry([-1.0,0.0,0.0,0.0,1.0,0.0,1.0,0.0,0.0])
let path=s.line(line_g,basic,{id:"line",width:8.0})
let near_camera=c.prepare({position:[0.0,0.0,0.0],target:[0.0,0.0,-1.0],near:1.0,far:10.0,fov:90.0},128,128)
let near_g=s.geometry([-0.2,0.0,-0.5,1.0,0.0,-3.0])
let segment=s.line_segments(near_g,basic,{width:8.0});
[
 p.hit(camera,cloud,g,105.9,64).source_index==1,
 p.hit(camera,cloud,g,106.1,64)==null,
 p.hit(camera,cloud,g,36.9,64).source_index==0,
 p.hit(camera,cloud,g,37.1,64)==null,
 p.hit(camera,cloud,g,96,64).object=="cloud",
 p.hit(camera,marker,one,75,59).object=="marker",
 p.hit(camera,marker,one,63,59)==null,
 p.hit(camera,s.points(one,basic,{size:1.0,'size-mode':'world'}),one,95,64,s.transform([0,0,0],[0,0,0],[2,1,1])^)!=null,
 p.hit(camera,s.points(one,basic,{size:1.0,'size-mode':'world'}),one,97,64,s.transform([0,0,0],[0,0,0],[2,1,1])^)==null,
 p.hit(camera,path,line_g,64,29.5)!=null,
 p.hit(camera,path,line_g,64,26)==null,
 p.hit(near_camera,segment,near_g,68,64)!=null,
 p.hit(near_camera,segment,near_g,63,64)==null,
 p.hit(camera,s.line(s.geometry([0,0,0,0,0,0]),basic),s.geometry([0,0,0,0,0,0]),64,64)==null,
 p.hit(camera,s.points(one,basic,{visible:false}),one,64,64)==null
]
