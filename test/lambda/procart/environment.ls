// solar coordinates, bounded saved data and rooted motion are package contracts (PG15).
import E: lambda.procart.environment
import H: lambda.procart.herb
import R: lambda.procart.rng
import C: lambda.procart._common
import T: lambda.scene3d.transform
import S: lambda.scene3d
import V: lambda.scene3d.camera
import A: lambda.procart.scene

let noon=E.sample({latitude:0.0,day:81,time:12.0})^
let morning=E.sample({latitude:0.0,day:81,time:8.0})^
let evening=E.sample({latitude:0.0,day:81,time:16.0})^
let night=E.sample({latitude:45.0,time:0.0})^
let polar_summer=E.sample({latitude:89.0,day:172,time:0.0})^
let polar_winter=E.sample({latitude:89.0,day:355,time:12.0})^;
[noon.sun.elevation>89.0,morning.sun.direction[0]>0.8,evening.sun.direction[0]<-0.8,
 night.sun.daylight==0.0,night.lighting.sun==0.0,polar_summer.sun.elevation>20.0,polar_winter.sun.elevation<0.0,
 abs(V.dot(noon.sun.direction,noon.sun.direction)-1.0)<1e-12,
 E.normalize(<environment time:8.0>)^==E.normalize({time:8.0})^];

let day=E.sample({time:12.0,humidity:0.8})^
let rain=E.sample({time:12.0,weather:"rainy"})^
let dry=E.sample({humidity:0.1})^
let wet=E.sample({humidity:0.9})^
let high=E.sample({altitude:3000.0})^
let sea=E.sample({altitude:0.0})^;
[E.response("water-lily",day).openness==1.0,E.response("water-lily",night).openness==0.0,
 E.response("water-lily",rain).openness<1.0,E.response("sunflower",morning).direction[0]>0.8,
 E.response("sunflower",evening).direction[0]<-0.8,E.response("daisy",night).openness<E.response("daisy",day).openness,
 E.response("poppy",dry).droop>E.response("poppy",wet).droop,high.lighting.sun>sea.lighting.sun,
 E.sample({lighting:{sun:0.0}})^.lighting.sun==0.0,
 len(unique([for (weather in E.WEATHER) E.sample({weather:weather})^.background]))>=3];

let wind=E.sample({weather:"windy",wind_direction:90.0})^
let q=E.wind(wind,"same-plant",0.0)
let later=E.wind(wind,"same-plant",1.0)
let loop=E.wind(wind,"same-plant",8.0)
let pivot=[2.0,0.5,-3.0]
let planted=T.compose(pivot,q,[1.0,1.0,1.0])
let rotated=T.matrix([1.0,2.0,3.0],[0.0,1.3,0.0],[1.0,1.0,1.0])^
let head=T.multiply(rotated,E.head(morning.sun.direction,rotated)^)^
let up=V.unit([head[4],head[5],head[6]]);
[sum([for (i in 0 to 3) abs(q[i]-loop[i])])<1e-12,q!=later,abs(sum(q*q)-1.0)<1e-12,
 E.wind(E.sample({wind_speed:0.0})^,"same-plant",1.0)==[0.0,0.0,0.0,1.0],
 T.point(planted,[0.0,0.0,0.0])^==pivot,V.dot(up,morning.sun.direction)>0.999999,
 E.wind(wind,"another-plant",0.0)!=q,
 E.response("tree",wind).flexibility<E.response("grass",wind).flexibility];

fn rejected(raw) bool => (E.normalize(raw) ^ { null })==null;
[rejected({latitude:91}),rejected({altitude:10000}),rejected({time:25}),rejected({day:1.5}),
 rejected({day:0}),rejected({weather:"snowy"}),rejected({humidity:1.1}),rejected({wind_speed:-1}),
 rejected({wind_direction:361}),rejected({lighting:{sun:5}}),rejected({lighting:{moon:1}}),
 rejected({lattitude:45}),rejected(<environment <flower>>),rejected({humidity:null})];

let source=<procart version:2,seed:42,<resources
    <recipe id:"sun",kind:"flower",generator:"plant.herb",version:1,parameters:{species:"sunflower"}>>;
    <environment time:8.0,weather:"windy">;<flower id:"gold",recipe:"sun",position:[2.0,0.0,0.0]>>
let model=A.expand(source,H.registry())^
let rendered=A.to_scene3d(model)
let roundtrip=parse(format(source,'mark')^,'mark')^
fn reject_document(doc) bool => (A.normalize(doc,H.registry()) ^ { null })==null;
[S.validate(rendered)^,model.environment.config.time==8.0,
 A.expand(roundtrip,H.registry())^==model,
 len([for (n in content(content(rendered)[5]) where name(n)=='animation-clip') n])==1,
 reject_document(<procart version:2,seed:1,<resources>;<environment>;<environment>;<box>>),
 reject_document(<procart version:2,seed:1,<resources>;<group <environment>>>),
 (H.normalize({species:"unknown"}) ^ { null })==null]
