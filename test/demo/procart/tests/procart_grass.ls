// patch prefixes, blade surfaces and bounds challenge the seeded grass construction.
import G: ~~.mod_grass
import solid: ~~.mod_plant_solids
import rng: lambda.procgen.rng
import camera: lambda.scene3d.camera

let p = {clumps:2,blades:5,radius:0.6,height:0.5,width:0.04,bend:0.4,colors:["#778844","#99aa66"]}
let blades = G.generate(rng.key(17),p)^
let more = G.generate(rng.key(17),{*:p,clumps:3})^
let mesh = G.geometry();
[len(blades) == 10,blades == G.generate(rng.key(17),p)^,
 blades != G.generate(rng.key(18),p)^,slice(more,0,10) == blades,
 all([for (b in blades) b.position[1] == 0.0 and min(b.scale) > 0.0 and
     sqrt(b.position[0]**2+b.position[2]**2) <= p.radius+0.045 and contains(p.colors,b.color)]),
 len(mesh.positions) == 30,len(mesh.indices) == 24,
 all([for (i in 0 to (len(mesh.normals) div 3-1),let n = slice(mesh.normals,i*3,i*3+3))
     abs(camera.dot(n,n)-1.0) < 1e-9]),
 (G.generate(rng.key(17),{*:p,blades:0}) ^ { null }) == null,
 (G.generate(rng.key(17),{*:p,bend:0.0}) ^ { null }) == null]
