// PG13.5 / S12.1.1v3: reproductive organs share stable branch attachments, independent of leaves.
import c: ._common
import rng: .rng
import plant: .plant
import primitive: .primitive
import shapes: lambda.scene3d.shape
import s: lambda.scene3d
import t: lambda.scene3d.transform
import v: lambda.scene3d.camera
import color: lambda.color

pub let FLOWERS=["none","blossom","bell","catkin"]
pub let FRUITS=["none","apple","berry","raspberry","strawberry","cone","nut"]
let DEFAULTS={flower:"blossom",fruit:"apple",count:2,flower_size:0.07,fruit_size:0.10,petals:5,
 flower_color:"#f7dce5",center_color:"#e1ba57",fruit_color:"#bb4435",stem_color:"#627a42"}
pub fn normalize(raw={}) map^ {
 let fields=c.require(raw is map and c.fields(raw,[for (k,val in DEFAULTS) string(k)]),"unknown plant organ setting")^
 let p={*:DEFAULTS,*:raw}
 let valid=c.require(contains(FLOWERS,p.flower) and contains(FRUITS,p.fruit) and c.bounded(p.count,0,8,true) and
  c.bounded(p.flower_size,0.002,1.0) and c.bounded(p.fruit_size,0.002,1.0) and c.bounded(p.petals,3,32,true),"invalid plant organs")^
 let paints=[for (paint in [p.flower_color,p.center_color,p.fruit_color,p.stem_color]) color.parse(paint)^];
 p
}
// merge small ellipsoids once per organ mesh, rather than instancing every scale or drupelet.
fn ellipsoid(buffer,position,scale) map => {*:buffer,
 positions:[for (i in 0 to (len(buffer.positions) div 3-1)) *(slice(buffer.positions,i*3,i*3+3)*scale+position)],
 normals:[for (i in 0 to (len(buffer.normals) div 3-1)) *v.unit(slice(buffer.normals,i*3,i*3+3)/scale)]}
fn fruit_buffer(kind) map {
 let sphere=shapes.sphere(6,10)
 if (kind=="cone" or kind=="raspberry") {
  let cone=kind=="cone";
  primitive.merge([for (row in 0 to 4,i in 0 to 7,
   let y=float(row)/4.0,let radius=(if (cone) 0.34 else 0.31)*sin((0.15+y*0.75)*math.pi),
   let angle=float(i)*math.pi/4.0+float(row)*2.399963)
   ellipsoid(sphere,[cos(angle)*radius,y-0.5,sin(angle)*radius],if (cone) [0.35,0.27,0.35] else [0.27,0.24,0.27])])
 } else if (kind=="strawberry") primitive.sweep([[0.0,-0.6,0.0],[0.0,-0.35,0.0],[0.0,0.0,0.0],[0.0,0.3,0.0],[0.0,0.4,0.0]],
  [0.012,0.20,0.40,0.38,0.08],16,true)
 else sphere
}
fn geometry(id,buffer:map) element^ => s.geometry(buffer.positions,{id:id,normals:buffer.normals,uvs:buffer.uvs,indices:buffer.indices})
fn piece(id,shape,position,scale,paint,role,organ,rotation=[0.0,0.0,0.0]) map =>
 {id:id,geometry:shape,shape:shape,matrix:t.matrix(position,rotation,scale)^,color:paint,role:role,organ:organ,opacity:1.0,kind:"tree"}
fn flowers(id,position,p,phase) array {
 let size=p.flower_size
 if (p.flower=="none") [] else if (p.flower=="catkin" or p.flower=="bell")
  [piece(id++"/flower","plant-organ-round",position-[0.0,size*0.5,0.0],
   if (p.flower=="catkin") [size*0.35,size*2.8,size*0.35] else [size,size*1.3,size],p.flower_color,"petal","flower")]
 else [*[for (i in 0 to (p.petals-1),let angle=float(i)*2.0*math.pi/float(p.petals)+phase)
  piece(id++"/petal-"++string(i),"plant-organ-blade",position,[size,size*0.65,size*0.52],p.flower_color,"petal","flower",[0.0,-angle,0.12])],
  piece(id++"/center","plant-organ-round",position+[0.0,size*0.06,0.0],[size*0.4,size*0.16,size*0.4],p.center_color,"center","flower")]
}
fn fruits(id,position,p) array {
 let size=p.fruit_size,center=position-[0.0,size*0.75,0.0]
 let role=if (contains(["cone","nut"],p.fruit)) p.fruit else "fruit";
 if (p.fruit=="none") [] else [
  piece(id++"/fruit-stem","plant-organ-round",position-[0.0,size*0.2,0.0],[size*0.07,size*0.4,size*0.07],p.stem_color,"stem","fruit"),
  piece(id++"/"++p.fruit,"plant-organ-"++p.fruit,center,
   if (p.fruit=="cone") [size,size*1.8,size] else [size,size,size],p.fruit_color,role,"fruit"),
  *(if (p.fruit=="strawberry") [for (i in 0 to 4) piece(id++"/calyx-"++string(i),"plant-organ-blade",center+[0.0,size*0.36,0.0],
   [size*0.4,size*0.2,size*0.12],p.stem_color,"sepal","fruit",[0.0,float(i)*2.0*math.pi/5.0,0.0])] else [])]
}
pub fn decorate(model,k:u64,parameters={}) map^ {
 let p=normalize(parameters)^
 let branches=model.skeleton.branches
 let skeleton=c.require(branches is array and len(branches)>0,"plant organs need a branch skeleton")^
 let parents=branches |> ~.parent
 let terminal=[for (b in branches where not contains(parents,b.id)) b]
 let budget=c.require(len(terminal)*p.count<=256,"plant organ attachment budget exceeded")^
 let anchors=[for (b in terminal,i in 0 to (p.count-1),let id=b.id++"/reproductive-"++string(i),
  let key=rng.derive(k,id),let at=plant.sample(b,0.4+0.5*(float(i)+0.5)/float(p.count)),
  let position=at.frame.position+at.frame.l*at.radius)
  {id:id,parent:b.id,position:position,phase:rng.unit(key)*2.0*math.pi}]
 let parts=[for (anchor in anchors) *[*flowers(anchor.id,anchor.position,p,anchor.phase),*fruits(anchor.id,anchor.position,p)]]
 let resources=[geometry("plant-organ-round",shapes.sphere(6,10))^,geometry("plant-organ-blade",primitive.blade(4,4))^,
  *(if (p.fruit=="none") [] else [geometry("plant-organ-"++p.fruit,fruit_buffer(p.fruit))^])];
 {*:model,resources:[*model.resources,*resources],parts:[*model.parts,*parts],organs:{settings:p,anchors:anchors}}
}
