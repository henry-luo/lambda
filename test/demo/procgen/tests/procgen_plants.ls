// botanical growth, seed replay and display-only controls (S12.1.1v3, PG2).
import B: ~~.mod_plants

let st = B.initial()
let next = B.command(st,"next")
let previous = B.command(next,"previous");
[st.plant == B.generate(2026,"flower",4,25)^, previous.plant == st.plant,
 next.plant.sequence != st.plant.sequence, next.seed == 2027];

// visibility never changes generated geometry; growth and angle both do.
let bare = B.command(B.command(st,"leaves"),"blooms")
let grown = B.command(st,"grow")
let wide = B.command(st,"wider");
[bare.plant == st.plant, not bare.leaves, not bare.blooms,
 len(grown.plant.segments) > len(st.plant.segments), grown.order == 5,
 wide.plant.sequence == st.plant.sequence, wide.plant.segments != st.plant.segments];

// all recipes have finite stems and tips; solid succulents and fungi have no leaves.
[for (recipe in B.SPECIES) (let p = B.generate(2026,recipe.id,3,25)^,
    [len(p.segments) > 0, (recipe.kind != null or len(p.leaves) > 0), len(p.tips) > 0,
     all([for (s in p.segments) len(s) == 6 and s[5] > 0 and all([for (v in s) v-v == 0.0])]),
     all([for (t in p.tips) not any([for (s in p.segments) s[0] == t.x and s[1] == t.y])])])];

// controls have finite ranges, including no-op clicks at both ends.
[B.command({*:st,seed:0},"previous").seed == 0,
 B.command({*:st,order:1},"prune").order == 1,
 B.command(grown,"grow") == grown,
 B.command({*:st,angle:15},"narrower").angle == 15,
 B.command({*:st,angle:40},"wider").angle == 40,
 B.command(st,"unknown") == st];

let fern = B.command(st,"fern")
let herb = B.command(st,"herb");
[fern.id == "fern", herb.id == "herb", fern.seed == st.seed,
 fern.plant.recipe.petals == 0, herb.plant.recipe.petals == 0, st.plant.recipe.petals == 8,
 B.command(fern,"flower").plant == st.plant]
