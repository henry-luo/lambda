// all contexts observe the old generation; geometry interpretation is a separate step (PG13.3).
import lsys: lambda.procart.lsystem
import rng: lambda.procart.rng
import turtle: lambda.procart.turtle3d
import plant: lambda.procart.plant
import scene: lambda.procart.scene
import native: lambda.scene3d
fn failed_production(m,ctx) array^ => raise error("production failed")
fn neighbor(m,ctx) array => [if (ctx.index == 0) ctx.right else ctx.left]
let swapped=lsys.derive(["A","B"],{A:neighbor,B:neighbor},1)^;
[swapped==["B","A"],lsys.derive(["A","B"],{A:neighbor,B:neighbor},2)^==["A","B"],
 lsys.derive("XYZ",{},2)^==["X","Y","Z"],lsys.derive(["A"],{A:(m,ctx)=>[]},1)^==[]];
let contexts=lsys.derive(["A","[","B","]","C"],{B:(m,ctx)=>[["Seen",ctx.left,ctx.right,ctx.index,ctx.generation]]},1)^;
[contexts[2]==["Seen","[","]",2,0]];
let rules={A:(m,ctx)=> if (m[1]>0.2) [["F",m[1],m[2]],"[",["&",35.0],["/",137.5],["A",m[1]*0.7,m[2]*0.65],"]",
 ["+",22.0],["A",m[1]*0.72,m[2]*0.65]] else ["L"]}
let seq=lsys.derive([["A",1.0,0.08]],rules,5,{seed:"development"})^
let sk=turtle.skeleton(seq,{radius:0.12})^;
[len(sk.branches)==31,native.validate(scene.to_scene3d(plant.mesh(sk)^))^,
 seq==lsys.derive([["A",1.0,0.08]],rules,5,{seed:"development"})^];
let random={A:(m,ctx)=>[["A",rng.unit(ctx.key)]]};
[lsys.derive(["A","A"],random,3,{seed:17})^==lsys.derive(["A","A"],random,3,{seed:17})^,
 lsys.derive(["A","A"],random,3,{seed:17})^!=lsys.derive(["A","A"],random,3,{seed:18})^,
 lsys.expand([["A",2.0]],{A:(m)=>[["F",m[1]]]},1)^==[["F",2.0]]];
[(lsys.derive("F",{F:"FF"},10,{max_symbols:100}) ^ {null})==null,
 (lsys.derive("F",{},65) ^ {null})==null,(lsys.derive([7],{},1) ^ {null})==null,
 (lsys.derive("F",{},1,{max_symbols:0}) ^ {null})==null,
 (lsys.derive("F",{F:(m,ctx)=>7},1) ^ {null})==null,
 (lsys.derive("F",{F:failed_production},1) ^ {null})==null]
