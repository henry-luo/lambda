import scene: lambda.procart.scene
import model: lambda.scene3d.model
import s: lambda.scene3d
let identity=[1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0];
fn parts(size) array => [for (i in 0 to (size-1))
    {id:"leaf-"++string(i),geometry:"shared",color:"#ffffff",opacity:1.0,kind:"tree",role:"leaf",surface:null,matrix:identity}];
fn check(size) bool {
    let original=if (size==0) [] else parts(size);
    let meshes=scene.meshes({parts:original},{'cast-shadow':false});
    len(meshes)==int(ceil(float(size)/float(model.INSTANCE_LIMIT))) and
    sum([for (m in meshes) len(m.instances)])==size and
    all([for (m in meshes) len(m.instances)>0 and len(m.instances)<=model.INSTANCE_LIMIT and
        int(m["data-count"])==len(m.instances) and m["cast-shadow"]==false and
        content(m)[0].type=='lambert' and m.geometry=="shared"]) and
    (if (size==0) true else join([for (m in meshes) m["data-source-ids"]]," ")==join(original |> ~.id," "))
};
[for (size in [0,model.INSTANCE_LIMIT-1,model.INSTANCE_LIMIT,model.INSTANCE_LIMIT+1,2*model.INSTANCE_LIMIT+17]) check(size)];
let both=[*parts(3),{*:parts(1)[0],id:"petal",role:"petal",color:"#eeaacc",opacity:0.5}];
let batches=scene.meshes({parts:both});
[len(batches)==2,len(batches[0].instances)==3,len(batches[1].instances)==1,content(batches[1])[0].transparent]
