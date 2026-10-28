// a saved material study exercises mineral atlases and instanced plant surfaces together (PG14).
import G: ~~.mod_grammar
import scene: lambda.procart.scene
import native: lambda.scene3d
let source=input("test/demo/procart/samples/surfaces.mark",'mark')^
let model=G.model(source)^
let trees=[for (p in model.parts where p.kind=="tree") p]
let leaves=[for (p in trees where p.role=="leaf") p];
[native.validate(scene.to_scene3d(model))^,model.stats.surface_textures==5,model.stats.surface_pixels<=source.settings.max_surface_pixels,
 len(leaves)==40,len(unique(leaves |> ~.surface.texture))==1,
 all([for (p in model.parts where p.kind=="stone") p.surface!=null]),
 G.model(parse(format(source,'mark')^,'mark')^)^==model];
let next=G.model(<procart *:{*:map(source),seed:2027},*content(source)>)^;
[next.parts!=model.parts,next.stats.objects==model.stats.objects,
 G.model(<procart *:{*:map(source),seed:2026},*content(source)>)^==model]
