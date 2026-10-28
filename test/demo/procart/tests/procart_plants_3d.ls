// S12.1.1v3 / D7.5.3v2: real seeded solids, valid native scenes and display-only modes.
import B: ~~.mod_plants
import solids: ~~.mod_plant_solids
import three: ~~.mod_botanical3d
import scene: lambda.scene3d

[for (recipe in B.SPECIES) (let p = B.generate(2026,recipe.id,3,25)^,
    let parts = solids.pieces(p),let box = solids.bounds(parts),
    // a single curved leaf has depth in its surface, even with its origin on the stem plane.
    [len(parts) > 0, box.hi[2] > box.lo[2],
     all([for (q in parts) all([for (v in [*q.position,*q.rotation,*q.scale]) v-v == 0.0]) and min(q.scale) > 0.0]),
     box.scale > 0.0, scene.validate(three.viewport(p,true,true))^])];

let st = B.initial()
let spatial = B.command(st,"view3d");
[spatial.mode == "3d", spatial.plant == st.plant, B.command(spatial,"view2d") == st,
 B.command(B.command(spatial,"cactus"),"next").mode == "3d"];

// caps, ribs and spines belong to their biological form; visibility keeps their skeletons.
let cactus = B.generate(2026,"cactus",4,25)^
let fungus = B.generate(2026,"fungus",4,25)^
let hidden = solids.pieces(cactus,true,false);
[cactus == B.generate(2026,"cactus",4,25)^, fungus == B.generate(2026,"fungus",4,25)^,
 cactus != B.generate(2027,"cactus",4,25)^, fungus != B.generate(2027,"fungus",4,25)^,
 len([for (p in cactus.solids where p.role == "spine") p]) > 0,
 len([for (p in fungus.solids where p.role == "cap") p]) == 5,
 len([for (p in hidden where p.role == "flower") p]) == 0,
 len([for (p in hidden where p.role == "spine") p]) == len([for (p in cactus.solids where p.role == "spine") p])];

[len(B.generate(2026,"fungus",5,25)^.tips) > len(fungus.tips),
 B.generate(2026,"cactus",4,40)^.solids != cactus.solids,
 B.generate(2026,"fungus",4,40)^.solids != fungus.solids]
