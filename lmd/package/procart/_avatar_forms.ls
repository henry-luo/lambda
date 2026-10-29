// PG17: form-specific silhouettes share one articulated surface vocabulary.
import t: lambda.scene3d.transform
import color: lambda.color
import geometry: ._avatar_geometry

fn tint(paint,gain) string^ => color.hex(color.parse(paint)^*gain)
fn piece(id,joint,shape,position,scale,paint,rotation=[0.0,0.0,0.0],surface={}) map =>
 {id:id,joint:joint,shape:shape,geometry:"avatar-v2-"++shape,position:position,scale:scale,rotation:rotation,color:paint,opacity:1.0,
  surface:{roughness:0.78,metalness:0.0,*:surface}}
fn bone(id,parent,position) map => {id:id,parent:parent,position:position}
pub fn plan(doc) map {
 let p=doc.parameters,plush=doc.form=="plush",robot=doc.form=="robot",fairy=doc.form=="fairy",spirit=doc.form=="genie"
 let upper=(if (plush) 0.15 else if (robot) 0.21 else 0.25)*p.limbs,
  lower=(if (plush) 0.15 else if (robot) 0.23 else 0.25)*p.limbs;
 {head:p.head*(if (plush) 1.16 else if (robot) 1.05 else if (fairy) 0.90 else if (spirit) 0.93 else 0.76),
  width:p.girth*(if (spirit) 1.10 else 1.0),depth:if (plush) 0.29 else if (robot) 0.22 else if (spirit) 0.26 else 0.22,
  torso:if (plush) 0.39 else if (spirit) 0.46 else 0.43,
  hip:if (spirit) 0.56 else upper+lower+0.055,
  chest:if (plush) 0.14 else 0.19,head_y:if (plush) 0.32 else if (robot) 0.34 else 0.32,
  upper:upper,lower:lower,arm:(if (plush) 0.17 else 0.23)*p.limbs,fore:(if (plush) 0.15 else 0.21)*p.limbs,
  arm_width:if (plush) 0.16 else if (robot) 0.11 else if (spirit) 0.13 else 0.085,
  leg_width:if (plush) 0.16 else if (robot) 0.15 else 0.13}
}
fn rig(doc,p) array => [bone("hips",null,[0.0,p.hip,0.0]),bone("chest","hips",[0.0,p.chest,0.0]),
 bone("head","chest",[0.0,p.head_y,0.0]),
 *[for (side in [-1.0,1.0],let id=if (side<0.0) "left" else "right") *[
  bone(id++"-arm","chest",[side*p.width*0.53,p.torso*0.29,0.0]),
  bone(id++"-forearm",id++"-arm",[0.0,-p.arm,0.0]),bone(id++"-hand",id++"-forearm",[0.0,-p.fore,0.0]),
  bone(id++"-leg","hips",[side*p.width*0.255,0.0,0.0]),bone(id++"-shin",id++"-leg",[0.0,-p.upper,0.0]),
  bone(id++"-foot",id++"-shin",[0.0,-p.lower,0.0]),bone(id++"-wing","chest",[side*0.075,0.10,-0.13])]]]

fn face(doc,p) array^ {
 let a=doc.appearance,h=p.head,d=h*0.87,plush=doc.form=="plush",robot=doc.form=="robot",spirit=doc.form=="genie"
 let skin=if (plush or spirit) a.body else a.skin,ink=if (plush) a.eyes else tint(a.eyes,0.55)^;
 [piece("head","head",if (robot) "rounded_box" else if (plush) "plush_head" else "human_head",[0.0,0.0,0.0],
   [h,h*(if (robot) 0.85 else 1.0),d],if (robot) a.body else skin,[0.0,0.0,0.0],{roughness:if (robot) 0.32 else 0.68,metalness:if (robot) 0.65 else 0.0}),
  *(if (robot) [piece("visor","head","rounded_box",[0.0,0.015,h*0.415],[h*0.89,h*0.48,0.025],"#17333b",[0.0,0.0,0.0],{roughness:0.22,metalness:0.25})] else []),
  *(if (plush) [piece("muzzle","head","round",[0.0,-h*0.16,d*0.39],[h*0.66,h*0.40,h*0.30],tint(skin,1.13)^)] else []),
  *[for (side in [-1.0,1.0],let id=if (side<0.0) "left" else "right") *[
   *(if (plush) [piece(id++"-eye","head","round",[side*h*0.22,h*0.065,d*0.455],[h*0.085,h*0.105,h*0.055],ink,[0.0,0.0,0.0],{roughness:0.18})]
    else if (robot) [piece(id++"-eye","head","rounded_box",[side*h*0.225,h*0.04,h*0.438],[h*0.22,h*0.12,0.014],a.eyes,[0.0,0.0,side*0.08],{emissive:a.eyes,'emissive-intensity':0.45,roughness:0.2})]
    else [piece(id++"-eye-white","head","round",[side*h*0.205,h*0.055,d*0.455],[h*0.175,h*0.115,h*0.045],"#f6eee1",[0.0,0.0,side*0.04]),
     piece(id++"-iris","head","round",[side*h*0.205,h*0.058,d*0.455+h*0.022],[h*0.073,h*0.082,h*0.026],a.eyes,[0.0,0.0,0.0],{roughness:0.20}),
     piece(id++"-pupil","head","round",[side*h*0.205,h*0.060,d*0.455+h*0.036],[h*0.038,h*0.050,h*0.012],"#101b24",[0.0,0.0,0.0],{roughness:0.14}),
     piece(id++"-brow","head","arch",[side*h*0.205,h*0.155,d*0.46],[h*0.21,h*0.12,h*0.12],if (spirit) a.hair else tint(a.hair,0.8)^)]),
   *(if (robot) [] else [piece(id++"-eye-glint","head","round",[side*h*0.205-h*0.012,h*0.081,d*0.455+h*0.043],
    [h*0.022,h*0.023,h*0.012],"#ffffff",[0.0,0.0,0.0],{roughness:0.05})]),
   *(if (not robot and doc.ear_shape=="none") []
    else if (plush) [for (inner in [false,true],let pointed=doc.ear_shape=="pointed",let long=doc.ear_shape=="long",
     let floppy=doc.ear_shape=="floppy",let size=doc.parameters.ears*(if (inner) 0.62 else 1.0))
     piece(id++(if (inner) "-inner-ear" else "-ear"),"head",if (pointed) "ear" else "round",
      [side*h*0.41,h*0.40+(if (long) doc.parameters.ears*0.4 else 0.0),if (inner) 0.034 else -h*0.015],
      [size*(if (pointed) side else if (long) 0.6 else 1.0),size*(if (long) 1.8 else if (floppy) 1.4 else 1.0),if (inner) 0.025 else 0.10],
      if (inner) tint(skin,0.73)^ else skin,[0.0,0.0,if (floppy) side*0.65 else 0.0])]
    else if (not robot) [piece(id++"-ear","head",if (doc.ear_shape=="pointed") "ear" else "round",
     [side*h*0.50,0.0,-h*0.025],[side*h*(if (doc.ear_shape=="pointed") 0.40 else 0.16),h*0.29,h*0.17]*(doc.parameters.ears/0.16),skin,[0.0,0.0,side*0.18])]
    else [piece(id++"-ear-bolt","head","round",[side*h*0.53,0.0,0.0],[h*0.12,h*0.27,h*0.27],a.accent,[0.0,0.0,0.0],{metalness:0.75,roughness:0.27})])]],
  *(if (robot) [piece("mouth","head","rounded_box",[0.0,-h*0.16,h*0.438],[h*0.25,h*0.035,0.012],tint(a.eyes,0.8)^)]
   else [piece("nose","head","round",[0.0,-h*0.04,d*(if (plush) 0.60 else 0.475)],
     [h*(if (plush) 0.18 else 0.11),h*0.13,h*(if (plush) 0.10 else 0.11)],if (plush) ink else skin),
    piece("smile","head","smile",[0.0,-h*(if (plush) 0.26 else 0.235),d*(if (plush) 0.55 else 0.454)],
     [h*0.31,h*0.20,h*0.16],if (plush) ink else tint(skin,0.55)^)]),
  *(if (spirit) [piece("goatee","head","ear",[0.0,-h*0.43,d*0.32],[h*0.32,-h*0.34,h*0.23],a.hair),
   *[for (side in [-1.0,1.0]) piece("moustache-"++string(side),"head","arch",[side*h*0.105,-h*0.145,d*0.48],
    [h*0.21,h*0.13,h*0.24],a.hair,[0.0,0.0,side*0.25])]] else [])]
}

fn hair(doc,p) array^ {
 let a=doc.appearance,h=p.head,d=h*0.87,style=if (doc.accessory=="wig") "waves" else doc.hair_style
 let paint=if (doc.accessory=="wig") "#ded9cc" else a.hair
 let length=if (style=="long" or style=="braid") 1.65 else if (style=="bob") 0.85 else 0.65;
 if (style=="none") [] else [
  piece("hair-cap","head","hair",[0.0,h*0.025,-d*0.015],[h*1.06,h*1.04,d*1.05],paint),
  *(if (contains(["long","bob","braid","waves"],style)) [piece("hair-back","head","hair_back",[0.0,-h*length*0.25,-d*0.33],
   [h*0.97,h*length,d*0.42],paint)] else []),
  *[for (side in [-1.0,1.0],i in 0 to (if (style=="waves") 1 else 0))
   piece("hair-lock-"++string(side)++"-"++string(i),"head","lock",[side*h*(0.43+float(i)*0.09),h*(0.32-length*0.5),-d*0.015],
    [h*0.48,h*(if (style=="short") 0.5 else length),d*0.4],paint,[0.0,0.0,side*0.06])],
  *(if (style=="bun") [piece("hair-bun","head","turban",[0.0,h*0.38,-d*0.34],[h*0.55,h*0.5,d*0.48],paint),
    piece("bun-ribbon","head","ring",[0.0,h*0.24,-d*0.34],[h*0.47,h*0.20,d*0.44],a.trim)] else []),
  *(if (style=="braid") [piece("braid","head","lock",[h*0.27,-h*0.65,-d*0.44],[h*0.5,h*1.7,d*0.5],paint),
    piece("braid-tie","head","ring",[h*0.27,-h*1.32,-d*0.44],[h*0.24,h*0.18,d*0.2],a.trim)] else [])]
}

fn clothes(doc,p) array^ {
 let a=doc.appearance,h=p.head,robot=doc.form=="robot",plush=doc.form=="plush",spirit=doc.form=="genie",fairy=doc.form=="fairy"
 let suit=doc.outfit=="suit";
 [*(if (doc.outfit=="dress" or doc.outfit=="hide") [piece("skirt","hips","dress",[0.0,-0.07,0.0],
   [p.width*(if (fairy) 1.75 else 1.60),if (fairy) 0.35 else if (doc.outfit=="hide") 0.32 else 0.46,p.depth*1.65],a.clothes),
   piece("waist-ribbon","hips","ring",[0.0,0.12,0.0],[p.width*0.97,0.09,p.depth*1.05],a.trim)] else []),
  *(if (suit and not robot) [piece("coat-tails","hips","coat",[0.0,-0.07,-0.01],[p.width*1.1,0.38,p.depth*1.05],a.clothes),
   *[for (side in [-1.0,1.0]) piece("lapel-"++string(side),"chest","ear",[side*p.width*0.13,0.11,p.depth*0.49],
    [side*p.width*0.34,0.18,0.025],tint(a.clothes,1.25)^,[0.0,0.0,side*0.18])],
   *[for (i in 0 to 2) piece("coat-button-"++string(i),"chest","round",[0.0,0.06-float(i)*0.085,p.depth*0.52],[0.018,0.018,0.01],a.trim)]] else []),
  *(if (plush and doc.outfit!="none") [piece("shirt-hem","hips","ring",[0.0,0.06,0.0],[p.width*1.02,0.12,p.depth*1.01],tint(a.clothes,0.85)^),
   piece("shirt-collar","chest","ring",[0.0,p.torso*0.46,0.0],[h*0.65,0.08,h*0.52],a.trim)] else []),
  *(if (spirit) [
   *[for (side in [-1.0,1.0],edge in [false,true])
    piece("vest-"++string(side)++(if (edge) "-edge" else ""),"chest",if (edge) "vest-edge" else "vest",[0.0,0.0,0.0],
     [side*p.width*1.04,p.torso*1.03,p.depth*1.04],if (edge) a.trim else a.clothes)],
   piece("sash","hips","pelvis",[0.0,0.025,0.0],[p.width*0.69,0.085,p.depth*0.91],a.clothes),
   piece("sash-edge","hips","ring",[0.0,0.04,0.0],[p.width*0.7,0.10,p.depth*0.9],a.trim,[0.0,0.0,0.0],{metalness:0.6,roughness:0.28}),
   piece("sash-jewel","hips","round",[0.0,0.02,p.depth*0.47],[0.05,0.06,0.025],a.eyes,[0.0,0.0,0.0],{metalness:0.25,roughness:0.18})] else []),
  *(if (robot) [piece("chest-panel","chest","rounded_box",[0.0,0.02,p.depth*0.41],[p.width*0.64,p.torso*0.59,0.045],a.accent,[0.0,0.0,0.0],{metalness:0.6,roughness:0.32}),
   piece("core-housing","chest","ring",[0.0,0.07,p.depth*0.53],[0.105,0.04,0.105],a.trim,[math.pi/2.0,0.0,0.0],{metalness:0.8,roughness:0.22}),
   piece("core-light","chest","round",[0.0,0.07,p.depth*0.54],[0.067,0.067,0.025],a.eyes,[0.0,0.0,0.0],{emissive:a.eyes,'emissive-intensity':0.35,roughness:0.18}),
   *[for (i in -1 to 1) piece("vent-"++string(i),"chest","rounded_box",[float(i)*0.043,-0.075,p.depth*0.525],[0.018,0.055,0.015],"#152a30")]] else []),
  *(if (doc.marking!="plain") [for (i in 0 to 2,let x=(float(i)-1.0)*p.width*0.18)
   piece("cloth-mark-"++string(i),"chest",if (doc.marking=="spots") "round" else "rounded_box",
    [x,-0.04+float(i%2)*0.04,p.depth*0.45],[0.038,if (doc.marking=="spots") 0.045 else 0.015,0.012],tint(a.clothes,0.72)^)] else [])]
}

fn extras(doc,p) array^ {
 let a=doc.appearance,h=p.head,d=h*0.87;
 [*(if (doc.accessory=="cap") [piece("turban","head","turban",[0.0,h*0.47,-0.005],[h*1.18,h*0.61,d*1.20],a.clothes),
   piece("turban-fold","head","ring",[0.0,h*0.34,0.0],[h*1.08,h*0.32,d*1.10],tint(a.clothes,1.18)^,[0.0,0.0,0.09]),
   piece("turban-jewel","head","round",[0.0,h*0.48,d*0.60],[h*0.16,h*0.20,h*0.085],a.trim,[0.0,0.0,0.0],{roughness:0.23,metalness:0.65}),
   piece("turban-feather","head","feather",[h*0.10,h*0.67,0.0],[h*0.55,h*0.15,h*0.18],"#eee5d0",[0.0,0.0,1.05])] else []),
  *(if (doc.accessory=="hood") [piece("hood","head","hood",[0.0,0.035,-0.025],[h*1.26,h*1.23,d*1.20],a.clothes),
   piece("cape","chest","coat",[0.0,-0.10,-p.depth*0.43],[p.width*1.35,0.62,0.09],tint(a.clothes,0.87)^),
   piece("cloak-clasp","chest","round",[0.0,0.14,p.depth*0.50],[0.034,0.034,0.014],a.trim)] else []),
  *(if (doc.accessory=="crown") [piece("circlet","head","ring",[0.0,h*0.40,0.0],[h*0.78,h*0.27,d*0.79],a.trim,[0.0,0.0,0.0],{roughness:0.25,metalness:0.7}),
   *[for (i in 0 to 4,let q=float(i)/5.0*2.0*math.pi)
    piece("crown-"++string(i),"head","ear",[cos(q)*h*0.35,h*0.48,sin(q)*d*0.35],[h*0.20,h*0.22,h*0.10],a.trim,[0.0,0.0-q,0.0],{roughness:0.25,metalness:0.7})]] else []),
  *(if (doc.accessory=="antenna") [piece("antenna","head","forearm",[0.0,h*0.75,0.0],[0.024,h*0.32,0.024],a.accent),
   piece("antenna-tip","head","round",[0.0,h*0.77,0.0],[0.054,0.054,0.054],a.eyes,[0.0,0.0,0.0],{emissive:a.eyes,'emissive-intensity':0.4})] else []),
  *(if (doc.form=="plush" and doc.parameters.tail>0.0) [piece("tail","hips","round",[0.0,0.05,-p.depth*0.48],[0.10,0.10,0.11+doc.parameters.tail*0.15],a.body)] else []),
  *(if (doc.form=="genie") [piece("spirit-tail","hips","spirit",[0.0,0.03,0.0],
   [p.width*1.45,0.85+doc.parameters.tail,p.depth*1.7],a.body,[0.0,0.0,0.0],{roughness:0.45})] else []),
  *(if (doc.form=="fairy") [for (side in [-1.0,1.0],row in [0,1],let id=if (side<0) "left" else "right",
   let spread=doc.parameters.wing_span*(if (row==0) 0.78 else 0.67)) *[
   piece(id++"-wing-"++string(row),id++"-wing",if (row==0) "wing-upper" else "wing-lower",[0.0,0.0,-0.005*row],
    [side*spread,spread,1.0],if (row==0) "#dcebdc" else "#c4dcca",[0.0,0.0,0.0],{roughness:0.38,metalness:0.15,opacity:0.64,transparent:true}),
   piece(id++"-vein-"++string(row),id++"-wing","vein",[0.0,0.0,0.015],[side*spread,spread*(if (row==0) 1.0 else -0.55),1.0],a.trim,
    [0.0,0.0,0.0],{roughness:0.4,metalness:0.3})]] else [])]
}

fn anatomy(doc,p) array^ {
 let a=doc.appearance,robot=doc.form=="robot",plush=doc.form=="plush",spirit=doc.form=="genie"
 let skin=if (plush or spirit or robot) a.body else a.skin
 let coat=if (doc.outfit=="none" or spirit) skin else a.clothes
 let limb_skin=if (robot) a.clothes else skin;
 [piece("torso","chest",if (robot) "rounded_box" else if (plush) "plush_body" else if (spirit) "spirit_body" else "torso",
   [0.0,0.0,0.0],[p.width,p.torso,p.depth],coat,[0.0,0.0,0.0],{roughness:if (robot) 0.36 else 0.9,metalness:if (robot) 0.65 else 0.0}),
  *(if (spirit) [] else [piece("pelvis","hips","pelvis",[0.0,0.04,0.0],[p.width*0.88,0.18,p.depth*0.91],
   if (plush) skin else tint(a.clothes,0.88)^)]),
  *(if (plush) [] else [piece("neck","chest","forearm",[0.0,p.head_y-p.head*0.30,0.0],[p.head*0.30,0.10,p.head*0.29],skin)]),
  *face(doc,p)^,*hair(doc,p)^,*clothes(doc,p)^,*extras(doc,p)^,
  *[for (side in [-1.0,1.0],let id=if (side<0) "left" else "right") *[
   piece(id++"-upper-arm",id++"-arm",if (robot) "rounded_box" else "limb",[0.0,if (robot) -p.arm/2.0 else 0.0,0.0],
    [p.arm_width,p.arm,p.arm_width],if (doc.outfit=="none" or spirit) skin else a.clothes),
   piece(id++"-elbow",id++"-forearm","round",[0.0,0.0,0.0],[p.arm_width*0.76,p.arm_width*0.76,p.arm_width*0.76],if (robot) a.accent else skin),
   piece(id++"-forearm",id++"-forearm","forearm",[0.0,0.0,0.0],[p.arm_width*0.84,p.fore,p.arm_width*0.84],limb_skin),
   piece(id++"-hand",id++"-hand",if (robot) "rounded_box" else "hand",[0.0,-0.045,0.015],[p.arm_width*0.78,0.11,p.arm_width*0.55],limb_skin),
   piece(id++"-thumb",id++"-hand","round",[0.0-side*p.arm_width*0.33,-0.035,0.028],[p.arm_width*0.32,0.065,p.arm_width*0.34],limb_skin,[0.0,0.0,side*0.35]),
   *(if (robot or spirit) [piece(id++"-cuff",id++"-forearm","ring",[0.0,-p.fore*0.87,0.0],[p.arm_width*0.86,0.09,p.arm_width*0.86],
    if (spirit) a.trim else a.accent,[0.0,0.0,0.0],{roughness:0.25,metalness:0.65})] else []),
   *(if (spirit) [] else [
    piece(id++"-thigh",id++"-leg","limb",[0.0,0.0,0.0],[p.leg_width,p.upper,p.leg_width*1.06],if (plush or doc.outfit=="hide" or doc.outfit=="dress") skin else tint(a.clothes,0.9)^),
    piece(id++"-knee",id++"-shin","round",[0.0,0.0,0.0],[p.leg_width*0.74,p.leg_width*0.74,p.leg_width*0.74],
     if (plush or doc.outfit=="hide" or doc.outfit=="dress") skin else tint(a.clothes,0.9)^),
    piece(id++"-calf",id++"-shin","forearm",[0.0,0.0,0.0],[p.leg_width*0.86,p.lower,p.leg_width*0.88],if (plush or doc.outfit=="hide" or doc.outfit=="dress") skin else tint(a.clothes,0.9)^),
    piece(id++"-foot",id++"-foot","boot",[0.0,-0.015,0.045],[p.leg_width*1.12,0.08,0.21],if (plush) a.body else a.accent),
    piece(id++"-sole",id++"-foot","rounded_box",[0.0,-0.047,0.045],[p.leg_width*1.10,0.025,0.205],tint(if (plush) a.body else a.accent,0.72)^),
    *(if (plush) [piece(id++"-paw-pad",id++"-foot","round",[0.0,-0.006,0.145],[0.08,0.045,0.025],tint(a.body,0.75)^)] else [])])]]]
}
fn origin(joints,id) array => if (id==null) [0.0,0.0,0.0] else
 (let j=[for (j in joints where j.id==id) j][0],j.position+origin(joints,j.parent))
pub fn generate(doc) map^ {
 let p=plan(doc),joints=rig(doc,p),parts=anatomy(doc,p)^
 let resources=geometry.build(unique(parts |> ~.shape))^
 let bounds=map([for (g in resources,let points=[for (i in 0 to (len(g.positions) div 3-1)) slice(g.positions,i*3,i*3+3)])
  (g.id,{lo:[for (axis in 0 to 2) min(points |> ~[axis])],hi:[for (axis in 0 to 2) max(points |> ~[axis])]})])
 let corners=[for (p in parts,let b=bounds[p.geometry],let matrix=t.matrix(p.position,p.rotation,p.scale)^)
  for (x in [b.lo[0],b.hi[0]],y in [b.lo[1],b.hi[1]],z in [b.lo[2],b.hi[2]]) origin(joints,p.joint)+t.point(matrix,[x,y,z])^]
 let lo=[for (axis in 0 to 2) min(corners |> ~[axis])],hi=[for (axis in 0 to 2) max(corners |> ~[axis])];
 {document:doc,joints:joints,parts:parts,resources:resources,plan:p,lo:lo,hi:hi,scale:doc.parameters.height/(hi[1]-lo[1])}
}
