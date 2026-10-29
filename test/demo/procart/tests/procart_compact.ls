// S12.1.1v3 / PG12: compact composites retain geometry, UVs, material boundaries and transformed normals.
import scene: lambda.procart.scene
import s: lambda.scene3d
import t: lambda.scene3d.transform
let geometry=s.geometry([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,1.0],{id:"tri",normals:[0.0,-0.7071067811865475,0.7071067811865475,0.0,-0.7071067811865475,0.7071067811865475,0.0,-0.7071067811865475,0.7071067811865475],uvs:[0.0,0.0,1.0,0.0,0.0,1.0],indices:[0,1,2]})
let first={id:"first",geometry:"tri",matrix:t.matrix([1.0,2.0,3.0],[0.0,0.0,0.0],[2.0,3.0,4.0])^,kind:"tree",role:"leaf",color:"#448844",opacity:1.0,surface:null}
let second={*:first,id:"second",matrix:t.matrix([0.0,0.0,0.0],[0.0,0.0,0.0],[-1.0,1.0,1.0])^}
let source={resources:[geometry],parts:[first,second]},before=source
let packed=scene.compact(source,"packed")^,g=packed.resources[0],n=slice(g.normals,0,3)
let separate=scene.compact({*:source,parts:[first,{*:second,color:"#aa4433"}]},"colors")^
let invalid=(scene.compact({resources:[<geometry *:{*:map(geometry),'skin-indices':[0,0,0,0]}>],parts:[first]},"invalid") ^ { ^ })
let draw=s.scene([s.resources(packed.resources),*scene.meshes({parts:[{*:packed.parts[0],matrix:first.matrix},{*:packed.parts[0],matrix:second.matrix}]}),s.camera({id:"camera"})],{camera:"camera"});
[len(packed.parts)==1,len(g.positions)==18,g.indices==[0,1,2,3,5,4],g.uvs==[0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0],
 slice(g.positions,0,9)==[1.0,2.0,3.0,3.0,2.0,3.0,1.0,5.0,7.0],
 abs(sum(n*n)-1.0)<1e-12,abs(n[1]/n[2]+4.0/3.0)<1e-12,
 packed.parts[0].source_ids==["first","second"],source==before,
 len(separate.parts)==2,invalid is error,s.validate(draw)^]
