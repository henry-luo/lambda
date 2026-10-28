// foliage architecture follows the reference's sprigs, fronds and broad leaves (S12.1.1v3).
import rng: lambda.procart.rng
import solid: .mod_plant_solids
import bloom: .mod_bloom
import transforms: lambda.scene3d.transform

pub let CATALOG = [
    {id:"olive",title:"Olive sprig",tag:"LEAVES / FRUIT",note:"Slender paired leaves and dark olives follow a gently curved twig.",
        kind:"foliage",section:"foliage",form:"sprig",blade:"petal",length:0.64,width:0.12,leaf:"#7d8c58",stem:"#78824f",petals:1,fruit:"#36343e",detail_label:"Olives"},
    {id:"eucalyptus",title:"Silver eucalyptus",tag:"LEAVES / ROUND",note:"Rounded blue-green leaves alternate along a loose, arching stem.",
        kind:"foliage",section:"foliage",form:"sprig",blade:"round-petal",length:0.52,width:0.28,base_nodes:3,node_step:1,leaf:"#779b8d",stem:"#78998d",petals:0},
    {id:"fernfrond",title:"Fern frond",tag:"LEAVES / PINNATE",note:"Paired leaflets taper along the curved rachis of a single feathered frond.",
        kind:"foliage",section:"foliage",form:"frond",blade:"petal",length:0.72,width:0.105,leaf:"#81915c",stem:"#8c965c",petals:0},
    {id:"palmfrond",title:"Palm frond",tag:"LEAVES / PALM",note:"Long, slender pinnae fan away from an arching central stem.",
        kind:"foliage",section:"foliage",form:"frond",blade:"petal",length:1.05,width:0.06,leaf:"#587a4d",stem:"#6d8553",petals:0},
    {id:"oak",title:"Oak leaf",tag:"LEAVES / LOBED",note:"Rounded lobes flank a pale midrib and branching side veins.",
        kind:"foliage",section:"foliage",form:"single",blade:"oak-leaf",length:2.7,width:0.86,leaf:"#8c9c62",stem:"#84925b",petals:0},
    {id:"ginkgo",title:"Ginkgo leaf",tag:"LEAVES / FAN",note:"A bilobed fan opens from a narrow petiole, with radiating veins.",
        kind:"foliage",section:"foliage",form:"single",blade:"fan-leaf",length:2.3,width:2.3,leaf:"#85975b",stem:"#819354",petals:0},
    {id:"berryspray",title:"Berry spray",tag:"LEAVES / BERRIES",note:"Forked twigs carry small leaves and dusty burgundy berries.",
        kind:"foliage",section:"foliage",form:"berries",blade:"petal",length:0.36,width:0.08,leaf:"#647c5b",stem:"#71865d",petals:1,fruit:"#926579",detail_label:"Berries"}
]

fn spine(t,branch) array => [0.5*t*t+branch*t,3.4*t,0.1*sin(t*3.0+branch)]
fn above(p) array => [p[0],p[1]+0.009,p[2]]

fn leaf(recipe,position,heading,length,width) array {
    let parent = transforms.matrix(position,[0.0,0.0,heading],[1.0,1.0,1.0])^
    let blade = solid.oval([0.0,0.0,0.0],[length,0.1,width],recipe.leaf,"leaf",[math.pi/2.0,0.0,0.0],recipe.blade)
    let m = solid.matrix(blade)
    let fan = recipe.blade == "fan-leaf"
    let veins = if (fan) [for (i in 0 to 8,let v = float(i)/4.0-1.0)
        solid.stem(above(bloom.point(0.08,v,recipe.blade)),above(bloom.point(0.94,v,recipe.blade)),0.004,"#c6cfa1","leaf")]
        else [solid.stem(above(bloom.point(0.04,0.0,recipe.blade)),above(bloom.point(0.96,0.0,recipe.blade)),0.006,"#ced6b1","leaf"),
            *[for (i in 1 to 5,let u = float(i)/7.0) for (side in [-1.0,1.0])
                solid.stem(above(bloom.point(u,0.0,recipe.blade)),above(bloom.point(min(0.97,u+0.14),side*0.84,recipe.blade)),0.003,"#b2c292","leaf")]];
    [{*:solid.placed(blade,parent),component:"blade"},
     *[for (vein in veins) {*:solid.placed(solid.placed(vein,m),parent),component:"vein"}]]
}

pub fn generate(k,recipe,order,angle) map {
    let single = recipe.form == "single"
    let count = if (single) 1 else (if (recipe.base_nodes != null) recipe.base_nodes else 5)+order*(if (recipe.node_step != null) recipe.node_step else 2)
    let branches = if (recipe.form == "berries") [-0.8,0.15,1.0] else [0.0]
    let segments = if (single) [solid.stem([0.0,0.0,0.0],[0.0,0.65,0.0],0.025,recipe.stem)]
        else [for (branch in branches) for (i in 0 to (count-1))
            solid.stem(spine(float(i)/float(count),branch),spine(float(i+1)/float(count),branch),0.018,recipe.stem)]
    let blades = if (single) leaf(recipe,[0.0,0.6,0.0],math.pi/2.0-0.18+float(angle-25)*0.012,
        recipe.length*(0.6+float(order)*0.1)*rng.between(k,0.9,1.1),recipe.width*rng.between(rng.at(k,1),0.9,1.1))
        else [for (branch in branches) for (i in 1 to (count-1),let t = float(i)/float(count))
            for (side in (if (recipe.form == "frond") [-1.0,1.0] else [if (i%2 == 0) -1.0 else 1.0]),
                let size = rng.between(rng.at(k,i+int((branch+1.0)*50.0)),0.85,1.1)*(if (recipe.form == "frond") (1.15-0.7*t) else 1.0),
                let heading = math.pi/2.0-side*(0.65+float(angle)*0.014))
                *leaf(recipe,spine(t,branch),heading,recipe.length*size,recipe.width*size)]
    let fruits = if (recipe.fruit == null) [] else [for (branch in branches) for (i in 2 to (count-1) where i%2 == 0 or recipe.form == "berries",
        let p = spine(float(i)/float(count),branch),let side = if (i%2 == 0) -1.0 else 1.0,
        let tip = [p[0]+side*0.18,p[1]+0.08,p[2]+0.1],let size = rng.between(rng.at(k,i*7+int((branch+1.0)*30.0)),0.85,1.15))
        *[solid.stem(p,tip,0.009,recipe.stem,"flower"),
          {*:solid.oval(tip,[0.075*size,0.1*size,0.065*size],recipe.fruit,"flower"),component:"fruit"}]]
    let tips = [for (p in blades where p.component == "blade") {x:p.position[0],y:p.position[1]+0.01,z:p.position[2],phase:0.0}];
    {recipe:recipe,sequence:"",segments:[for (s in segments) [s.a[0],s.a[1],s.b[0],s.b[1],0,0.036]],
     leaves:[],tips:tips,solids:[*segments,*blades,*fruits],petal:recipe.fruit,
     detail_count:len([for (p in fruits where p.component == "fruit") p])}
}
