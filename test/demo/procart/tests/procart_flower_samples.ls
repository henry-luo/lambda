// four reference-inspired architectures retain seeded, pure construction (S12.1.1v3).
import B: ~~.mod_plants
import solids: ~~.mod_plant_solids
import bloom: ~~.mod_bloom
import camera: lambda.scene3d.camera
import three: ~~.mod_botanical3d
import scene: lambda.scene3d

let ids = ["lupin","bluecrown","rosette","lotus"];
[for (id in ids) (let p = B.generate(2026,id,4,25)^,
    let grown = B.generate(2026,id,5,25)^,let bare = solids.pieces(p,false,false),
    [p == B.generate(2026,id,4,25)^, p != B.generate(2027,id,4,25)^,
     len(grown.solids) > len(p.solids), p.solids != B.generate(2026,id,4,40)^.solids,
     all([for (part in p.solids) all([for (v in solids.matrix(part)) v-v == 0.0]) and min(part.scale) > 0.0]),
     len([for (part in bare where part.role == "flower" or part.role == "leaf") part]) == 0,
     scene.validate(three.viewport(p,true,true))^])];

let lupin = B.generate(2026,"lupin",4,25)^
let flowers = [for (p in lupin.solids where p.role == "flower") p]
let mid = (max(lupin.tips |> ~.y)+min(lupin.tips |> ~.y))/2.0
fn radius(p) => sqrt(p.position[0]**2+p.position[2]**2);
[len(lupin.tips) > 30, max(lupin.tips |> ~.y)-min(lupin.tips |> ~.y) > 2.0,
 max([for (p in flowers where p.position[1] < mid) radius(p)]) > max([for (p in flowers where p.position[1] > mid) radius(p)]),
 len([for (p in lupin.solids where p.role == "leaf") p]) > 20];

let blue = B.generate(2026,"bluecrown",4,25)^
let rosette = B.generate(2026,"rosette",4,25)^
let lotus = B.generate(2026,"lotus",4,25)^
let blue_petals = [for (p in blue.solids where p.component == "petal") p]
let rose_petals = [for (p in rosette.solids where p.component == "petal") p]
let pads = [for (p in lotus.solids where p.shape == "pad") p]
let pad_box = solids.bounds(pads);
[len(blue_petals) > 50, len(unique(blue_petals |> ~.tier)) == 5,
 len([for (p in blue.solids where p.component == "stamen") p]) == 26,
 len([for (p in blue.solids where p.component == "anther") p]) == 26,
 all(blue_petals |> (~.shape == "round-petal")), len(unique(blue_petals |> ~.color)) >= 4,
 len(unique(rose_petals |> ~.tier)) == 6, all(rose_petals |> (~.shape == "petal")),
 max(rose_petals |> ~.rotation[2])-min(rose_petals |> ~.rotation[2]) > 0.6,
 len(pads) == 2, pad_box.hi[1]-pad_box.lo[1] < 0.25];

// rounded petals and notched leaves retain finite unit normals at their poles.
[for (kind in ["round-petal","pad"])
    all([for (j in 0 to 8, i in 0 to 8,
        let v = if (kind == "pad") float(i)/8.0 else float(i)/4.0-1.0,
        let n = bloom.normal(float(j)/8.0,v,kind),let p = bloom.point(float(j)/8.0,v,kind))
        all([for (x in [*n,*p]) x-x == 0.0]) and abs(camera.dot(n,n)-1.0) < 1e-9])];

let spatial = B.command(B.initial(),"view3d");
[for (id in ids) (let chosen = B.command(spatial,id),
    chosen.mode == "3d" and chosen.id == id and B.command(B.command(chosen,"next"),"previous").plant == chosen.plant)]
