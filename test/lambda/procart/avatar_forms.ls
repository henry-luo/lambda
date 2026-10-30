// PG17 / S12.1.1v3: sculpted forms, strict recipes, reusable geometry and legacy replay.
import A: lambda.procart.avatar
import s: lambda.scene3d
import t: lambda.scene3d.transform
import v: lambda.scene3d.camera
fn recipe(form) element => <avatar version:1,generator_version:2,seed:2026,form:form,ear_shape:"round",outfit:"shirt",accessory:"none",marking:"plain",parameters:{},appearance:{}>
let models=[for (form in A.FORMS) A.generate(recipe(form))^]
let spirit=A.generate(<avatar *:{*:map(recipe("genie")),accessory:"cap",ear_shape:"pointed"}>)^
let fairy=A.generate(<avatar *:{*:map(recipe("fairy")),outfit:"dress"}>)^
let human=A.generate(<avatar *:{*:map(recipe("human")),hair_style:"long",appearance:{eyes:"#294f68",trim:"#baa278"}}>)^
let resources=A.resources([*models,spirit,fairy,human]),surfaces=content(resources)
let refined=[for (g in surfaces where starts_with(g.id,"avatar-v2-")) g]
let legacy=<avatar *:{*:map(recipe("human")),generator_version:1}>
let original=A.generate(legacy)^
let upgraded=A.generate(A.upgrade(legacy)^)^
let small=A.generate(<avatar *:{*:map(human.document),parameters:{*:human.document.parameters,height:0.25,limbs:0.65,ears:0.05}}>)^
let large=A.generate(<avatar *:{*:map(fairy.document),accessory:"crown",parameters:{*:fairy.document.parameters,height:2.5,wing_span:1.8}}>)^
fn admitted(model) bool^ => s.validate(s.scene([s.camera({id:"camera"}),A.resources(model),*A.nodes(model,"wave",true,"test")^]))^
let normals_ok=all([for (g in refined) len(g.normals)==len(g.positions) and len(g.uvs)*3==len(g.positions)*2 and
 all([for (x in [*g.positions,*g.normals,*g.uvs]) t.numeric(x)]) and
 all([for (i in 0 to (len(g.normals) div 3-1)) abs(sum(slice(g.normals,i*3,i*3+3)**2.0)-1.0)<0.00001])])
// check geometric winding as well as stored unit normals, including mirrored wing resources.
fn winding(g) bool => all([for (i in 0 to (len(g.indices) div 3-1),let ids=slice(g.indices,i*3,i*3+3),
 let p=[for (j in ids) slice(g.positions,j*3,j*3+3)],let n=slice(g.normals,ids[0]*3,ids[0]*3+3)+slice(g.normals,ids[1]*3,ids[1]*3+3)+slice(g.normals,ids[2]*3,ids[2]*3+3),
 let face=v.cross(p[1]-p[0],p[2]-p[0])) sum(face*n)>=-0.00000001])
let indices_ok=all([for (g in refined) len(g.indices)%3==0 and all([for (i in g.indices) i>=0 and i<len(g.positions) div 3])]);
[all([for (m in [*models,spirit,fairy,human,small,large]) admitted(m)^]),
 all([for (m in [*models,small,large]) abs((m.hi[1]-m.lo[1])*m.scale-m.document.parameters.height)<0.000001]),
 normals_ok,indices_ok,all([for (g in refined) winding(g)]),len(unique(surfaces |> ~.id))==len(surfaces),
 all([for (m in models) all([for (p in m.parts) contains(surfaces |> ~.id,p.geometry)])]),
 len([for (p in spirit.parts where contains(["left-thigh","right-thigh","left-foot","right-foot"],p.id)) p])==0,
 len([for (p in spirit.parts where p.id=="spirit-tail" and p.surface.transparent!=true) p])==1,
 all([for (p in spirit.parts where ends_with(p.id,"-pupil")) p.color=="#101b24"]),
 len([for (p in fairy.parts where starts_with(p.id,"left-wing-") or starts_with(p.id,"right-wing-")) p])==4,
 len([for (p in human.parts where ends_with(p.id,"-iris") and p.color=="#294f68") p])==2,
 A.normalize(parse(format(human.document,'mark')^,'mark')^)^==human.document,
 A.generate(human.document)^==human,
 (A.normalize(<avatar *:map(human.document),<unknown>>) ^ { true })==true,
 (A.normalize(<avatar *:{*:map(human.document),hair_style:"unknown"}>) ^ { true })==true,
 (A.normalize(<avatar *:{*:map(human.document),appearance:{*:human.document.appearance,unknown:"#ffffff"}}>) ^ { true })==true,
 original.document.generator_version==1 and original.resources==null,
 upgraded.document.generator_version==2 and upgraded.document.parameters==original.document.parameters,
 upgraded.document.appearance.skin==original.document.appearance.skin]
hash(format(original,'json')^,'sha256')==b'8469612F4F32A8B93208E3D012C645B49253BA1F809AD4ABE5E273715BA79BA5'
