// UVs follow organs and the original centerline, while object atlases sample local 3D positions (PG14).
import plant: lambda.procart.plant
import rng: lambda.procart.rng
import scene: lambda.procart.scene
import native: lambda.scene3d
import surface: lambda.procart.surface
import image: lambda.image
import media: lambda.doc.media
// independent equal bindings must share one draw batch (S5.6.1v2 / S5.6.2).
let repeated=[for (i in 0 to 9) {id:string(i),geometry:"shared-leaf",color:"#ffffff",opacity:1.0,kind:"tree",role:"leaf",
    surface:{texture:"shared-veins",roughness:0.85,metalness:0.0},matrix:[1.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,0.0,1.0,0.0,float(i),0.0,0.0,1.0]}];
[len(scene.meshes({parts:repeated}))==1,len(scene.meshes({parts:repeated})[0].instances)==10];
let skeleton=plant.skeleton(rng.key(0),{levels:[{length:3,sections:6,gnarliness:15}],leaves:{count:2}})^
let full=plant.mesh(skeleton,{segments:8})^,coarse=plant.mesh(skeleton,{segments:4,section_stride:3})^
let leaf=full.resources[1],bark=full.resources[0],low=coarse.resources[0]
let distance=[for (i in 0 to 6) bark.uvs[i*9*2+1]];
[len(leaf.uvs)==len(leaf.positions) div 3*2,min(leaf.uvs)==0,max(leaf.uvs)==1,
 all([for (i in 0 to 6) bark.uvs[i*18]==0 and bark.uvs[i*18+16]==1]),
 [for (i in 0 to 2) low.uvs[i*10+1]]==[distance[0],distance[3],distance[6]],distance[6]>0,
 native.validate(scene.to_scene3d(full))^,
 all([for (i in 0 to 2) slice(low.positions,i*15,i*15+3)==slice(bark.positions,i*3*27,i*3*27+3)])];
let patterns=[<pattern id:"bark",generator:"bark",version:1>,<pattern id:"veins",generator:"leaf-veins",version:1>]
let tree=<procart version:2,seed:42,<resources *patterns;
 <recipe id:"oak",kind:"tree",generator:"plant.tree",version:1,parameters:{levels:[{length:2,sections:3}],leaves:{count:2}}>>;
 <tree recipe:"oak",surfaces:{bark:{pattern:"bark",mapping:"branch",resolution:8},leaf:{pattern:"veins",mapping:"leaf",resolution:8}}>>
let model=scene.expand(tree,plant.registry())^;
[model.stats.surface_textures==2,model.stats.objects==3,
 model.parts[1].surface==model.parts[2].surface,model.parts[0].surface!=model.parts[1].surface,
 native.validate(scene.to_scene3d(model))^];
let atlas=<procart version:2,seed:42,<resources <pattern id:"mineral",generator:"marble",version:1>>;
 <sphere id:"one",segments:4,rings:2,surface:{pattern:"mineral",mapping:"object",resolution:16}>;
 <sphere id:"two",segments:4,rings:2,position:[2,0,0],scale:2,surface:{pattern:"mineral",mapping:"object",resolution:16}>>
let rock=scene.expand(atlas)^
let geometry=[for (g in rock.resources where name(g)=='geometry' and g.id==rock.parts[0].geometry) g][0];
[rock.stats.surface_textures==1,rock.parts[0].geometry==rock.parts[1].geometry,
 len(geometry.positions)==16*9,len(geometry.uvs)==16*6,
 native.validate(scene.to_scene3d(rock))^,rock==scene.expand(parse(format(atlas,'mark')^,'mark')^)^,
 (scene.expand(<procart *:{*:map(atlas),settings:{max_vertices:50}},*content(atlas)>) ^ {null})==null];
let no_uv=<procart version:2,seed:0,<resources *patterns;<geometry id:"facet",type:"buffer",positions:[0,0,0,1,0,0,0,1,0]>>;
 <mesh geometry:"facet",surface:"bark">>;
[(scene.expand(no_uv) ^ {null})==null,
 (scene.expand(<procart version:2,seed:0,<resources *patterns>;<box surface:{pattern:"bark",mapping:"object"}>>) ^ {null})==null];
let pattern=<pattern id:"oracle",generator:"noise",version:1,parameters:{frequency:2}>
let triangle=<geometry id:"tri",type:"buffer",positions:[0,0,0,1,0,0,0,1,0],uvs:[0,0,1,0,0,1]>
let doc=<procart version:2,seed:42,<resources pattern;triangle>;<mesh geometry:"tri",surface:{pattern:"oracle",mapping:"object",resolution:8}>>
let oracle=scene.expand(doc)^
let tex=[for (r in oracle.resources where name(r)=='texture') r][0]
let raster=image.decode(media.data_uri_bytes(tex.src)^)^,red=image.channel(image.composition(raster)[0][0],"r")^
let p=surface.prepare(pattern,42)^
let mapped=scene.expand(<procart version:2,seed:42,<resources pattern;triangle>;
 <mesh geometry:"tri",surface:{pattern:"oracle",resolution:8,scale:[2,4],offset:[0.25,0.1]}>>)^;
[red[6][1]==int(round(surface.sample(p,[0,0,0])[0]*255)),
 red[6][6]==int(round(surface.sample(p,[1,0,0])[0]*255)),
 red[1][1]==int(round(surface.sample(p,[0,1,0])[0]*255)),
 [for (g in mapped.resources where name(g)=='geometry' and g.id==mapped.parts[0].geometry) g.uvs][0]==[0.25,0.1,0.75,0.1,0.25,0.35]]
