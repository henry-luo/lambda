// cardinal turns, taper and bracket restoration are independent turtle oracles (PG13.3).
import turtle: lambda.procart.turtle3d
import plant: lambda.procart.plant
import scene: lambda.procart.scene
import native: lambda.scene3d
import v: lambda.scene3d.camera
fn near(a,b) bool => all([for (i in 0 to 2) abs(a[i]-b[i])<1e-10])
let trace=turtle.skeleton([["F",2.0,0.04],"[",["!",0.02],"+",["F",1.0,0.01],["L",0.4,0.2],"]","F"],{angle:90.0,radius:0.1})^;
[len(trace.branches)==3,len(trace.leaves)==1,near(trace.branches[0].points[1],[0,2,0]),
 near(trace.branches[1].points[1],[-1,2,0]),near(trace.branches[2].points[1],[0,3,0]),
 trace.branches[0].radii==[0.1,0.04],trace.branches[1].radii==[0.02,0.01],trace.branches[2].radii==[0.04,0.04],
 trace.branches[2].parent==trace.branches[0].id,trace.branches[1].level==1];
[near(turtle.skeleton("&F",{angle:90})^.end.position,[0,0,-1]),near(turtle.skeleton("^F",{angle:90})^.end.position,[0,0,1]),
 near(turtle.skeleton(["/","+","F"],{angle:90})^.end.position,[0,0,1]),
 near(turtle.skeleton(["\\","+","F"],{angle:90})^.end.position,[0,0,-1]),
 near(turtle.skeleton("|F")^.end.position,[0,-1,0]),near(turtle.skeleton("fF")^.branches[0].points[0],[0,1,0])];
let mesh=plant.mesh(trace)^,leaf=plant.mesh(turtle.skeleton(["L"])^)^,empty=plant.mesh(turtle.skeleton("A")^)^;
[native.validate(scene.to_scene3d(mesh))^,native.validate(scene.to_scene3d(leaf))^,
 len(leaf.parts)==1,len(leaf.resources)==1,len(empty.parts)==0,len(empty.resources)==0,
 abs(v.dot(trace.end.h,trace.end.l))<1e-12];
[(turtle.skeleton("F]") ^ {null})==null,(turtle.skeleton("[F") ^ {null})==null,
 (turtle.skeleton([["F",0]]) ^ {null})==null,(turtle.skeleton([["!",-1]]) ^ {null})==null,
 (turtle.skeleton([["L",1,-1]]) ^ {null})==null,(turtle.skeleton("F",{force:{strength:1,direction:[0,0,0]}}) ^ {null})==null,
 (turtle.skeleton(join([for (i in 1 to 513) "F"],"")) ^ {null})==null]
