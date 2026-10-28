// keyed rewriting is pure (S12.1.1v3); free-angle turtle geometry follows PG2.
import rng: lambda.procart.rng
import lsystem: lambda.procart.lsystem
import solids: .mod_plant_solids
import bloom: .mod_bloom
import flowers: .mod_flower_samples
import foliage: .mod_foliage

pub let GARDEN = [
    {id:"fern",title:"Woodland fern",tag:"01 / FRONDS",note:"Repeated branches unfold into fine, feathered fronds.",
        rules:{X:"F[+X][-X]FX",F:"FF"},stem:"#527341",leaf:"#72914e",petals:0,leaf_mode:"all",leaf_size:0.24},
    {id:"herb",title:"Meadow herb",tag:"02 / BRANCHES",note:"A branching herb, with broad leaves along each new shoot.",
        rules:{X:[[1,"F[+X]F[-X]X"],[1,"F[-X]F[+X]X"]],F:"FF"},stem:"#4f704e",leaf:"#65936d",petals:0},
    {id:"flower",title:"Wild cosmos",tag:"03 / BLOSSOMS",note:"Forking stems carry radial flowers at their growing tips.",
        rules:{X:[[2,"F[+X][-X]"],[1,"F[+X]FX"]],F:"FF"},stem:"#557b55",leaf:"#7d9c67",petals:8,leaf_size:0.55},
    {id:"sunflower",title:"Golden sunflower",tag:"04 / SUN SEEKERS",note:"Golden rays surround dark seed heads on leafy, forking stalks.",
        rules:{X:"FF[+X][-X]",F:"F"},stem:"#608249",leaf:"#78934c",petals:16,petal:"#e8b93d",center:"#6a492e"},
    {id:"lavender",title:"Wild lavender",tag:"05 / FLOWER SPIKES",note:"Slender stems rise above silvery foliage into separated, tapering violet spikes.",
        kind:"lavender",stem:"#718575",leaf:"#91a58d",petals:5,petal:"#9d7cb8"},
    {id:"cactus",title:"Desert cactus",tag:"06 / SUCCULENTS",note:"Ribbed columns branch into upward arms, with spines and blush flowers.",
        kind:"cactus",stem:"#6b956a",leaf:"#83a27b",petals:7},
    {id:"fungus",title:"Woodland mushrooms",tag:"07 / FUNGI",note:"A seeded colony of spotted caps, pale stems and radial gills. Fungi are their own kingdom.",
        kind:"fungus",stem:"#e8ddba",leaf:"#e8ddba",petals:0,petal:"#bb6755"},
    {id:"lupin",title:"Pink lupin",tag:"08 / PEA FLOWERS",note:"Folded pink-and-cream blossoms spiral up a tall, tapering spike above palmate leaves.",
        kind:"lupin",stem:"#71834a",leaf:"#688b60",foliage:"palm",petals:4},
    {id:"bluecrown",title:"Blue crown",tag:"09 / LAYERED BLOOM",note:"Overlapping blue petals rise around a crown of slender golden stamens.",
        kind:"layered",stem:"#416e52",leaf:"#4f7b61",foliage:"lance",height:1.6,petals:12,layers:4,
        length:0.95,width:0.17,tilt:0.65,petal_shape:"round-petal",colors:["#62b6e3","#397cc6","#81c8ec","#b5e0ee"],highlight:"#b9e3f4",
        center:"#d9b25e",stamens:26,illustration_pitch:0.65,camera:[5.8,7.5,7.5]},
    {id:"rosette",title:"Pink rosette",tag:"10 / PETAL ROSETTE",note:"Staggered rings of pointed pink petals open from a compact rose-colored center.",
        kind:"layered",stem:"#6b8856",leaf:"#7a9c67",foliage:"lance",height:0.35,petals:16,layers:5,
        length:1.05,width:0.2,tilt:0.13,petal_shape:"petal",colors:["#df79aa","#ef9cc9","#f6b5d7","#ffd1df"],
        center:"#d996b7",center_size:[0.1,0.04,0.1],stamens:0,illustration_pitch:1.0,camera:[4.5,8.5,6.0]},
    {id:"lotus",title:"Pond lotus",tag:"11 / POND FLOWERS",note:"Broad, rounded pink petals form an open cup above notched pond leaves with radial veins.",
        kind:"layered",stem:"#5f865b",leaf:"#7f9c56",foliage:"pad",height:1.0,petals:8,layers:2,
        length:1.3,width:0.4,tilt:0.28,petal_shape:"round-petal",colors:["#e7a4c8","#efbdd7","#f8d9e5"],
        center:"#e0ba56",stamens:12,illustration_pitch:0.7,camera:[5.5,7.0,7.0]}
]

pub let SPECIES = [*GARDEN,*foliage.CATALOG]
pub fn visible(st) array => if (st.section == "foliage") foliage.CATALOG else GARDEN

pub fn species(id) map => [for (s in SPECIES where s.id == id) s][0]

// an endpoint is a growing tip only when no segment continues from it.
fn point_key(x, y) string => string(x) ++ "," ++ string(y)

// data-authored recipes share the catalogue's generators without a species lookup.
pub fn grow(k: u64, recipe: map, order: int, angle) map^ {
    let generators = {cactus:solids.cactus,fungus:solids.fungus,lavender:solids.lavender,layered:flowers.layered,lupin:flowers.lupin,foliage:foliage.generate}
    let grow = generators[recipe.kind];
    if (grow != null) grow(k,recipe,order,angle)
    else {
    let sequence = lsystem.expand(if (recipe.axiom != null) recipe.axiom else "X", recipe.rules, order, rng.derive(k,"branches"))^
    let heading = 90.0 + rng.between(rng.derive(k,"lean"), -5.0, 5.0)
    let turtle = lsystem.turtle(sequence, {angle:float(angle),heading:heading,width:3.6,width_decay:0.72})^
    let segments = turtle.segments
    let starts = map([for (s in segments) for (v in [point_key(s[0],s[1]),true]) v])
    let tips = [for (i, s in segments where starts[point_key(s[2],s[3])] == null)
        {x:s[2],y:s[3],phase:rng.between(rng.at(rng.derive(k,"petals"),i),0.0,360.0),
         bloom:bloom.profile(rng.at(rng.derive(k,"bloom-form"),i),recipe.petals)}]
    let leaves = [for (i, s in segments where s[4] > 0 and (recipe.leaf_mode == "all" or i % 2 == 0))
        {x:(s[0]+s[2])/2.0,y:(s[1]+s[3])/2.0,
         angle: -math.atan2(s[3]-s[1],s[2]-s[0])*180.0/math.pi + (if (i % 2 == 0) -42.0 else 42.0),
         size:(if (recipe.leaf_size != null) recipe.leaf_size else 0.5) *
             rng.between(rng.at(rng.derive(k,"leaves"),i),0.85,1.15)}];
    {recipe:recipe,sequence:sequence,segments:segments,tips:tips,leaves:leaves,
     petal:if (recipe.petal != null) recipe.petal else rng.pick(rng.derive(k,"color"),["#c8657e","#bd6a9c","#d58c75","#ad82b6"])}
    }
}

pub fn generate(seed, id, order, angle) map^ => grow(rng.derive(rng.key(seed),id),species(id),order,angle)^

pub fn initial() map => {seed:2026,id:"flower",section:"garden",order:4,angle:25,leaves:true,blooms:true,mode:"2d",
    plant:generate(2026,"flower",4,25)^}

pub fn command(st, action) map {
    if (action == "leaves") {*:st,leaves:not st.leaves}
    else if (action == "blooms") {*:st,blooms:not st.blooms}
    else if (action == "view2d" or action == "view3d") {*:st,mode:if (action == "view3d") "3d" else "2d"}
    else {
        let seed = if (action == "next") st.seed+1 else if (action == "previous") max(0,st.seed-1) else st.seed
        let id = if (action == "group-foliage") "olive" else if (action == "group-garden") "flower"
            else if (any([for (s in SPECIES) s.id == action])) action else st.id
        let section = if (species(id).section == "foliage") "foliage" else "garden"
        let order = if (action == "grow") min(5,st.order+1) else if (action == "prune") max(1,st.order-1) else st.order
        let angle = if (action == "wider") min(40,st.angle+5) else if (action == "narrower") max(15,st.angle-5) else st.angle;
        if (seed == st.seed and id == st.id and order == st.order and angle == st.angle) st
        else {*:st,seed:seed,id:id,section:section,order:order,angle:angle,plant:generate(seed,id,order,angle)^}
    }
}
