// PG17 / S12.1.1v3: authored poses animate joints; they never advance ecological time.
import s: lambda.scene3d
import t: lambda.scene3d.transform
import rng: .rng

fn rotation(model,id,action,phase) array {
 let side=if (starts_with(id,"left")) -1.0 else 1.0,wave=sin(phase*2.0*math.pi),
  traveling=contains(["walk","run"],action),stride=if (action=="run") 0.72 else 0.42,
  thigh=if (action=="sit") -math.pi/2.0 else if (traveling) side*wave*stride else 0.0,
  knee=if (action=="sit") math.pi/2.0 else if (traveling) max(0.0,side*wave)*(if (action=="run") 1.05 else 0.68) else 0.0;
 if (ends_with(id,"-wing")) [0.0,side*(0.20+sin(phase*4.0*math.pi)*0.46),side*0.07]
 else if (ends_with(id,"-leg")) [thigh,0.0,0.0]
 else if (ends_with(id,"-shin")) [knee,0.0,0.0]
 else if (ends_with(id,"-foot")) [if (action=="sit") 0.0 else 0.0-(thigh+knee)*0.78,0.0,0.0]
 else if (ends_with(id,"-hand")) [0.0,if (action=="wave" and side>0) wave*0.32 else 0.0,side*0.04]
 else if (ends_with(id,"-forearm"))
  if (action=="wave" and side>0) [-0.18,wave*0.08,0.58]
  else [if (action=="sit") -0.85 else if (traveling) -0.15-max(0.0,0.0-side*wave)*0.22 else -0.10,0.0,side*0.02]
 else if (ends_with(id,"-arm"))
  if (action=="wave" and side>0) [-0.22,0.0,2.0+wave*0.08]
  else [if (traveling) 0.0-side*wave*0.38 else if (action=="sit") -0.20 else if (action=="play") -0.3+side*wave*0.4 else wave*0.025,
   0.0,side*(if (action=="play") 0.6+wave*0.2 else if (model.document.form=="genie") 0.19 else 0.08)]
 else if (id=="chest") [if (action=="run") 0.12 else 0.02,if (traveling) 0.0-wave*0.07 else wave*0.018,wave*0.01]
 else if (id=="hips") [0.0,if (traveling) wave*0.06 else 0.0,if (traveling) wave*0.025 else 0.0]
 else if (id=="head") [0.0,if (action=="wave") 0.08+wave*0.025 else wave*0.035,0.0]
 else [0.0,0.0,0.0]
}
fn quaternion(angles) array^ => t.quaternion_multiply(t.quaternion_multiply(
 t.quaternion_axis_angle([1.0,0.0,0.0],angles[0]),t.quaternion_axis_angle([0.0,1.0,0.0],angles[1]))^,
 t.quaternion_axis_angle([0.0,0.0,1.0],angles[2]))^
fn position(model,j,action,phase) array => j.position+(if (j.id!="hips") [0.0,0.0,0.0] else
 [0.0,if (model.document.form=="genie") 0.025+sin(phase*2.0*math.pi)*0.017 else if (action=="sit") 0.0-model.plan.upper
  else if (action=="play") abs(sin(phase*2.0*math.pi))*0.07 else if (contains(["walk","run"],action))
   abs(sin(phase*2.0*math.pi))*(if (action=="run") 0.03 else 0.012) else sin(phase*2.0*math.pi)*0.002,0.0])
fn joint_node(model,j,prefix,action,phase) element^ => s.group([
 *[for (p in model.parts where p.joint==j.id) <mesh id:prefix++"-part-"++p.id,geometry:p.geometry,position:p.position,rotation:p.rotation,scale:p.scale,
  'data-avatar-part':p.id,'cast-shadow':true,'receive-shadow':true,
  s.material('standard',{color:p.color,side:'double',opacity:p.opacity,transparent:p.opacity<1.0,*:p.surface})>],
 *[for (child in model.joints where child.parent==j.id) joint_node(model,child,prefix,action,phase)^]],
 {id:prefix++"-"++j.id,position:position(model,j,action,phase),quaternion:quaternion(rotation(model,j.id,action,phase))^})

pub fn nodes(model,action,playing,prefix,phase,pace) array^ {
 let duration=(if (action=="run") 0.62 else if (action=="walk") 1.15 else 2.8)/pace
 let times=[for (i in 0 to 32) float(i)/32.0*duration]
 let blink=0.71+rng.unit(rng.derive(rng.key(model.document.seed),["blink",prefix]))*0.10
 let eyes=[for (p in model.parts where model.document.form!="plush" and
  (ends_with(p.id,"-eye-white") or ends_with(p.id,"-eye-glint") or ends_with(p.id,"-iris") or ends_with(p.id,"-pupil"))) p];
 [s.group([for (j in model.joints where j.parent==null) joint_node(model,j,prefix,action,phase)^],
  {id:prefix,scale:[model.scale,model.scale,model.scale],position:[0.0,0.0-model.lo[1]*model.scale,0.0],
   'data-avatar-form':model.document.form,'data-avatar-action':action,'data-avatar-generator':2}),
  *(if (not playing) [] else [s.clip(prefix++"-pose",[
   *[for (j in model.joints) s.track(prefix++"-"++j.id++".quaternion",times,
    [for (i in 0 to 32) *quaternion(rotation(model,j.id,action,phase+float(i)/32.0))^],'quaternion')],
   s.track(prefix++"-hips.position",times,[for (i in 0 to 32) *position(model,model.joints[0],action,phase+float(i)/32.0)],'vector'),
   *[for (p in eyes) s.track(prefix++"-part-"++p.id++".scale",[0.0,blink*duration,(blink+0.035)*duration,(blink+0.075)*duration,duration],
    [for (q in [1.0,1.0,0.06,1.0,1.0]) *[p.scale[0],p.scale[1]*q,p.scale[2]]],'vector')]],{duration:duration,autoplay:true})])]
}
