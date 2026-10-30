// S1.6 / S12.1.1v3: buffer and upright batch transforms retain scalar results.
import t: lambda.scene3d.transform
let matrix=t.matrix([1.0,-2.0,3.0],[0.2,-0.3,0.7],[-2.0,1.2,0.4])^
let points=[0.0,0.0,0.0,1.0,-2.0,3.0,-4.0,5.0,6.0]
let poses=[[0.0,1.0,2.0],[-3.0,4.0,-5.0]]
let angles=[0.0,4.3];
[t.points(matrix,points)^==[for (i in 0 to 2) *t.point(matrix,slice(points,i*3,i*3+3))^],
 t.points(matrix,[])^==[],t.scatter(poses,angles,1.3)^==[for (i,p in poses) t.matrix(p,[0.0,angles[i],0.0],[1.3,1.3,1.3])^],
 t.scatter(poses,angles,-1.3)^==[for (i,p in poses) t.matrix(p,[0.0,angles[i],0.0],[-1.3,-1.3,-1.3])^],
 t.scatter([],[],1.0)^==[],(t.points(matrix,[1,2])^ {true})==true,(t.scatter(poses,angles,0.0)^ {true})==true]

let lo=[-1.0,-2.0,0.0],hi=[3.0,1.0,2.0]
let corners=[for (x in [lo[0],hi[0]],y in [lo[1],hi[1]],z in [lo[2],hi[2]]) t.point(matrix,[x,y,z])^];
t.bounds(matrix,lo,hi)^=={lo:[for (r in 0 to 2) min(corners |> ~[r])],hi:[for (r in 0 to 2) max(corners |> ~[r])]}
