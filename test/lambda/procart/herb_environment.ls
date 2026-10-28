// flower anatomy is seeded independently of environment, including valid organ coordinates (PG15).
import H: lambda.procart.herb
import E: lambda.procart.environment
import R: lambda.procart.rng
import T: lambda.scene3d.transform
import V: lambda.scene3d.camera
import S: lambda.scene3d
import D: lambda.scene3d.dom
let day=E.sample({time:12.0})^
let night=E.sample({time:0.0})^
let key=R.key(2026)
let lily_day=H.generate(key,{species:"water-lily"},day)^
let lily_night=H.generate(key,{species:"water-lily"},night)^
fn organs(model,role) array => [for (p in model.parts where p.role==role) p];
[len(lily_day.parts)==len(lily_night.parts),lily_day.resources==lily_night.resources,
 organs(lily_day,"leaf")==organs(lily_night,"leaf"),organs(lily_day,"petal")!=organs(lily_night,"petal"),
 lily_day.response.openness==1.0,lily_night.response.openness==0.0,
 H.generate(key,{species:"water-lily"},day)^==lily_day];

let am=H.generate(key,{species:"sunflower"},E.sample({time:8.0})^)^
let pm=H.generate(key,{species:"sunflower"},E.sample({time:16.0})^)^
let petals=H.resources()[1]
let tri=slice(petals.indices,0,3)
let points=[for (i in tri) slice(petals.positions,i*3,i*3+3)]
// the first row's corner is collapsed; test a nondegenerate triangle inside the petal.
let middle=slice(petals.indices,30,33)
let q=[for (i in middle) slice(petals.positions,i*3,i*3+3)]
let normal=V.cross(V.subtract(q[1],q[0]),V.subtract(q[2],q[0]));
[organs(am,"stem")==organs(pm,"stem"),organs(am,"leaf")==organs(pm,"leaf"),organs(am,"petal")!=organs(pm,"petal"),
 len(petals.uvs)==len(petals.positions) div 3*2,min(petals.uvs)>=0.0,max(petals.uvs)<=1.0,normal[1]>0.0,
 all([for (species in H.SPECIES,let m=H.generate(key,{species:species},day)^)
    len(m.parts)>0 and all([for (p in m.parts) T.affine(p.matrix)])]),
 (H.normalize({size:0.0}) ^ { null })==null,(H.normalize({height:5.0}) ^ { null })==null,
 parse(D.attribute([[1.0,2.0],[3.0,4.0]]),'json')^==[1.0,2.0,3.0,4.0],
 parse(D.attribute([1.0,2.0,3.0]),'json')^==[1.0,2.0,3.0],
 parse(D.attribute(0.123456789123456),'json')^==0.123456789123456,
 parse(D.attribute(parse("[1.23456789,2.5]",'json')^),'json')^==[1.23456789,2.5],
 parse(D.attribute(parse("[[1.25,2],[3,4.5]]",'json')^),'json')^==[1.25,2,3,4.5]]
