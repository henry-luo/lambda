// reference foliage preserves seeds, leaf topology and display-only controls (S12.1.1v3).
import B: ~~.mod_plants
import F: ~~.mod_foliage
import S: ~~.mod_plant_solids
import bloom: ~~.mod_bloom
import camera: lambda.scene3d.camera
import scene: lambda.scene3d
import three: ~~.mod_botanical3d

[for (r in F.CATALOG) (let p = B.generate(2026,r.id,4,25)^,
    [p == B.generate(2026,r.id,4,25)^,p != B.generate(2027,r.id,4,25)^,
     p != B.generate(2026,r.id,5,25)^,p != B.generate(2026,r.id,4,40)^,
     len([for (part in p.solids where part.component == "blade") part]) == len(p.tips),
     len([for (part in S.pieces(p,false,false) where part.role == "leaf" or part.role == "flower") part]) == 0,
     scene.validate(three.viewport(p,true,true))^])];

[for (kind in ["oak-leaf","fan-leaf"])
 all([for (i in 0 to 32,j in 0 to 16,let n = bloom.normal(float(i)/32.0,float(j)/8.0-1.0,kind))
     all([for (v in n) v-v == 0.0]) and abs(camera.dot(n,n)-1.0) < 1e-9])];

let st = B.command(B.command(B.initial(),"view3d"),"group-foliage")
let garden = B.command(st,"group-garden");
[len(B.visible(st)) == 7,st.id == "olive",st.mode == "3d",st.section == "foliage",
 len(B.visible(garden)) == 11,garden.id == "flower",garden.mode == "3d"];

let olive = B.generate(2026,"olive",4,25)^
let berries = B.generate(2026,"berryspray",4,25)^;
[olive.detail_count > 0,berries.detail_count > olive.detail_count,
 len([for (p in S.pieces(olive,true,false) where p.role == "flower") p]) == 0]
