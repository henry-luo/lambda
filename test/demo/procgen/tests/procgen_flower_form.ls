// flower morphology is seeded and shared by both drawing modes (S12.1.1v3, PG2).
import B: ~~.mod_plants
import solids: ~~.mod_plant_solids
import bloom: ~~.mod_bloom
import transforms: lambda.scene3d.transform
import camera: lambda.scene3d.camera

let plant = B.generate(2026,"flower",4,25)^
let petals = [for (p in solids.pieces(plant) where p.shape == "petal") p]
fn location(p,u) => transforms.point(solids.matrix(p),bloom.point(u,0.0))^
let lengths = [for (p in petals,let d = camera.subtract(location(p,1.0),location(p,0.0))) sqrt(camera.dot(d,d))];

// petals rise and cup above their bases instead of forming horizontal disks.
[len(petals) == 3*len(plant.tips)*plant.recipe.petals,
 all([for (p in petals) location(p,1.0)[1] > location(p,0.0)[1]]),
 all([for (p in petals) location(p,0.5)[1] > (location(p,0.0)[1]+location(p,1.0)[1])/2.0])];

let sizes = [for (tip in plant.tips) tip.bloom.size]
let widths = [for (p in petals) p.scale[2]];
[max(sizes)/min(sizes) > 1.2, max(lengths)/min(lengths) > 1.3, max(widths)/min(widths) > 1.2,
 B.generate(2026,"flower",4,25)^ == plant,
 B.generate(2027,"flower",4,25)^.tips != plant.tips];

// shared cupped geometry has finite unit normals, including its narrow ends.
[all([for (j in 0 to 8, i in 0 to 6,let n = bloom.normal(float(j)/8.0,2.0*float(i)/6.0-1.0))
    all([for (v in n) v-v == 0.0]) and abs(camera.dot(n,n)-1.0) < 1e-9])];

let lavender = B.generate(2026,"lavender",4,25)^
let heads = lavender.tips
let florets = [for (p in lavender.solids where p.role == "flower") p]
let leaves = [for (p in lavender.solids where p.role == "leaf") p]
let leaf_top = max([for (p in leaves) p.position[1]]);
[len(heads) == 7, max([for (h in heads) h.x])-min([for (h in heads) h.x]) > 1.0,
 all([for (i,a in heads) for (j,b in heads where i < j)
     (a.x-b.x)**2+(a.y-b.y)**2+(a.z-b.z)**2 > 0.35**2]),
 max([for (p in florets) max(p.scale)])/min([for (h in heads) h.y]) < 0.04,
 leaf_top < min([for (h in heads) h.y])*0.8,
 len(solids.pieces(lavender,true,false)) < len(lavender.solids),
 len([for (p in solids.pieces(lavender,true,false) where p.role == "flower") p]) == 0,
 len(B.generate(2026,"lavender",5,25)^.tips) > len(heads),
 lavender == B.generate(2026,"lavender",4,25)^]
