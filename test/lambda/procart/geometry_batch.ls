// S1.6 / PG12.9: batch transforms and distance samples equal their scalar oracles.
import g: lambda.chart.geometry
import t: lambda.scene3d.transform
import s: lambda.scene3d
import c: lambda.procart.scene
let ring=[[-2.0,-1.0],[2.0,-1.0],[1.0,2.0],[0.0,0.0],[-2.0,1.0],[-2.0,1.0]]
let points=[for (i in 0 to 79) [sin(float(i))*3.0,cos(float(i)*1.7)*2.0]]
let geometry=s.geometry([0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0,1.0],
 {id:"tri",normals:[0.0,-0.7,0.7,0.0,-0.7,0.7,0.0,-0.7,0.7],indices:[0,1,2]})
let matrices=[for (i in 1 to 12) t.matrix([float(i),-2.0,3.0],[float(i)*0.03,-0.7,0.9],[-2.0,1.3,0.4])^]
let models=[for (m in matrices) {resources:[geometry],parts:[{id:"part",geometry:"tri",matrix:m,color:"#775533",opacity:1.0,kind:"tree",role:"bark",surface:null}]}];
[math.polygon_distances(points,ring)^==g.distances_reference(points,ring),g.distances([],ring)==[],
 math.polygon_distances([[2.0,3.0]],[[0.0,0.0]])^==[sqrt(13.0)],
 all([for (m in models) c.compact(m,"test")^==c.compact_reference(m,"test")^]),
 math.transform_buffer(matrices[0],[],[])^==[[],[]],
 (math.transform_buffer([1],[],[])^ {true})==true,(math.transform_buffer(matrices[0],[1,2,3],[])^ {true})==true,
 (math.polygon_distances(points,[])^ {true})==true]
