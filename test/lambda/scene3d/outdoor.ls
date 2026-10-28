import s: lambda.scene3d
import o: lambda.scene3d.outdoor
fn stage(children) element => s.scene([s.camera(),*children])
fn bad(children) bool => (s.validate(stage(children)) ^ {false}) == false
fn export_error(material) => format(stage([s.mesh(s.box(),material)]),'gltf') ^ {^.message}
let leaf=o.foliage({id:"leaf",'wind-strength':0.2})
let water=o.water(0.4,{id:"water"});
[
 s.validate(stage([o.fog('linear',{near:10.0,far:100.0})]))^,
 s.validate(stage([o.fog('exp2',{density:0.01})]))^,
 s.validate(stage([o.fog('height',{height:3.0,falloff:0.2})]))^,
 s.validate(stage([leaf,o.wind("leaf")^]))^,
 s.validate(stage([water,o.flow("water")^]))^,
 water.metalness==0.0 and water.ior==1.333 and water.thickness==0.4,
 bad([o.fog('linear',{near:20.0,far:10.0})]),
 bad([o.fog('unknown')]),
 bad([o.fog('exp2',{density:-0.1})]),
 bad([o.fog('height',{falloff:-0.1})]),
 bad([o.fog(),o.fog()]),
 bad([s.group([o.fog()])]),
 bad([o.foliage({foliage:1.1})]),
 bad([o.foliage({'wind-height':0.0})]),
 bad([o.foliage({'wind-strength':-1.0})]),
 bad([o.water(0.4,{'water-amplitude':-1.0})]),
 bad([s.material('basic',{foliage:0.5})]),
 bad([o.water(-0.1)]),
 s.validate(stage([s.light('directional',{'cast-shadow':true,'shadow-cascades':3,'shadow-distance':200.0})]))^,
 bad([s.light('point',{'shadow-cascades':2})]),
 bad([s.light('directional',{'shadow-cascades':5})]),
 bad([s.light('directional',{'shadow-transition':0.3})]),
 bad([s.light('directional',{'cast-shadow':true,'shadow-cascades':3}),s.light('spot',{'cast-shadow':true}),s.light('point',{'cast-shadow':true})]),
 contains(export_error(leaf),"outdoor material effects"),
 contains(export_error(water),"outdoor material effects"),
 (o.wind("leaf",0.0) ^ {false})==false,
 (o.flow("bad.id") ^ {false})==false,
 bad([s.shader_material("void main(){}","void main(){}",{}, {'wind-strength':1.0})])
]
