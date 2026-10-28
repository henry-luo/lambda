// definition names and instance IDs have separate jobs (PG12.11).
import art: lambda.procart.scene
import native: lambda.scene3d
fn doc(nodes,definitions,cfg={}) element => <procart version:2,seed:42,settings:cfg,*[definitions,*nodes]>
let definitions=<resources
 <rule name:"R1",<variant name:"main",
  <R1 id:"next",position:[0.9,0,0],scale:0.99,saturation:0.99>;
  <sphere id:"ball",scale:2>>>>
let source=doc([<R1 id:"start",color:"#77a955">],definitions,{max_depth:4})
let model=art.expand(source)^
let ordered=sort(model.parts,(part)=>part.matrix[12]);
[native.validate(art.to_scene3d(model))^,model.stats.objects==4,model.stats.depth_retired==1,
 all([for (i,p in ordered) abs(p.matrix[0]-2.0*(0.99**i))<1e-12]),
 all([for (i,p in ordered) abs(p.matrix[12]-90.0*(1.0-0.99**i))<1e-10]),len(unique(model.parts |> ~.id))==4,
 art.expand(parse(format(source,'mark')^,'mark')^)^==model];
let forward=<resources
 <rule name:"First",<variant name:"draw",<Second id:"target">>>;
 <rule name:"Second",<variant name:"draw",<box id:"piece">>>>
let instances=[<First id:"left">,<First id:"right",position:[2,0,0]>]
let a=art.expand(doc(instances,forward))^
let b=art.expand(doc(reverse(instances),<resources *reverse(content(forward))>))^;
[a.stats.objects==2,map([for (p in a.parts) (p.id,p.key)])==map([for (p in b.parts) (p.id,p.key)]),
 len(unique(a.parts |> ~.key))==2];
let prototypes=<resources <geometry id:"facet",type:"buffer",positions:[0,0,0,1,0,0,0,1,0],indices:[0,1,2]>;
 <rule name:"Facet",<variant name:"draw",<mesh id:"face",geometry:"facet">>>>
let mesh=art.expand(doc([<Facet id:"one">],prototypes))^;
[mesh.stats.objects==1,mesh.parts[0].geometry=="prototype-facet",native.validate(art.to_scene3d(mesh))^];
[(art.expand(doc([<Missing>],definitions)) ^ {null})==null,
 (art.expand(doc([<R1 name:"instance">],definitions)) ^ {null})==null,
 (art.expand(doc([<R1 rule:"R1">],definitions)) ^ {null})==null,
 (art.expand(doc([<R1>,<R1>],<resources <rule name:"R1",<variant name:"same",<box>>;<variant name:"same",<sphere>>>>)) ^ {null})==null,
 (art.expand(doc([<box>],<resources <rule name:"box",<variant name:"draw",<sphere>>>>)) ^ {null})==null,
 (art.expand(doc([<box>],<resources <rule name:"1invalid",<variant name:"draw",<sphere>>>>)) ^ {null})==null,
 (art.expand(doc([<box>],<resources <rule name:"rule",<variant name:"draw",<sphere>>>>)) ^ {null})==null];
// obsolete syntax is rejected rather than silently becoming a different random identity.
[(art.expand(doc([<R1>],<resources <rule id:"R1",<variant name:"main",<sphere>>>>)) ^ {null})==null,
 (art.expand(doc([<R1>],<resources <rule name:"R1",<variant id:"main",<sphere>>>>)) ^ {null})==null,
 (art.expand(doc([<use rule:"R1">],definitions)) ^ {null})==null,
 (art.expand(doc([<R1 id:"same">,<R1 id:"same">],definitions)) ^ {null})==null]
