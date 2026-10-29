// PG16 / S12.1.1v3: authored morphology and surface recipes are pure, reusable values.
import c: ._common
import rng: .rng
import primitive: .primitive
import surface: .surface
import atlas: .butterfly_atlas
import scene: .scene
import s: lambda.scene3d
import fx: lambda.scene3d.effects
import t: lambda.scene3d.transform
import v: lambda.scene3d.camera
import colors: lambda.color

pub let FAMILIES=["butterfly","bird","frog","fish","bee","rabbit"]
let COMMON={size:0.2,girth:0.4,head:0.32,appendage:1.0,tail:0.45,feathers:9,
    base:"#799450",accent:"#344329",belly:"#e2dab5",frequency:10,pattern_width:0.09,warp:0.35,
    pattern:"spots",beat:2.0,amplitude:0.6}
let PROFILES={butterfly:{size:0.07,girth:0.13,appendage:1.25,base:"#e99b31",accent:"#281d16",belly:"#f8e9ba",pattern:"stripes",beat:2.5},
    bird:{size:0.24,girth:0.52,base:"#9d6945",accent:"#48382b",belly:"#dbbd87",pattern:"feathers",beat:1.4},
    frog:{size:0.1,girth:0.72,head:0.55,base:"#7a9e4d",accent:"#2e482d",belly:"#d9d5a4",pattern:"spots",beat:0.5,amplitude:0.15},
    fish:{size:0.22,girth:0.45,base:"#e9a34f",accent:"#ad5833",belly:"#efdec1",pattern:"scales",beat:1.5},
    bee:{size:0.025,girth:0.47,base:"#deb952",accent:"#342b25",belly:"#eadcaa",pattern:"stripes",beat:4.0,amplitude:0.3},
    rabbit:{size:0.4,girth:0.62,head:0.37,base:"#a69b81",accent:"#675e4e",belly:"#e8e1d1",pattern:"noise",beat:0.6,amplitude:0.12}}

pub fn normalize(document) map^ {
    let envelope=c.require(document is element and contains(FAMILIES,string(name(document))) and
        c.fields(document,["version","generator_version","seed","parameters","species"]) and len(content(document))==0 and
        document.version==1 and document.generator_version==1 and
        (document.seed is int or document.seed is string) and document.parameters is map and
        (document.species==null or c.identifier(document.species)),"invalid stored animal specimen")^
    let kind=string(name(document)),raw=document.parameters
    let keys=c.require(c.fields(raw,[for (key,value in COMMON) string(key)]),"unknown animal parameter")^
    let p={*:COMMON,*:PROFILES[kind],*:raw}
    let dims=c.require(c.bounded(p.size,0.005,5.0) and c.bounded(p.girth,0.1,1.5) and c.bounded(p.head,0.12,0.9) and
        c.bounded(p.appendage,0.3,2.0) and c.bounded(p.tail,0.1,1.5) and c.bounded(p.feathers,3,20,true) and
        c.bounded(p.frequency,3,32,true) and c.bounded(p.pattern_width,0.01,0.3) and c.bounded(p.warp,0.0,4.0) and
        c.bounded(p.beat,0.1,8.0) and c.bounded(p.amplitude,0.0,1.3) and contains(surface.KINDS,p.pattern),"animal parameter outside range")^
    let palette=[for (color in [p.base,p.accent,p.belly]) colors.parse(color)^];
    {family:kind,seed:document.seed,parameters:p,species:document.species}
}
fn part(id,geometry,position,scale,color,role,joint=null,rotation=[0.0,0.0,0.0],patterned=false) map =>
    {id:id,geometry:geometry,shape:geometry,matrix:t.matrix(position,rotation,scale)^,color:if (patterned) "#ffffff" else color,
     opacity:1.0,kind:"animal",role:role,joint:joint,surface:if (patterned) {pattern:"skin",resolution:96,roughness:0.7} else null}
fn oval(id,position,scale,color,role,joint=null,patterned=false) map => part(id,"animal-round",position,scale,color,role,joint,[0.0,0.0,0.0],patterned)
// cylinder geometry runs along X; this orthonormal frame avoids Euler-angle discontinuities.
fn link(id,a,b,radius,color,role,joint=null,patterned=false) map {
    let delta=v.subtract(b,a),length=sqrt(v.dot(delta,delta)),axis=v.unit(delta)
    let across=v.unit(v.cross(axis,if (abs(axis[1])<0.9) [0.0,1.0,0.0] else [0.0,0.0,1.0])),up=v.cross(axis,across)
    let center=(a+b)/2.0;
    {*:part(id,"animal-rod",[0.0,0.0,0.0],[1.0,1.0,1.0],color,role,joint,[0.0,0.0,0.0],patterned),
     matrix:[*[for (x in axis) x*length],0.0,*[for (x in across) x*radius],0.0,*[for (x in up) x*radius],0.0,*center,1.0]}
}
fn blade(id,position,scale,color,role,joint=null,rotation=[0.0,0.0,0.0],patterned=false) map =>
    part(id,"animal-blade",position,scale,color,role,joint,rotation,patterned)
fn joint(id,pivot,axis,sign=1.0) map => {id:id,pivot:pivot,axis:axis,sign:sign}
fn eyes(p,z,y,width) array => [for (side in [-1.0,1.0])
    oval("eye-"++string(side),[side*width,y,z],[0.075,0.075,0.075],"#171c13","eye")]
fn limbs(p,frog=false) array => [for (side in [-1.0,1.0],row in [0,1],
    let root=[side*p.girth*0.32,0.28,if (row==0) -0.25 else 0.35],
    let knee=[side*(p.girth*0.45+0.18*p.appendage),if (frog and row==1) 0.28 else 0.12,if (row==0) -0.15 else 0.5],
    let foot=[side*(p.girth*0.45+0.07*p.appendage),0.055,if (row==0) -0.45*p.appendage else 0.2],
    let id="leg-"++string(side)++"-"++string(row)) *[
        link(id++"-upper",root,knee,if (row==1) 0.11 else 0.06,p.base,"skin",null,true),
        link(id++"-lower",knee,foot,0.045,p.base,"skin",null,true),
        *[for (toe in 0 to (if (frog) (if (row==0) 3 else 4) else 2),let offset=(float(toe)-(if (frog) (if (row==0) 1.5 else 2.0) else 1.0))*0.045)
            link(id++"-toe-"++string(toe),foot,foot+[offset,0.0,-0.10*p.appendage],0.013,p.base,"toe",null,true)]]]
fn butterfly(p) map {
    let hinge=0.025;
    {parts:[oval("thorax",[0.0,0.13,-0.12],[p.girth,0.15,0.45],p.accent,"body"),
        oval("abdomen",[0.0,0.12,0.28],[p.girth*0.65,0.13,0.58],p.accent,"body"),
        oval("head",[0.0,0.14,-0.4],[p.head*0.5,p.head*0.5,p.head*0.5],p.accent,"head"),
        *[for (side in [-1.0,1.0],let id=if (side<0) "wing-left" else "wing-right")
            part(id,"animal-wing",[0.0,0.0,0.0],[side*1.55*p.appendage,1.0,1.35],"#ffffff","wing",id)],
        *[for (side in [-1.0,1.0]) link("antenna-"++string(side),[side*0.04,0.15,-0.42],[side*0.18,0.3,-0.75],0.012,p.accent,"antenna")]],
     joints:[joint("wing-left",[-hinge,0.13,0.0],[0.0,0.0,1.0],-1.0),joint("wing-right",[hinge,0.13,0.0],[0.0,0.0,1.0])]}
}
fn bird(p) map {
    {parts:[oval("body",[0.0,0.48,0.1],[p.girth,0.65,0.95],p.base,"body",null,true),
        oval("neck",[0.0,0.7,-0.25],[p.head*0.8,0.37,p.head],p.base,"neck",null,true),
        oval("breast",[0.0,0.43,-0.13],[p.girth*0.84,0.48,0.6],p.belly,"belly"),
        oval("head",[0.0,0.83,-0.37],[p.head,p.head,p.head],p.base,"head",null,true),
        part("beak","animal-beak",[0.0,0.8,-0.57],[0.1,0.12,0.22],p.accent,"beak"),
        *eyes(p,-0.48,0.87,p.head*0.4),
        *[for (side in [-1.0,1.0],i in 0 to (p.feathers-1),let id=if (side<0) "wing-left" else "wing-right",
            let u=float(i)/float(p.feathers-1))
            blade(id++"-"++string(i),[side*u*0.42*p.appendage,0.015*float(i),u*0.28],
                [side*(0.68-0.18*u)*p.appendage,0.3,0.09+0.035*u],p.base,"feather",id,[0.0,side*(0.6+0.9*u),0.0],true)],
        *[for (i in 0 to 4,let u=float(i-2)) blade("tail-"++string(i),[u*0.035,0.0,0.0],
            [p.tail,0.25,0.085],p.base,"feather","tail",[0.0,-math.pi/2.0+u*0.15,0.0],true)],
        *[for (side in [-1.0,1.0]) *[
            link("leg-"++string(side),[side*0.12,0.3,0.13],[side*0.15,0.06,0.02],0.023,p.accent,"leg"),
            *[for (toe in -1 to 1) link("toe-"++string(side)++"-"++string(toe),[side*0.15,0.06,0.02],
                [side*0.15+float(toe)*0.06,0.03,-0.15],0.012,p.accent,"toe")]]]],
     joints:[joint("wing-left",[-p.girth*0.34,0.62,0.0],[0.0,0.0,1.0],-1.0),
        joint("wing-right",[p.girth*0.34,0.62,0.0],[0.0,0.0,1.0]),joint("tail",[0.0,0.5,0.48],[1.0,0.0,0.0],0.25)]}
}
fn frog(p) map => {parts:[oval("body",[0.0,0.32,0.12],[p.girth,0.45,0.9],p.base,"body",null,true),
    oval("belly",[0.0,0.18,0.02],[p.girth*0.86,0.22,0.7],p.belly,"belly"),
    oval("head",[0.0,0.37,-0.32],[p.head*1.4,0.32,p.head],p.base,"head",null,true),
    *[for (side in [-1.0,1.0]) oval("eye-mount-"++string(side),[side*p.head*0.4,0.54,-0.4],[0.16,0.19,0.18],p.base,"skin",null,true)],
    *eyes(p,-0.46,0.58,p.head*0.4),*limbs(p,true),
    oval("throat",[0.0,0.24,-0.47],[p.head*0.75,0.16,0.19],p.belly,"throat","throat")],
    joints:[joint("throat",[0.0,0.0,0.0],[1.0,0.0,0.0],0.12)]}
fn fish(p) map => {parts:[oval("body",[0.0,0.5,0.0],[p.girth,0.63,1.05],p.base,"body",null,true),
    oval("belly",[0.0,0.34,-0.05],[p.girth*0.76,0.24,0.8],p.belly,"belly"),
    oval("head",[0.0,0.53,-0.37],[p.head,p.head*1.15,p.head],p.base,"head",null,true),*eyes(p,-0.43,0.58,p.head*0.45),
    blade("dorsal",[-0.015,0.7,0.28],[0.58,0.18,0.24*p.appendage],p.accent,"fin",null,[math.pi/2.0,math.pi/2.0,0.0]),
    *[for (side in [-1.0,1.0]) blade("pectoral-"++string(side),[side*p.girth*0.35,0.45,-0.12],
        [side*0.38*p.appendage,0.1,0.16],p.accent,"fin",null,[0.0,-side*0.6,0.0])],
    *[for (side in [-1.0,1.0]) blade("tail-fin-"++string(side),[0.0,0.0,0.0],
        [p.tail,0.12,0.3],p.base,"fin","tail",[math.pi/2.0,-math.pi/2.0,side*0.55],true)]],
    joints:[joint("tail",[0.0,0.5,0.45],[0.0,1.0,0.0])]}
fn bee(p) map => {parts:[oval("abdomen",[0.0,0.33,0.26],[p.girth,0.42,0.62],p.base,"body",null,true),
    oval("thorax",[0.0,0.38,-0.15],[p.girth*0.8,0.42,0.4],p.base,"body",null,true),
    oval("head",[0.0,0.38,-0.43],[p.head,p.head,p.head],p.accent,"head"),*eyes(p,-0.52,0.43,p.head*0.4),
    *[for (side in [-1.0,1.0]) link("antenna-"++string(side),[side*0.06,0.47,-0.5],[side*0.15,0.66,-0.64],0.012,p.accent,"antenna")],
    *[for (side in [-1.0,1.0],row in 0 to 2,let id="leg-"++string(side)++"-"++string(row),let z=-0.2+float(row)*0.22)
        *[link(id,[side*0.1,0.27,z],[side*0.32,0.12,z+0.1],0.025,p.accent,"leg"),
          link(id++"-foot",[side*0.32,0.12,z+0.1],[side*0.43,0.03,z+0.04],0.016,p.accent,"leg")]],
    *[for (side in [-1.0,1.0],row in 0 to 1,let id=if (side<0) "wing-left" else "wing-right")
        blade(id++"-"++string(row),[0.0,0.025*float(row),float(row)*0.15],
            [side*0.9*p.appendage,0.2,0.2-float(row)*0.05],"#dbe5dc","wing",id,[0.0,side*(0.15+float(row)*0.35),0.0])]],
    joints:[joint("wing-left",[-0.1,0.52,-0.2],[0.0,0.0,1.0],-1.0),joint("wing-right",[0.1,0.52,-0.2],[0.0,0.0,1.0])]}
fn rabbit(p) map => {parts:[oval("body",[0.0,0.39,0.08],[p.girth,0.65,1.0],p.base,"body",null,true),
    oval("head",[0.0,0.65,-0.42],[p.head,p.head*1.1,p.head],p.base,"head",null,true),
    oval("nose",[0.0,0.63,-0.64],[0.11,0.08,0.1],p.accent,"nose"),*eyes(p,-0.5,0.73,p.head*0.4),
    *[for (side in [-1.0,1.0]) *[
        oval("ear-"++string(side),[side*0.12,0.98,-0.35],[0.13,0.6*p.appendage,0.12],p.base,"ear",null,true),
        oval("inner-ear-"++string(side),[side*0.12,0.98,-0.415],[0.07,0.43*p.appendage,0.025],p.belly,"inner-ear")]],
    *limbs(p),oval("tail",[0.0,0.4,0.62],[p.tail*0.5,p.tail*0.5,p.tail*0.5],p.belly,"tail","tail")],
    joints:[joint("tail",[0.0,0.0,0.0],[1.0,0.0,0.0],0.15)]}

pub fn generate(document) map^ {
    let doc=normalize(document)^,p=doc.parameters
    let anatomy=if (doc.family=="butterfly") butterfly(p) else if (doc.family=="bird") bird(p) else if (doc.family=="frog") frog(p)
        else if (doc.family=="fish") fish(p) else if (doc.family=="bee") bee(p) else rabbit(p)
    let feather=primitive.blade(6,4,0.08,0.035)
    let resources=[s.sphere(12,20,{id:"animal-round"}),primitive.geometry("cylinder",{segments:10},"animal-rod"),
        s.geometry(feather.positions,{id:"animal-blade",normals:feather.normals,uvs:feather.uvs,indices:feather.indices}),
        s.geometry([0.0,0.0,-0.5,1.0,0.0,-0.5,1.0,0.0,0.5,0.0,0.0,0.5],
            {id:"animal-wing",normals:[0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0,0.0,0.0,1.0,0.0],uvs:[0.0,1.0,1.0,1.0,1.0,0.0,0.0,0.0],indices:[0,2,1,0,3,2]}),
        s.geometry([-0.5,-0.5,0.0,0.5,-0.5,0.0,0.0,0.5,0.0,0.0,0.0,-1.0],{id:"animal-beak",indices:[0,1,3,1,2,3,2,0,3,0,2,1]})]
    let base={resources:resources,parts:anatomy.parts,seed:doc.seed,stats:{vertices:sum([for (g in resources) if (g.type=='buffer') len(g.positions) div 3 else 24]),objects:len(anatomy.parts)}}
    let definition=<pattern id:"skin",generator:p.pattern,version:1,parameters:{colors:[p.accent,p.base],frequency:float(p.frequency),width:p.pattern_width,warp:p.warp}>
    let baked=surface.bake(base,[definition],doc.seed,{max_surface_pixels:262144,max_vertices:100000})^
    let wing=if (doc.family=="butterfly") fx.svg_texture(atlas.wing_svg({base:p.base,mark:p.accent,edge:p.belly,
        frequency:p.frequency,width:p.pattern_width,warp:p.warp,style:p.pattern,seed:rng.below(rng.key(doc.seed),2147483647)})^,{id:"animal-wing-paint",width:256,height:512})^ else null
    let parts=[for (part in baked.parts) if (doc.family=="butterfly" and part.role=="wing")
        {*:part,surface:{texture:"animal-wing-paint",roughness:0.7,metalness:0.0,'alpha-mode':'mask','alpha-cutoff':0.3}} else part];
    {*:baked,parts:parts,resources:[*baked.resources,*(if (wing==null) [] else [wing])],family:doc.family,parameters:p,joints:anatomy.joints,
        species:doc.species,scale:p.size,stats:{*:baked.stats,joints:len(anatomy.joints),
            surface_pixels:c.value(baked.stats.surface_pixels,0)+(if (wing==null) 0 else 131072),
            surface_textures:c.value(baked.stats.surface_textures,0)+(if (wing==null) 0 else 1)}}
}
pub fn nodes(model,phase=0.0,motion=false,prefix="specimen") array {
    let p=model.parameters
    let groups=[for (joint in model.joints) s.group(scene.meshes({parts:[for (part in model.parts where part.joint==joint.id) part]},
        {'cast-shadow':true,'receive-shadow':true}),{id:prefix++"-"++joint.id,position:joint.pivot,
        quaternion:t.quaternion_axis_angle(joint.axis,sin(phase*2.0*math.pi)*p.amplitude*joint.sign)})]
    let duration=1.0/p.beat,times=[for (i in 0 to 32) float(i)/32.0*duration]
    let tracks=[for (joint in model.joints) s.track(prefix++"-"++joint.id++".quaternion",times,
        [for (i in 0 to 32) *t.quaternion_axis_angle(joint.axis,sin((phase+float(i)/32.0)*2.0*math.pi)*p.amplitude*joint.sign)],'quaternion')];
    [s.resources(model.resources),s.group([*scene.meshes({parts:[for (part in model.parts where part.joint==null) part]},
        {'cast-shadow':true,'receive-shadow':true}),*groups],{id:prefix,scale:[model.scale,model.scale,model.scale],'data-family':model.family}),
     *(if (motion and len(tracks)>0) [s.clip(prefix++"-motion",tracks,{duration:duration,autoplay:true})] else [])]
}

pub let ACTIVITIES=["rest","walk","hop","graze","takeoff","fly","glide","land","perch","hover","visit","swim","call","leave","stranded"]
fn locomotion(model,activity) map {
 let p=model.parameters
 let legs=if (contains(["rabbit","frog"],model.family)) [for (side in [-1.0,1.0],row in [0,1])
  {id:"leg-"++string(side)++"-"++string(row),pivot:[side*p.girth*0.32,0.28,if (row==0) -0.25 else 0.35],
   axis:[1.0,0.0,0.0],sign:if (row==0) 1.0 else -1.0,offset:if (side<0.0) 0.0 else math.pi,kind:"leg"}]
  else if (model.family=="bird") [for (side in [-1.0,1.0]) {id:"foot-"++string(side),pivot:[side*0.12,0.3,0.13],
   axis:[1.0,0.0,0.0],sign:1.0,offset:if (side<0.0) 0.0 else math.pi,kind:"leg"}] else []
 let head=if (contains(["rabbit","bird"],model.family)) [{id:"head-pose",pivot:[0.0,0.55,-0.25],axis:[1.0,0.0,0.0],sign:1.0,offset:0.0,kind:"head"}] else []
 let parts=[for (part in model.parts,
  let leg=[for (j in legs where starts_with(part.id,j.id++"-") or part.id==j.id or
   (model.family=="bird" and (part.id=="leg-"++slice(j.id,5,len(j.id)) or starts_with(part.id,"toe-"++slice(j.id,5,len(j.id))++"-")))) j][0],
  let is_head=len(head)>0 and (contains(["head","beak","nose","eye","ear","inner-ear"],part.role)),
  let picked=if (leg!=null) leg else if (is_head) head[0] else null)
  if (part.joint!=null or picked==null) part else {*:part,joint:picked.id,
   matrix:t.multiply(t.matrix(0.0-picked.pivot,[0.0,0.0,0.0],[1.0,1.0,1.0])^,part.matrix)^}];
 {*:model,parts:parts,joints:[*[for (j in model.joints) {*:j,kind:if (starts_with(j.id,"wing")) "wing" else j.id,offset:0.0}],*legs,*head]}
}
fn pose_angle(model,j,phase,activity) float {
 let mode=activity.state,p=model.parameters,wave=sin(phase*2.0*math.pi+j.offset)
 let moving=activity.speed>0.01 and contains(["walk","hop","swim","fly","takeoff","land","hover","leave"],mode)
 let flight=contains(["fly","takeoff","land","hover","leave"],mode)
 let amount=if (j.kind=="wing") (if (flight) p.amplitude*wave else if (mode=="glide") 0.08*wave else if (model.family=="bird") 1.15
   else if (model.family=="butterfly") 1.2 else 0.15)
  else if (j.kind=="leg") (if (moving) (if (mode=="hop") 0.65 else 0.4)*wave else if (flight or mode=="glide") 0.7 else 0.0)
  // resting animals retain small local motions without moving their habitat anchor.
  else if (j.kind=="head") (if (mode=="graze") 0.55+0.08*wave else if (mode=="walk") 0.05*wave else if (contains(["rest","perch"],mode)) 0.035*wave else 0.0)
  else if (j.kind=="tail") (if (model.family=="fish" and contains(["swim","rest"],mode))
   (if (mode=="rest") 0.04 else 0.12+min(0.5,activity.speed))*wave else if (moving) 0.08*wave else 0.0)
  else if (j.kind=="throat" and mode=="call") 0.12*wave else 0.0;
 amount*j.sign
}
fn pose_offset(model,phase,activity) array => [0.0,
 if (activity.state=="hop" and activity.speed>0.01) model.scale*0.15*abs(sin(phase*2.0*math.pi)) else 0.0,0.0]
fn pose_scale(j,phase,activity) array {
 let calling=activity.state=="call",wave=max(0.0,sin(phase*2.0*math.pi));
 if (j.kind=="throat" and (calling or activity.state=="rest"))
  [1.0+(if (calling) 0.22 else 0.015)*wave,1.0+(if (calling) 0.4 else 0.04)*wave,1.0] else [1.0,1.0,1.0]
}
// State-dependent articulated poses reuse the same morphology and material resources.
pub fn pose_nodes(model,activity={state:"rest",speed:0.0},phase=0.0,motion=false,prefix="specimen") array^ {
 let valid=c.require(activity is map and c.fields(activity,["state","speed"]) and contains(ACTIVITIES,activity.state) and
  c.bounded(activity.speed,0.0,20.0) and c.bounded(phase,0.0,1.0),"invalid animal presentation activity")^
 let rig=locomotion(model,activity)
 let duration=1.0/max(0.1,min(8.0,if (contains(["walk","hop"],activity.state)) max(0.5,activity.speed/max(0.005,model.scale)*1.2) else model.parameters.beat))
 let times=[for (i in 0 to 32) float(i)/32.0*duration]
 let groups=[for (j in rig.joints) s.group(scene.meshes({parts:[for (part in rig.parts where part.joint==j.id) part]},
  {'cast-shadow':true,'receive-shadow':true}),{id:prefix++"-"++j.id,position:j.pivot,
   quaternion:t.quaternion_axis_angle(j.axis,pose_angle(rig,j,phase,activity)),scale:pose_scale(j,phase,activity)})]
 let tracks=[*[for (j in rig.joints) s.track(prefix++"-"++j.id++".quaternion",times,
  [for (i in 0 to 32) *t.quaternion_axis_angle(j.axis,pose_angle(rig,j,phase+float(i)/32.0,activity))],'quaternion')],
  s.track(prefix++".position",times,[for (i in 0 to 32) *pose_offset(model,phase+float(i)/32.0,activity)],'vector'),
  *[for (j in rig.joints where j.kind=="throat") s.track(prefix++"-"++j.id++".scale",times,
   [for (i in 0 to 32) *pose_scale(j,phase+float(i)/32.0,activity)],'vector')]];
 [s.resources(rig.resources),s.group([*scene.meshes({parts:[for (part in rig.parts where part.joint==null) part]},
  {'cast-shadow':true,'receive-shadow':true}),*groups],{id:prefix,position:pose_offset(model,phase,activity),scale:[model.scale,model.scale,model.scale],'data-family':model.family,'data-activity':activity.state}),
  *(if (motion) [s.clip(prefix++"-activity",tracks,{duration:duration,autoplay:true})] else [])]
}
