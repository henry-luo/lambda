// S12.1.1v3 / PG14: shared textures preserve each model, admission budget and replay.
import surface: lambda.procart.surface
import s: lambda.scene3d
import t: lambda.scene3d.transform
let geometry=s.geometry([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,0.0],{id:"tri",normals:[0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0],uvs:[0.0,0.0,1.0,0.0,0.0,1.0],indices:[0,1,2]})
let model={resources:[geometry],parts:[{id:"part",geometry:"tri",matrix:t.matrix([0,0,0],[0,0,0],[1,1,1])^,surface:{pattern:"paint",resolution:8}}],stats:{vertices:3}}
let definitions=[<pattern id:"paint",version:1,generator:"stripes",parameters:{colors:["#123456","#abcdef"]}>]
let item={model:model,definitions:definitions,seed:42,limits:{max_surface_pixels:64,max_vertices:100}}
let inputs=[item,{*:item,model:{*:model,parts:[{*:model.parts[0],id:"second"}]}}],before=inputs
let batch=surface.bake_many(inputs)^;
[batch==[for (p in inputs) surface.bake(p.model,p.definitions,p.seed,p.limits)^],inputs==before,
 batch[0].resources[1]==batch[1].resources[1],surface.bake_many([])^==[],
 (surface.bake_many([{*:item,limits:{max_surface_pixels:63,max_vertices:100}}])^ {true})==true]
