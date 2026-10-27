// workshop models retain geometry across palette changes and reproduce seeds (S12.1.1v3).
import R: ~~.mod_minerals
import P: ~~.mod_patterns
import T: ~~.mod_textures
import W: ~~.mod_workshop
import solids: ~~.mod_plant_solids
import scene: lambda.scene3d
import noise: lambda.procgen.noise

[for (r in R.CATALOG) (let m = R.generate(2026,r.id,3,0)^,
    let colored = R.generate(2026,r.id,3,1)^,
    [m == R.generate(2026,r.id,3,0)^,m.parts != R.generate(2027,r.id,3,0)^.parts,
     len(R.generate(2026,r.id,4,0)^.parts) > len(m.parts),
     (m.parts |> ~.position) == (colored.parts |> ~.position),m.parts != colored.parts,
     len(m.edges) > len(m.parts),scene.validate(R.viewport(m,true)^)^,
     all([for (p in m.parts) all([for (v in solids.matrix(p)) v-v == 0.0])])])];

[for (r in P.CATALOG) (let m = P.generate(2026,r.id,3,0)^,
    [m == P.generate(2026,r.id,3,0)^,m.nodes != P.generate(2027,r.id,3,0)^.nodes,
     len(P.generate(2026,r.id,4,0)^.nodes) > len(m.nodes),m.nodes != P.generate(2026,r.id,3,1)^.nodes])];

// each swatch must contain actual encoded pixels, including on a cold generator call.
[for (r in T.CATALOG) (let m = T.generate(2026,r.id,3,0)^,
    [len(m.bytes) > 1000,starts_with(m.uri,"data:image/png;base64,"),len(m.field) == T.SIZE,
     all([for (row in m.field) len(row) == T.SIZE and all([for (v in row) v-v == 0.0])]),
     abs(noise.sample(m.generator,0.0,37.0)-noise.sample(m.generator,float(T.SIZE),37.0)) < 1e-9])];

let swatch = T.generate(2026,"wood",3,0)^
let recolored = T.generate(2026,"wood",3,1)^;
[swatch.bytes == T.generate(2026,"wood",3,0)^.bytes,
 swatch.bytes != T.generate(2027,"wood",3,0)^.bytes,
 swatch.field == recolored.field,swatch.bytes != recolored.bytes];

let config = {catalog:P.CATALOG,colorways:P.COLORWAYS,generate:P.generate}
let st = W.initial(config)
let overlay = W.command(config,st,"overlay");
[overlay.model == st.model,overlay.overlay,
 W.command(config,W.command(config,st,"next"),"previous").model == st.model,
 W.command(config,{*:st,detail:1},"less").detail == 1,
 W.command(config,{*:st,detail:5},"more").detail == 5]
