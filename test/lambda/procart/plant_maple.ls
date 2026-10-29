import plant: lambda.procart.plant
import rng: lambda.procart.rng
import scene: lambda.procart.scene
import native: lambda.scene3d
let params={levels:[{length:1.0,radius:0.03,children:0,sections:3}],leaves:{count:4,shape:"maple",length:0.3,width:0.25}};
let a=plant.generate(rng.key(42),params)^,b=plant.generate(rng.key(42),params)^;
let leaf=a.resources[1];
[a==b,len(a.skeleton.leaves)==4,len(a.parts)==5,len(leaf.positions)==16*9,
 len(leaf.normals)==len(leaf.positions),len(leaf.uvs)==len(leaf.positions) div 3*2,
 all([for (n in leaf.normals) n is number and abs(n)<=1.0]),
 min(leaf.uvs)>=0.0,max(leaf.uvs)<=1.0,native.validate(scene.to_scene3d(a))^,
 (plant.normalize({*:params,leaves:{shape:"unknown"}}) ^ {null})==null]
