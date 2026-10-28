// stored samples, skeleton-preserving controls and semantic recipes exercise PG13.4.
import T: ~~.mod_trees
import plant: lambda.procart.plant
import scene: lambda.procart.scene
import native: lambda.scene3d
let documents=map([for (item in T.CATALOG) (item.id,input("test/demo/procart/samples/"++item.file,'mark')^)])
let st=T.initial(documents)^;
[all([for (item in T.CATALOG) native.validate(T.viewport(T.command(st,item.id)^))^]),
 st.model.stats.branches==36,st.model.stats.leaves==560,
 plant.architecture(parse(format(st.document,'mark')^,'mark')^)^==st.skeleton];
let coarse=T.command(st,"coarse")^,full=T.command(coarse,"full")^;
[coarse.skeleton==st.skeleton,coarse.model.stats.vertices<st.model.stats.vertices,coarse.model.stats.leaves<st.model.stats.leaves,
 full.model==st.model,T.command(st,"leaves")^.skeleton==st.skeleton];
let next=T.command(st,"next")^,back=T.command(next,"previous")^,pruned=T.command(st,"prune")^,grown=T.command(pruned,"grow")^;
[next.seed==2027,next.skeleton!=st.skeleton,back.skeleton==st.skeleton,back.model==st.model,
 T.depth(pruned)==2,pruned.model.stats.branches==8,grown.model==st.model];
let wind=T.command(st,"windswept")^,bent=T.command(wind,"bend-up")^,apex=T.command(st,"apex")^;
[bent.skeleton!=wind.skeleton,apex.model.stats.branches==31,T.depth(T.command(apex,"grow")^)==6,
 native.validate(T.viewport(T.command(apex,"prune")^))^];
let doc=<procart version:2,seed:2026,
 <resources
  <recipe id:"oak",kind:"tree",generator:"plant.tree",version:1,parameters:documents.broadleaf.parameters>
  <recipe id:"herb",kind:"tree",generator:"plant.apex",version:1,parameters:documents.apex.parameters>>
 <tree id:"a",recipe:"oak",scale:0.8>
 <tree id:"b",recipe:"herb",position:[3,0,0],hue:10>>
let model=scene.expand(doc,plant.registry())^;
[native.validate(scene.to_scene3d(model))^,model.stats.objects==593,
 scene.expand(doc,plant.registry())^==model];
let custom=T.initial({*:documents,broadleaf:<tree *:{*:map(documents.broadleaf),seed:41}>})^;
[custom.seed==41,custom.skeleton==plant.architecture(custom.document)^,
 (plant.architecture(<tree *:{*:map(st.document),generator_version:2}>) ^ {null})==null];
let translucent=<procart version:2,seed:2026,
 <resources <recipe id:"oak",kind:"tree",generator:"plant.tree",version:1,
  parameters:{*:documents.broadleaf.parameters,bark:"rgba(128,64,32,0.5)",leaves:{count:1,colors:[[0.1,0.5,0.2,0.25]]}}>>
 <tree recipe:"oak",opacity:0.5>>
let alpha=scene.expand(translucent,plant.registry())^;
[native.validate(scene.to_scene3d(alpha))^,alpha.parts[0].opacity==0.25,
 all([for (p in alpha.parts where p.role == "leaf") p.opacity==0.125])]
