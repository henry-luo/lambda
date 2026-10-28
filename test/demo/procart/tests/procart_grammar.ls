// saved recipes, registered living objects and primitive scenes use one pipeline (PG12).
import G: ~~.mod_grammar
import scene: lambda.procart.scene
import native: lambda.scene3d
let docs=map([for (id in ["primitives","ribbons","branchwork","garden"]) (id,input("test/demo/procart/samples/"++id++".mark",'mark')^)])
let models=[for (id in ["primitives","ribbons","branchwork","garden"]) G.model(docs[id])^];
[all([for (m in models) native.validate(scene.to_scene3d(m))^]),models[0].stats.objects==19,len(unique(models[0].parts |> ~.kind))==9,
 all([for (m in models) m.stats.objects>0 and m.stats.vertices>0]),contains(unique(models[3].parts |> ~.kind),"flower"),
 contains(unique(models[3].parts |> ~.kind),"tree"),contains(unique(models[3].parts |> ~.kind),"grass"),contains(unique(models[3].parts |> ~.kind),"stone")];
let initial=G.initial(docs)^
let next=G.command(initial,"next")^
let previous=G.command(next,"previous")^
let branch=G.command(initial,"branchwork")^
let less=G.command(branch,"less-depth")^;
[initial.model.parts==previous.model.parts,next.seed==2027,previous.seed==2026,branch.id=="branchwork",less.model.settings.max_depth==6,
 G.model(parse(format(docs.primitives,'mark')^,'mark')^)^.parts==models[0].parts]

;
let custom=G.initial({primitives:<procart version:2,seed:41,<resources>;<box>>})^;
[custom.seed==41,custom.model.seed==41]
