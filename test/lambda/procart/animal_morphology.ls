// PG16: distinct family silhouettes, hinged native motion and replayable recipes.
import a: lambda.procart.animal
import s: lambda.scene3d
import p: lambda.procart.primitive
import old: ~~.~~.demo.scene3d.butterflies.mod_atlas
import atlas: lambda.procart.butterfly_atlas
let docs=[<butterfly version:1,generator_version:1,seed:2026,parameters:{}>,<bird version:1,generator_version:1,seed:2026,parameters:{}>,
 <frog version:1,generator_version:1,seed:2026,parameters:{}>,<fish version:1,generator_version:1,seed:2026,parameters:{}>,
 <bee version:1,generator_version:1,seed:2026,parameters:{}>,<rabbit version:1,generator_version:1,seed:2026,parameters:{}>]
let models=[for (doc in docs) a.generate(doc)^];
[all([for (m in models) s.validate(s.scene([s.camera(),*a.nodes(m,0.25,true)]))^]),
 all([for (i,m in models) a.generate(parse(format(docs[i],'mark')^,'mark')^)^==m]),
 all([for (m in models) len(unique(m.parts |> ~.id))==len(m.parts)]),
 len([for (part in models[1].parts where part.role=="feather") part])==23,
 len([for (part in models[2].parts where part.role=="toe") part])==18,
 len([for (part in models[4].parts where part.role=="antenna") part])==2,
 models[0].stats.surface_pixels==131072,
 hash(format(atlas.atlas(),'xml')^,'sha256')==b'C6883F3C8AC323FD9F4ABDF4DC67AA36B96340A261AF41E40DD0DC45B199DC1B',
 len(atlas.SPECIES)==16 and old.CELL==atlas.CELL];
let blade=p.blade();
[len(blade.positions)==105,len(blade.normals)==105,len(blade.uvs)==70,len(blade.indices)==144,
 all([for (i in 0 to 34,let n=slice(blade.normals,i*3,i*3+3)) abs(sum(n*n)-1.0)<0.000001])];
[(a.normalize(<frog version:2,generator_version:1,seed:1,parameters:{}>) ^ {null})==null,
 (a.normalize(<frog version:1,generator_version:1,seed:1,parameters:{unknown:1}>) ^ {null})==null,
 (a.normalize(<bird version:1,generator_version:1,seed:1,parameters:{feathers:21}>) ^ {null})==null,
 (atlas.wing_svg({width:1.0}) ^ {null})==null]
