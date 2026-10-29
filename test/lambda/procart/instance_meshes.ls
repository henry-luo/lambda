import scene: lambda.procart.scene
import s: lambda.scene3d
import t: lambda.scene3d.transform
import model: lambda.scene3d.model
let identity=t.matrix([0.0,0.0,0.0],[0.0,0.0,0.0],[1.0,1.0,1.0])^
let part={id:"leaf",geometry:"unit",matrix:identity,kind:"tree",role:"leaf",surface:null,color:"#708641",opacity:1.0}
let parts=[part,{*:part,id:"bark",role:"bark",color:"#725237"}]
let placements=[for (i in 0 to 2) {id:"stand-"++string(i),matrix:t.matrix([float(i),0.0,0.0],[0.0,float(i)*0.3,0.0],[1.0,1.0,1.0])^}]
let expanded={parts:[for (placement in placements) for (part in parts) {*:part,id:placement.id++"/"++part.id,matrix:placement.matrix}]}
let options={'cast-shadow':true,'receive-shadow':true}
let direct=scene.instance_meshes({parts:parts},placements,options)^;
[direct==scene.meshes(expanded,options),scene.instance_meshes({parts:parts},[])^==[],
 scene.instance_meshes({parts:[]},placements)^==[],
 (scene.instance_meshes({parts:[{*:part,matrix:placements[1].matrix}]},placements)^ { true })==true]
let many=[for (i in 0 to model.INSTANCE_LIMIT) {id:"s-"++string(i),matrix:identity}]
let chunks=scene.instance_meshes({parts:[part]},many)^;
[len(chunks)==2,len(chunks[0].instances)==model.INSTANCE_LIMIT,len(chunks[1].instances)==1,
 chunks[1]["data-source-ids"]=="s-"++string(model.INSTANCE_LIMIT)++"/leaf",
 chunks[0].instances[0]==identity,chunks[1]["data-count"]=="1"]
