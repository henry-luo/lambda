// fixed topology, attachment and detail invariants exercise PG13.1–2 (S1.6).
import plant: lambda.procart.plant
import rng: lambda.procart.rng
import v: lambda.scene3d.camera
import t: lambda.scene3d.transform
import native: lambda.scene3d
import scene: lambda.procart.scene
let params={variation:0.0,levels:[{length:4.0,radius:0.2,children:2,sections:4,gnarliness:0.0,taper:0.8},
    {length:2.0,radius:0.08,children:2,sections:4,gnarliness:0.0},{length:1.0,radius:0.03,sections:4,gnarliness:0.0}],leaves:{count:6}}
let k=rng.key("architecture"),sk=plant.skeleton(k,params)^,again=plant.skeleton(k,params)^;
[len(sk.branches)==7,len(sk.leaves)==24,sk==again,sk!=plant.skeleton(rng.key(8),params)^,
 sk.branches[0].points==[[0,0,0],[0,1,0],[0,2,0],[0,3,0],[0,4,0]],
 abs(sk.branches[0].radii[4]-0.04)<1e-12,len(unique(sk.branches |> ~.id))==7];
let by_id=map([for (b in sk.branches) (b.id,b)]);
[all([for (b in sk.branches where b.parent != null) {
 let p=by_id[b.parent],start=b.points[0];
 b.radii[0]<=max(p.radii) and b.level==p.level+1 and any([for (i in 0 to (len(p.points)-2),let a=v.subtract(start,p.points[i]),let e=v.subtract(p.points[i+1],p.points[i]))
   v.dot(v.cross(a,e),v.cross(a,e))<1e-16 and v.dot(a,e)>=0 and v.dot(a,e)<=v.dot(e,e)])
}]),any([for (b in sk.branches) any([for (p in b.points) abs(p[2])>0.1])]),
 all([for (l in sk.leaves) t.affine(imported_matrix(l))])];
fn imported_matrix(l) array => [*v.times(l.frame.l,0.0-l.width),0,*v.times(l.frame.h,l.length),0,*l.frame.u,0,*l.frame.position,1]
let lods=plant.lods(sk,[{segments:12},{segments:5,section_stride:3,leaf_stride:3,leaf_scale:1.5}])^;
[lods[0].skeleton==lods[1].skeleton,lods[0].stats.leaves==24,lods[1].stats.leaves==8,
 lods[1].stats.vertices<lods[0].stats.vertices,native.validate(scene.to_scene3d(lods[0]))^,native.validate(scene.to_scene3d(lods[1]))^,
 all([for (p in slice(lods[1].parts,1,len(lods[1].parts))) contains(sk.leaves |> ~.id,p.id)])];
let trunk={levels:[{length:4.0,radius:0.2,sections:8,gnarliness:0.0}],variation:0.0,leaves:{count:0}}
let straight=plant.skeleton(k,trunk)^,bent=plant.skeleton(k,{*:trunk,force:{direction:[1.0,0.0,0.0],strength:0.8}})^;
[straight.branches[0].points[8]==[0,4,0],bent.branches[0].points[8][0]>2.0,
 straight==plant.skeleton(k,{*:trunk,force:{direction:[0,0,0],strength:0}})^,
 len(plant.mesh(straight)^.resources)==1,plant.mesh(straight)^.stats.leaves==0,
 abs(plant.mesh(straight)^.resources[0].normals[1]-0.0425/sqrt(1.0+0.0425*0.0425))<1e-12];
let buffer=lods[0].resources[0]
let norms=[for (i in 0 to (len(buffer.normals) div 3-1)) slice(buffer.normals,i*3,i*3+3)];
[all([for (n in norms) abs(v.dot(n,n)-1)<1e-9]),
 all([for (i in 0 to (len(buffer.indices) div 3-1),let ids=slice(buffer.indices,i*3,i*3+3),
 let a=slice(buffer.positions,ids[0]*3,ids[0]*3+3),let b=slice(buffer.positions,ids[1]*3,ids[1]*3+3),let c=slice(buffer.positions,ids[2]*3,ids[2]*3+3))
 v.dot(v.cross(v.subtract(b,a),v.subtract(c,a)),norms[ids[0]])>0])];
[(plant.normalize({oops:1}) ^ {null})==null,(plant.normalize({levels:[]}) ^ {null})==null,
 (plant.normalize({levels:[{children:16},{children:16},{children:16},{}]}) ^ {null})==null,
 (plant.normalize({leaves:{colors:["not-a-color"]}}) ^ {null})==null,
 (plant.normalize({force:{direction:[0,0,0],strength:1}}) ^ {null})==null,
 (plant.mesh(sk,{segments:2}) ^ {null})==null,(plant.mesh(sk,{unknown:1}) ^ {null})==null];
let alpha=plant.generate(k,{*:params,bark:"rgba(128,64,32,0.5)",leaves:{count:1,colors:[[0.1,0.5,0.2,0.25]]}})^;
[native.validate(scene.to_scene3d(alpha))^,alpha.parts[0].opacity==0.5,alpha.parts[1].opacity==0.25]
