import p: lambda.procart.plant
import o: lambda.procart.plant_organs
import rng: lambda.procart.rng
let key=rng.key(19)
let parameters={levels:[{length:1.0,children:2},{length:0.4,children:0}],leaves:{count:3}}
let tree=p.generate(key,parameters)^
let decorated=o.decorate(tree,key,{count:2})^
let leafless=o.decorate(p.generate(key,{*:parameters,leaves:{count:0}})^,key,{count:2})^;
[len(decorated.organs.anchors)==4,decorated.organs.anchors==leafless.organs.anchors,
 decorated.organs.anchors==o.decorate(tree,key,{count:2})^.organs.anchors,
 all([for (a in decorated.organs.anchors) contains(tree.skeleton.branches |> ~.id,a.parent)]),
 len(unique(decorated.parts |> ~.id))==len(decorated.parts),
 slice(decorated.parts,0,len(tree.parts))==tree.parts,
 len([for (part in decorated.parts where part.organ=="flower") part])==24,
 len([for (part in decorated.parts where part.organ=="fruit") part])==8];
let trifoliate=p.skeleton(key,{*:parameters,leaves:{count:3,leaflets:3}})^
let fruit_models=[for (kind in slice(o.FRUITS,1,len(o.FRUITS))) o.decorate(tree,key,{fruit:kind})^];
[len(trifoliate.leaves)==18,len(unique(trifoliate.leaves |> ~.id))==18,
 all([for (m in fruit_models) len([for (part in m.parts where part.organ=="fruit") part])>=8]),
 all([for (m in fruit_models) all([for (g in m.resources where name(g)=='geometry')
  all([for (x in g.positions) x is number and abs(x)<10000.0])])]),
 len(o.decorate(tree,key,{count:0})^.parts)==len(tree.parts),
 len(o.decorate(tree,key,{flower:"none",fruit:"none"})^.parts)==len(tree.parts)];
[(o.normalize({fruit:"unknown"}) ^ {null})==null,(o.normalize({count:9}) ^ {null})==null,
 (o.normalize({fruit_size:0.0}) ^ {null})==null,(o.normalize({unknown:1}) ^ {null})==null,
 (o.normalize({flower_color:"invalid"}) ^ {null})==null,
 (o.decorate({parts:[],resources:[]},key) ^ {null})==null,
 (p.normalize({leaves:{leaflets:2}}) ^ {null})==null,
 (o.decorate(p.generate(key,{levels:[{children:16},{children:16},{children:0}],leaves:{count:0}})^,key,{count:2}) ^ {null})==null]
