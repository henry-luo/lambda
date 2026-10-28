// patterns remain data; seeded color fields and shared bindings are pure across tiers (PG14 / S1.6).
import surface: lambda.procart.surface
import scene: lambda.procart.scene
import native: lambda.scene3d
import image: lambda.image
import media: lambda.doc.media
let pattern=<pattern id:"grain",generator:"noise",version:1,parameters:{colors:["#304030","#e0e8c0"],frequency:4}>
let prepared=surface.prepare(pattern,42)^;
[prepared==surface.prepare(pattern,42)^,prepared!=surface.prepare(pattern,43)^,
 surface.sample(prepared,[0.2,0.3,0.4])==surface.sample(prepared,[0.2,0.3,0.4]),
 surface.sample(prepared,[0.2,0.3,0.4])!=surface.sample(surface.prepare(pattern,43)^,[0.2,0.3,0.4]),
 all([for (kind in surface.KINDS,let p=surface.prepare(<pattern *:{*:map(pattern),generator:kind}>,42)^)
    all([for (v in surface.sample(p,[0.2,0.3],true)) v>=0 and v<=1])])];
fn document(nodes,settings={}) element => <procart version:2,seed:42,settings:settings,<resources pattern>;*nodes>
let source=document([<group surface:{pattern:"grain",resolution:8},<box id:"one">;<box id:"two",color:"#889966">>,<box id:"plain">])
let model=scene.expand(source)^
let textures=[for (r in model.resources where name(r)=='texture') r]
let raster=image.decode(media.data_uri_bytes(textures[0].src)^)^;
[model.stats.surface_textures==1,model.stats.surface_pixels==64,len(textures)==1,model.parts[0].surface==model.parts[1].surface,
 model.parts[2].surface==null,native.validate(scene.to_scene3d(model))^,
 scene.expand(parse(format(source,'mark')^,'mark')^)^==model,
 len(unique(flatten(image.channel(image.composition(raster)[0][0],"r")^)))>1,
 all([for (m in scene.meshes(model) where content(m)[0].texture != null) content(m)[0].type=='standard'])];
let override=document([<group surface:{pattern:"grain",resolution:8},
 <box id:"clear",surface:null>;<box id:"role",surfaces:{box:null}>;<box id:"kept">>])
let overridden=scene.expand(override)^;
[overridden.parts[0].surface==null,overridden.parts[1].surface==null,overridden.parts[2].surface!=null,
 (scene.expand(document([<box surface:{pattern:"missing"}>])) ^ {null})==null,
 (scene.expand(document([<box surface:{pattern:"grain",resolution:8}>],{max_surface_pixels:63})) ^ {null})==null,
 (surface.normalize(<pattern id:"bad",generator:"noise",version:2>) ^ {null})==null,
 (surface.normalize(<pattern id:"bad",generator:"noise",version:1,parameters:{colors:["#ffffff00","#ffffff"]}>) ^ {null})==null,
 (scene.expand(document([<box surface:{pattern:"grain",mystery:1}>])) ^ {null})==null,
 (scene.expand(document([<box surface:{pattern:"grain",mapping:"leaf"}>])) ^ {null})==null];
let changed=<procart *:{*:map(source),seed:43},*content(source)>
let pinned=<pattern *:{*:map(pattern),seed:17}>
let a=<procart version:2,seed:42,<resources pinned>;<box surface:{pattern:"grain",resolution:8}>>
let b=<procart *:{*:map(a),seed:43},*content(a)>;
[scene.expand(changed)^.parts[0].surface.texture!=model.parts[0].surface.texture,
 scene.expand(a)^.resources==scene.expand(b)^.resources];
let ruled=<procart version:2,seed:42,surface:{pattern:"grain",resolution:8},
 <resources pattern;<rule name:"Draw",<variant name:"main",<box>;<box surface:null>>>>;
 <Draw id:"one">;<Draw id:"two">>
let rules=scene.expand(ruled)^
let reversed=<procart *:map(ruled),<resources *reverse(content(content(ruled)[0]))>;*slice(content(ruled),1,len(content(ruled)))>;
[rules.stats.surface_textures==1,rules.parts[0].surface!=null,rules.parts[1].surface==null,
 rules.parts[2].surface!=null,rules.parts[3].surface==null,
 (surface.normalize(<pattern id:"bad",generator:"missing",version:1>) ^ {null})==null,
 (surface.normalize(<pattern id:"bad",generator:"noise",version:1,parameters:{frequency:-1}>) ^ {null})==null,
 (scene.expand(document([<box surface:{pattern:"grain",scale:0}>])) ^ {null})==null,
 (scene.expand(document([<box>],{max_surface_pixels:1048577})) ^ {null})==null,
 scene.expand(reversed)^==rules]
