// cardinality, unit frames and face normals verify all requested drawing primitives (PG12.2).
import p: lambda.procart.primitive
import s: lambda.procart.scene
import native: lambda.scene3d
import v: lambda.scene3d.camera
let definitions = <resources <geometry id:"facet",type:"buffer",positions:[0.0,0.0,0.0,1.0,0.0,0.0,0.5,0.5,0.0],indices:[0,1,2]>>
let doc = <procart version:2,seed:42,*[definitions,<box>,<grid>,<sphere scale:[1.0,0.6,0.4]>,<line>,<point>,
 <triangle points:[[0,0,0],[1,0,0],[0.5,0.5,0]]>,<mesh geometry:"facet">,<cylinder>,
 <tube points:[[0,0,0],[1,0,0],[1.5,0.5,0]],radius:0.04,capped:true> ]>
let m = s.expand(doc)^;
[len(m.parts)==9,len(m.resources)==9,native.validate(s.to_scene3d(m))^,all([for (g in m.resources where g.positions != null) all([for (x in g.positions) x is number])])];
let cylinder = p.geometry("cylinder",p.parameters("cylinder",<cylinder>)^,"cylinder")
let tube = p.geometry("tube",p.parameters("tube",<tube points:[[0,0,0],[1,0,0],[1.5,0.5,0]],radius:0.04,capped:true>)^,"tube")
let grid = p.geometry("grid",p.parameters("grid",<grid>)^,"grid")
let line = p.geometry("line",p.parameters("line",<line>)^,"line");
[len(cylinder.positions)==102*3,len(grid.positions)==12*38*3,len(line.positions)==38*3,
 min([for (i in 0 to 101) cylinder.positions[i*3]]) == -0.5,max([for (i in 0 to 101) cylinder.positions[i*3]]) == 0.5,
 all([for (i in 0 to (len(tube.normals) div 3-1)) abs(v.dot(slice(tube.normals,i*3,i*3+3),slice(tube.normals,i*3,i*3+3))-1)<1e-9])];
[all([for (i in 0 to (len(cylinder.indices) div 3-1),let ids=slice(cylinder.indices,i*3,i*3+3),
 let a=slice(cylinder.positions,ids[0]*3,ids[0]*3+3),let b=slice(cylinder.positions,ids[1]*3,ids[1]*3+3),let c=slice(cylinder.positions,ids[2]*3,ids[2]*3+3))
 v.dot(v.cross(v.subtract(b,a),v.subtract(c,a)),slice(cylinder.normals,ids[0]*3,ids[0]*3+3))>0]),
 (p.parameters("tube",<tube points:[[0,0,0],[0,0,0]]>) ^ {null}) == null,
 (p.parameters("tube",<tube points:[[0,0,0],[1,0,0],[0,0,0]]>) ^ {null}) == null,
 (p.parameters("triangle",<triangle points:[[0,0,0],[1,0,0],[2,0,0]]>) ^ {null}) == null]
