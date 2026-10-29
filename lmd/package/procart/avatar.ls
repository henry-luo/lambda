// PG17 / S12.1.1v3: saved character recipes generate articulated Scene3D values.
import c: ._common
import rng: .rng
import s: lambda.scene3d
import t: lambda.scene3d.transform
import color: lambda.color

pub let FORMS=["plush","robot","human","fairy","genie"]
pub let ACTIONS=["idle","walk","run","play","wave","sit"]
pub let DEFAULTS={height:1.25,girth:0.34,head:0.32,limbs:1.0,ears:0.16,tail:0.15,wing_span:1.0}
pub let PALETTE={body:"#d4a34d",clothes:"#b74939",accent:"#493a2c",hair:"#5a382a",skin:"#dba37e"}
pub fn normalize(doc) element^ {
 let valid=c.require(doc is element and name(doc)=='avatar' and doc.version==1 and doc.generator_version==1 and
  c.fields(doc,["version","generator_version","seed","form","ear_shape","outfit","accessory","marking","parameters","appearance"]) and
  len(content(doc))==0 and doc.seed is int and c.bounded(doc.seed,0,2147483647,true) and contains(FORMS,doc.form) and
  contains(["round","long","pointed","floppy","none"],doc.ear_shape) and
  contains(["shirt","dress","tunic","hide","suit","none"],doc.outfit) and
  contains(["none","hood","crown","wig","antenna","cap"],doc.accessory) and contains(["plain","stripes","spots"],doc.marking) and
  doc.parameters is map and doc.appearance is map and c.fields(doc.parameters,[for (k,v in DEFAULTS) string(k)]) and
  c.fields(doc.appearance,[for (k,v in PALETTE) string(k)]),"avatar: invalid recipe or unknown field")^
 let p={*:DEFAULTS,*:doc.parameters},a={*:PALETTE,*:doc.appearance}
 let ranges=c.require(c.bounded(p.height,0.25,2.5) and c.bounded(p.girth,0.2,0.65) and c.bounded(p.head,0.2,0.48) and
  c.bounded(p.limbs,0.65,1.25) and c.bounded(p.ears,0.05,0.5) and c.bounded(p.tail,0.0,0.5) and c.bounded(p.wing_span,0.5,1.8),"avatar: proportions outside range")^
 let paints=[for (k,v in a) color.parse(v)^];
 <avatar *:{*:map(doc),parameters:p,appearance:a}>
}
pub fn resources() element => s.resources([s.sphere(10,16,{id:"avatar-round"}),s.box([1.0,1.0,1.0],{id:"avatar-box"})])
fn part(id,joint,position,scale,paint,shape="round",opacity=1.0) map =>
 {id:id,joint:joint,position:position,scale:scale,color:paint,shape:shape,opacity:opacity}
fn bone(id,parent,position) map => {id:id,parent:parent,position:position}
fn rig(p) array => [bone("hips",null,[0.0,0.4*p.limbs+0.04,0.0]),bone("chest","hips",[0.0,0.16,0.0]),
 bone("head","chest",[0.0,0.33,0.0]),
 *[for (side in [-1.0,1.0],let id=if (side<0) "left" else "right") *[
  bone(id++"-arm","chest",[side*(p.girth*0.5+0.04),0.17,0.0]),bone(id++"-forearm",id++"-arm",[0.0,-0.18*p.limbs,0.0]),
  bone(id++"-leg","hips",[side*p.girth*0.27,0.0,0.0]),bone(id++"-shin",id++"-leg",[0.0,-0.2*p.limbs,0.0]),
  bone(id++"-wing","chest",[side*0.09,0.14,-0.10])]]]
fn anatomy(doc) array {
 let p=doc.parameters,a=doc.appearance,robot=doc.form=="robot",plush=doc.form=="plush"
 let shape=if (robot) "box" else "round",skin=if (plush or robot or doc.form=="genie") a.body else a.skin
 let coat=if (doc.outfit=="none") skin else a.clothes;
 [part("torso","chest",[0.0,0.02,0.0],[p.girth,0.34,0.24],coat,shape),
  part("pelvis","hips",[0.0,0.01,0.0],[p.girth*0.85,0.16,0.22],coat,shape),
  part("head","head",[0.0,0.0,0.0],[p.head,p.head*0.95,p.head*0.85],skin,shape),
  *[for (side in [-1.0,1.0],let id=if (side<0) "left" else "right") *[
   part(id++"-upper-arm",id++"-arm",[0.0,-0.09*p.limbs,0.0],[0.095,0.22*p.limbs,0.095],coat,shape),
   part(id++"-forearm",id++"-forearm",[0.0,-0.085*p.limbs,0.0],[0.075,0.19*p.limbs,0.075],skin,shape),
   part(id++"-hand",id++"-forearm",[0.0,-0.17*p.limbs,0.015],[0.10,0.10,0.085],skin,shape),
   part(id++"-thigh",id++"-leg",[0.0,-0.10*p.limbs,0.0],[0.13,0.23*p.limbs,0.14],if (doc.outfit=="suit") a.clothes else skin,shape),
   part(id++"-calf",id++"-shin",[0.0,-0.09*p.limbs,0.0],[0.10,0.20*p.limbs,0.11],skin,shape),
   part(id++"-foot",id++"-shin",[0.0,-0.20*p.limbs,0.045],[0.14,0.08,0.23],if (plush) a.body else a.accent,shape),
   part(id++"-eye-white","head",[side*p.head*0.22,0.025,p.head*0.38],[p.head*0.23,p.head*0.25,0.035],"#fff8df"),
   part(id++"-pupil","head",[side*p.head*0.22,0.025,p.head*0.43],[p.head*0.10,p.head*0.14,0.025],a.accent),
   *(if (doc.ear_shape=="none") [] else [part(id++"-ear","head",[side*p.head*0.40,if (doc.ear_shape=="floppy") -0.02 else p.head*0.40,0.0],
     [if (doc.ear_shape=="long") p.ears*0.4 else p.ears,if (contains(["long","floppy"],doc.ear_shape)) p.ears*1.7 else p.ears,0.075],skin),
    part(id++"-inner-ear","head",[side*p.head*0.40,if (doc.ear_shape=="floppy") -0.02 else p.head*0.40,0.043],
     [p.ears*0.4,p.ears*(if (doc.ear_shape=="long") 1.1 else 0.5),0.025],a.accent)]),
   *(if (robot) [part(id++"-elbow",id++"-forearm",[0.0,0.0,0.0],[0.11,0.11,0.11],a.accent),
    part(id++"-knee",id++"-shin",[0.0,0.0,0.0],[0.12,0.12,0.12],a.accent)] else [])]],
  part("nose","head",[0.0,-0.025,p.head*(if (plush) 0.47 else 0.43)],
   [p.head*0.22,p.head*0.14,p.head*(if (plush) 0.30 else 0.13)],if (plush) a.accent else skin),
  part("smile","head",[0.0,-p.head*0.22,p.head*0.39],[p.head*0.23,0.015,0.018],a.accent),
  *(if (plush and p.tail>0.0) [part("tail","hips",[0.0,0.07,-0.18],[p.tail*0.6,p.tail*0.6,p.tail],a.body)] else []),
  *(if (doc.outfit=="dress" or doc.outfit=="hide") [part("skirt","hips",[0.0,-0.06,0.0],[p.girth*1.55,0.34,0.36],a.clothes)] else []),
  *(if (contains(["human","fairy"],doc.form)) [part("hair-cap","head",[0.0,p.head*0.25,-0.025],[p.head*1.02,p.head*0.65,p.head*0.9],a.hair),
   *[for (side in [-1.0,1.0]) part("hair-side-"++string(side),"head",[side*p.head*0.43,0.0,-0.025],[p.head*0.25,p.head*0.85,p.head*0.75],a.hair)]] else []),
  *(if (doc.accessory=="hood") [part("hood-back","head",[0.0,0.03,-0.07],[p.head*1.25,p.head*1.25,p.head],a.clothes),
   part("cape","chest",[0.0,-0.05,-0.14],[p.girth*1.4,0.58,0.08],a.clothes)] else []),
  *(if (doc.accessory=="crown") [for (i in -1 to 1) part("crown-"++string(i),"head",[float(i)*0.075,p.head*0.65,0.0],[0.055,0.12,0.08],"#e1b345","box")] else []),
  *(if (doc.accessory=="wig") [for (side in [-1.0,1.0],i in 0 to 3) part("wig-"++string(side)++"-"++string(i),"head",
   [side*p.head*0.45,-0.025-float(i)*0.045,0.0],[0.115,0.10,0.16],"#e6e1d0")] else []),
  *(if (doc.accessory=="antenna") [part("antenna-stalk","head",[0.0,p.head*0.75,0.0],[0.025,0.18,0.025],a.accent,"box"),
   part("antenna-light","head",[0.0,p.head*0.75+0.10,0.0],[0.075,0.075,0.075],"#a7e69b")] else []),
  *(if (doc.accessory=="cap") [part("cap","head",[0.0,p.head*0.46,0.0],[p.head*1.15,0.12,p.head*1.12],a.clothes)] else []),
  *(if (doc.marking=="plain") [] else [for (i in 0 to 4,let jitter=rng.between(rng.derive(rng.key(doc.seed),["mark",i]),-0.015,0.015))
   part("mark-"++string(i),"chest",[if (doc.marking=="spots") (float(i%2)-0.5)*p.girth*0.45 else 0.0,
    -0.10+float(i)*0.055+jitter,0.121],[if (doc.marking=="stripes") p.girth*0.86 else 0.05,0.025,0.015],a.accent,"box")]),
  *(if (doc.form=="fairy") [for (side in [-1.0,1.0],row in [0,1],let id=if (side<0) "left" else "right")
   part(id++"-wing-"++string(row),id++"-wing",[side*p.wing_span*0.25,if (row==0) 0.15 else -0.1,-0.04],
    [p.wing_span*0.60,if (row==0) 0.48 else 0.30,0.035],"#d9f1e0","round",0.55)] else []),
  *(if (doc.form=="genie") [for (i in 0 to 4) part("magic-"++string(i),"hips",[sin(float(i)*1.8)*0.08,-0.08-float(i)*0.05,0.0],
   [0.3-float(i)*0.04,0.1,0.25-float(i)*0.035],a.accent,"round",0.5)] else [])]
}
fn origin(joints,id) array => if (id==null) [0.0,0.0,0.0] else
 (let j=[for (j in joints where j.id==id) j][0],j.position+origin(joints,j.parent))
pub fn generate(description) map^ {
 let doc=normalize(description)^,joints=rig(doc.parameters),parts=anatomy(doc)
 let corners=[for (p in parts,let center=origin(joints,p.joint)+p.position) for (side in [-1.0,1.0]) center+p.scale*(side*0.5)]
 let lo=[for (axis in 0 to 2) min(corners |> ~[axis])],hi=[for (axis in 0 to 2) max(corners |> ~[axis])];
 {document:doc,joints:joints,parts:parts,lo:lo,hi:hi,scale:doc.parameters.height/(hi[1]-lo[1])}
}
// local poses keep feet at the root's ground anchor; sit bends hips and knees together.
fn angle(id,action,phase) float {
 let wave=sin(phase*2.0*math.pi),side=if (starts_with(id,"left")) -1.0 else 1.0
 let walking=contains(["walk","run"],action)
 if (ends_with(id,"-wing")) side*(0.25+sin(phase*4.0*math.pi)*0.55)
 else if (ends_with(id,"-leg")) if (action=="sit") -math.pi/2.0 else if (walking) side*wave*(if (action=="run") 0.8 else 0.5) else if (action=="play") side*wave*0.25 else 0.0
 else if (ends_with(id,"-shin")) if (action=="sit") math.pi/2.0 else if (walking) max(0.0,side*wave)*(if (action=="run") 1.0 else 0.65) else 0.05
 else if (ends_with(id,"-forearm")) if (action=="wave" and side>0) -1.3+wave*0.3 else if (action=="sit") -0.9 else -0.12
 else if (ends_with(id,"-arm")) if (action=="wave" and side>0) -2.0+wave*0.1 else if (walking) -side*wave*0.45 else if (action=="play") -0.6+side*wave*0.7 else 0.05*wave
 else if (id=="head") wave*0.035 else 0.0
}
fn axis(id) array => if (ends_with(id,"-wing")) [0.0,1.0,0.0] else if (id=="head") [0.0,1.0,0.0] else [1.0,0.0,0.0]
fn position(model,j,action,phase) array => j.position+(if (j.id!="hips") [0.0,0.0,0.0] else
 [0.0,if (action=="sit") -0.2*model.document.parameters.limbs else if (action=="play") abs(sin(phase*2.0*math.pi))*0.14 else
  if (contains(["walk","run"],action)) abs(sin(phase*2.0*math.pi))*0.018 else 0.003*sin(phase*2.0*math.pi),0.0])
fn joint_node(model,j,prefix,action,phase) element => s.group([
 *[for (p in model.parts where p.joint==j.id) <mesh id:prefix++"-part-"++p.id,geometry:"avatar-"++p.shape,position:p.position,scale:p.scale,
  'cast-shadow':true,'receive-shadow':true,s.material('standard',{color:p.color,roughness:if (model.document.form=="robot") 0.35 else 0.85,
   metalness:if (model.document.form=="robot") 0.5 else 0.0,opacity:p.opacity,transparent:p.opacity<1.0})>],
 *[for (child in model.joints where child.parent==j.id) joint_node(model,child,prefix,action,phase)]],
 {id:prefix++"-"++j.id,position:position(model,j,action,phase),quaternion:t.quaternion_axis_angle(axis(j.id),angle(j.id,action,phase))})
pub fn nodes(model,action="idle",playing=true,prefix="avatar",phase=0.0,pace=1.0) array^ {
 let valid=c.require(contains(ACTIONS,action) and playing is bool and c.identifier(prefix) and c.bounded(phase,0.0,1.0) and c.bounded(pace,0.2,3.0),"avatar: invalid playback")^
 let duration=(if (action=="run") 0.55 else if (action=="walk") 1.0 else 2.0)/pace
 let times=[for (i in 0 to 24) float(i)/24.0*duration];
 [s.group([for (j in model.joints where j.parent==null) joint_node(model,j,prefix,action,phase)],
  {id:prefix,scale:[model.scale,model.scale,model.scale],position:[0.0,-model.lo[1]*model.scale,0.0],
   'data-avatar-form':model.document.form,'data-avatar-action':action}),
  *(if (not playing) [] else [s.clip(prefix++"-pose",[
   *[for (j in model.joints) s.track(prefix++"-"++j.id++".quaternion",times,
    [for (i in 0 to 24) *t.quaternion_axis_angle(axis(j.id),angle(j.id,action,phase+float(i)/24.0))],'quaternion')],
   s.track(prefix++"-hips.position",times,[for (i in 0 to 24) *position(model,model.joints[0],action,phase+float(i)/24.0)],'vector')],
   {duration:duration,autoplay:true})])]
}
