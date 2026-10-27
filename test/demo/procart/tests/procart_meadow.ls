// semantic composition, saved data and presentation have independent ownership (PG11).
import C: ~~.mod_composition
import M: ~~.mod_meadow
import G: ~~.mod_grass
import solid: ~~.mod_plant_solids
import scene: lambda.scene3d
import t: lambda.scene3d.transform

let doc = input("test/demo/procart/meadow.mark",'mark')^
let resolved = C.resolve(doc)^
let model = M.generate(doc)^
let replayed = M.generate(doc)^;
[len(resolved) == 49,model.counts == {flower:21,tree:2,stone:4,grass:22},
 C.resolve(parse(format(doc,'mark')^,'mark')^)^ == resolved,
 len(model.height) == 25,len(model.height[0]) == 33,scene.validate(M.viewport(model)^)^,
 all([for (p in model.parts) all([for (v in solid.matrix(p)) t.numeric(v)])]),model.parts == replayed.parts];

let recipe = <resources <recipe id:"wildflower",kind:"flower",generator:"plant",version:1,
    parameters:{rules:{X:"F[+X][-X]",F:"FF"},stem:"#608249",leaf:"#78934c",petals:8,order:2,angle:25}>>
fn document(nodes,definitions=recipe) element => <meadow version:1,seed:42,width:8.0,depth:8.0,relief:0.3,*[definitions,*nodes]>
let a = <flower id:"a",recipe:"wildflower",position:[1.0,0.0,0.0],settings:{order:3}>
let b = <flower id:"b",recipe:"wildflower">
let grouped = C.resolve(document([<group id:"patch",position:[2.0,0.0,-1.0],scale:[2.0,2.0,2.0],a>]))^
let forwards = C.resolve(document([a,b]))^
let backwards = C.resolve(document([b,a]))^;
[grouped[0].parameters.order == 3,grouped[0].parameters.angle == 25,
 t.point(grouped[0].matrix,[0.0,0.0,0.0])^ == [4.0,0.0,-1.0],
 forwards[0].key == backwards[1].key,forwards[1].key == backwards[0].key,
 forwards[0].parameters == backwards[1].parameters];

let repeated = C.resolve(document([<group id:"patch",count:2,spread:[2.0,2.0],b>]))^
let extended = C.resolve(document([<group id:"patch",count:3,spread:[2.0,2.0],b>]))^;
[len(repeated) == 2,slice(extended,0,2) == repeated,repeated[0].key != repeated[1].key,
 repeated[0].matrix != repeated[1].matrix];

let named_a = C.resolve(document([<flower id:"named-a",recipe:"wildflower",key_path:["same-stream"]>]))^
let named_b = C.resolve(document([<flower id:"named-b",recipe:"wildflower",key_path:["same-stream"]>]))^;
[named_a[0].key == named_b[0].key,named_a[0].id != named_b[0].id];

fn rejected(d) bool => (C.resolve(d) ^ { null }) == null;
[rejected(document([a,a])),rejected(document([<shrub id:"unknown",recipe:"wildflower">])),
 rejected(document([<flower id:"missing",recipe:"absent">])),
 rejected(document([<group id:"too-many",count:64,<group id:"nested",count:64,b>>])),
 rejected(document([<flower id:"bad-count",recipe:"wildflower",count:0>])),
 rejected(document([<flower id:"bad-transform",recipe:"wildflower",scale:[1.0,0.0,1.0]>])),
 rejected(document([a],<resources <recipe id:"wildflower",kind:"flower",generator:"plant",version:2,parameters:{}>>)),
 (M.generate(document([<flower id:"typo",recipe:"wildflower",settings:{ordr:3}>])) ^ { null }) == null];

let tiny = document([a])
let st = M.initial(tiny)^
let hidden = M.command(st,"flower")^
let next = M.command(st,"next")^
let replay = M.command(next,"previous")^;
[hidden.model == st.model,not hidden.visibility.flower,hidden.document == st.document,
 replay.model == st.model,next.model.parts != st.model.parts,next.model.seed == 43,
 scene.validate(M.viewport(model,{flower:false,grass:false})^)^]

let named = M.initial(<meadow *:{*:map(tiny),seed:"summer"},*content(tiny)>)^
let variant = M.command(named,"next")^;
[variant.model.seed == "summer:1",M.command(variant,"previous")^.model == named.model]
