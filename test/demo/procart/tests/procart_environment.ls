// a pond and changing conditions preserve meadow identity and seeded placement (PG15).
import M: ~~.mod_meadow
import P: ~~.mod_pond
import E: lambda.procart.environment
import S: lambda.scene3d
import T: lambda.scene3d.transform
let doc=input("test/demo/procart/meadow.mark",'mark')^
let definitions=[for (n in content(doc) where name(n)=='resources') n][0]
let small=<meadow version:1,seed:2026,width:28.0,depth:20.0,relief:0.8,terrain:{width:17,height:13,octaves:2},
    *[definitions,<pond id:"water",position:[6.0,-2.6]>,<environment time:14.0,humidity:0.65>,
      <flower id:"lily",recipe:"water-lily",position:[6.0,0.0,-2.6]>,
      <flower id:"sun",recipe:"sunflower",position:[0.0,0.0,4.0]>,
      <grass id:"reeds",recipe:"pond-reed",position:[10.5,0.0,-2.6]>]>
let st=M.initial(small)^
let night=M.edit_environment(st,"time",0.0)^
let day=M.edit_environment(night,"time",12.0)^
let windy=M.edit_environment(day,"weather","windy")^
let reset=M.command(windy,"reset-environment")^
fn lilies(model) array => [for (i in model.instances where i.species=="water-lily") i];
[st.model.width==28.0,st.model.depth==20.0,st.model.pond != null,
 len(unique([for (o in st.model.objects where o.kind=="flower") o.recipe]))==2,
 len(lilies(st.model))==1,all([for (i in lilies(night.model)) i.response.openness==0.0]),
 all([for (i in lilies(day.model)) i.response.openness==1.0]),
 st.model.objects==night.model.objects,st.model.height==night.model.height,st.model.resources==night.model.resources,
 st.model.parts!=night.model.parts,reset.model==st.model];

let roundtrip=parse(format(windy.document,'mark')^,'mark')^
let restored=M.generate(roundtrip)^;
[restored.parts==windy.model.parts,restored.environment==windy.model.environment,
 S.validate(M.viewport(windy.model)^)^,S.validate(M.viewport(night.model)^)^,
 all([for (i in st.model.instances where i.species=="water-lily") abs(i.root[1]-st.model.pond.level)<1e-12]),
 all([for (i in st.model.instances where i.species!="water-lily") for (p in i.parts)
    P.distance(st.model.pond,p.position[0],p.position[2])>1.18]),
 M.elevation(st.model,st.model.pond.position[0],st.model.pond.position[1])<st.model.pond.level,
 st.model.counts=={flower:2,tree:0,stone:0,grass:1},
 M.command(windy,"next")^.model.environment==windy.model.environment,
 not M.command(st,"motion")^.motion,
 M.clock(8.25)=="08:15",M.clock(24.0)=="00:00"]
;
fn rejected_pond(p,relief=0.8) bool => (P.from_document(<meadow width:28.0,depth:20.0,relief:relief,p>) ^ { null })==null;
[rejected_pond(<pond id:"outside",position:[14.0,0.0]>),
 rejected_pond(<pond id:"empty",depth:0.0>),rejected_pond(<pond id:"flat">,0.0),
 rejected_pond(<pond id:"bad-color",color:"invalid">),
 (P.from_document(<meadow width:28.0,depth:20.0,relief:0.8,<pond id:"a">;<pond id:"b">>) ^ { null })==null]
