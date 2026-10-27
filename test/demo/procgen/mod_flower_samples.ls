// reference-inspired flower architecture stays pure and seeded (S12.1.1v3, PG2).
import rng: lambda.procgen.rng
import transforms: lambda.scene3d.transform
import camera: lambda.scene3d.camera
import bloom: .mod_bloom
import solid: .mod_plant_solids

fn leaf_fan(position,angle,length,width,count,color,shape="petal") array =>
    [for (i in 0 to (count-1),let a = angle+float(i)*2.0*math.pi/float(count))
        solid.oval(position,[length,length,width],color,"leaf",[0.0,-a,0.18],shape)]

fn pond_leaves(recipe,spread) array => [for (i in 0 to 1,let a = float(i)*2.5+0.3,
    let p = [spread*cos(a),0.16+float(i)*0.1,spread*sin(a)],let size = if (i == 0) 0.8 else 0.62,
    let parent = transforms.matrix(p,[0.0,-a,0.0],[size,size,size])^)
    *[solid.placed(solid.oval([0.0,0.0,0.0],[1.0,1.0,1.0],recipe.leaf,"leaf",[0.0,0.0,0.0],"pad"),parent),
      *[for (vein in 0 to 15,let t = float(vein)/15.0)
          solid.placed(solid.stem(bloom.point(0.08,t,"pad"),bloom.point(0.95,t,"pad"),0.006,"#a8bc70","leaf"),parent)]]]

fn foliage(recipe,height,spread) array => if (recipe.foliage == "pad") pond_leaves(recipe,spread)
    else [for (i in 0 to 3,let a = float(i)*2.4,let y = height*(0.18+float(i)*0.13),
        let p = [0.32*cos(a),y,0.32*sin(a)])
        *[solid.stem([0.0,y-0.08,0.0],p,0.012,recipe.stem,"leaf"),
          *leaf_fan(p,a,if (recipe.foliage == "palm") 0.38 else 0.62,
              if (recipe.foliage == "palm") 0.065 else 0.17,
              if (recipe.foliage == "palm") 7 else 1,recipe.leaf)]]

// petal rings share one construction; recipes set shape, packing, color and opening.
fn rings(k,recipe,order,angle) array {
    let layers = max(2,recipe.layers+order-3)
    let form = bloom.profile(rng.derive(k,"head"),recipe.petals)
    let parent = transforms.matrix([0.0,recipe.height,0.0],
        [form.lean[0]*0.4,form.phase,form.lean[1]*0.4],[form.size,form.size,form.size])^;
    [for (layer in 0 to (layers-1),let t = float(layer)/float(layers-1),
        let count = recipe.petals+layer*2,
        let ring = bloom.profile(rng.at(rng.derive(k,"rings"),layer),count),
        let length = recipe.length*(1.0-0.55*t),
        let tilt = recipe.tilt+0.7*t+(float(angle)-25.0)*0.01,
        let color = recipe.colors[min(len(recipe.colors)-1,int(t*float(len(recipe.colors))))])
        for (i,p in ring.petals,let a = p.angle+float(layer)*0.35,
            let shade = if (recipe.highlight != null and rng.chance(rng.at(rng.at(k,layer),i),0.24)) recipe.highlight else color,
            let radius = 0.11*(1.0-t),let petal = solid.oval([radius*cos(a),float(layer)*0.07,radius*sin(a)],
                [length*p.length,length*p.length,recipe.width*(1.0-0.35*t)*p.width],shade,"flower",
                [0.0,-a,tilt+(p.tilt-0.49)*0.35],recipe.petal_shape))
            {*:solid.placed(petal,parent),component:"petal",tier:layer}]
}

fn stamens(k,recipe,order) array {
    let base = recipe.height+0.12+float(max(2,recipe.layers+order-3)-1)*0.07;
    [for (i in 0 to (recipe.stamens-1),let key = rng.at(k,i),let a = float(i)*2.4,
        let r = rng.between(key,0.04,0.2),let p = [r*cos(a),base,r*sin(a)],
        let tip = [r*1.25*cos(a),base+rng.between(rng.derive(key,"height"),0.25,0.57),r*1.25*sin(a)])
        *[{*:solid.stem(p,tip,0.009,"#d6ab54","flower"),component:"stamen"},
          {*:solid.oval(tip,[0.023,0.045,0.023],"#ffd56a","flower"),component:"anther"}]]
}

pub fn layered(k,recipe,order,angle) map {
    let stalk = solid.stem([0.0,0.0,0.0],[0.0,recipe.height,0.0],0.035,recipe.stem)
    let petals = rings(k,recipe,order,angle)
    let center = solid.oval([0.0,recipe.height+0.12+float(max(2,recipe.layers+order-3)-1)*0.07,0.0],
        (if (recipe.center_size != null) recipe.center_size else [0.16,0.09,0.16]),recipe.center,"flower")
    let leaves = foliage(recipe,recipe.height,0.55+float(angle)*0.008);
    {recipe:recipe,sequence:"",segments:[[0.0,0.0,0.0,recipe.height,0,0.07]],leaves:[],
     tips:[{x:0.0,y:recipe.height,z:0.0,phase:0.0}],petal:recipe.colors[0],
     solids:[stalk,*leaves,*petals,center,*stamens(rng.derive(k,"stamens"),recipe,order)]}
}

fn pea_blossom(position,rotation,size,shade) array {
    let parent = transforms.matrix(position,rotation,[size,size,size])^
    let parts = [solid.oval([0.0,0.01,-0.09],[0.16,0.075,0.19],shade,"flower"),
        *[for (side in [-1.0,1.0]) solid.oval([side*0.085,0.025,0.065],[0.1,0.07,0.14],"#f5d5df","flower",[0.0,0.0,side*0.35])],
        solid.oval([0.0,0.09,0.045],[0.035,0.04,0.12],"#bd5d8c","flower")];
    [for (part in parts) solid.placed(part,parent)]
}

// the tall spike tapers from open, folded pea flowers into smaller terminal buds.
pub fn lupin(k,recipe,order,angle) map {
    let height = 2.6+float(order)*0.28
    let rows = 7+order*2
    let heads = [for (row in 0 to (rows-1),let t = float(row)/float(rows-1))
        for (side in 0 to 2,let key = rng.at(k,row*3+side),let a = float(side)*2.0*math.pi/3.0+float(row)*0.45,
            let r = 0.23*(1.0-t)+0.035,
            let p = [r*cos(a),0.95+t*(height-0.95),r*sin(a)],
            let direction = [cos(a),0.18+float(angle)*0.006,sin(a)])
            {p:p,rotation:solid.stem([0.0,0.0,0.0],direction,0.01,recipe.stem).rotation,
             size:(1.65-1.2*t)*rng.between(key,0.85,1.15),
             shade:rng.pick(rng.derive(key,"shade"),["#e7a1bd","#f0b8cd","#edbdd5","#d991b4"])}]
    let flowers = [for (head in heads) *pea_blossom(head.p,head.rotation,head.size,head.shade)]
    let leaves = foliage(recipe,1.25,1.0);
    {recipe:recipe,sequence:"",segments:[[0.0,0.0,0.0,height,0,0.07]],leaves:[],
     tips:[for (head in heads) {x:head.p[0],y:head.p[1],z:head.p[2],phase:0.0}],petal:"#edbdd5",
     solids:[solid.stem([0.0,0.0,0.0],[0.0,height+0.12,0.0],0.035,recipe.stem),*leaves,*flowers,
         solid.oval([0.0,height+0.15,0.0],[0.04,0.095,0.04],"#edc4da","flower")]}
}
