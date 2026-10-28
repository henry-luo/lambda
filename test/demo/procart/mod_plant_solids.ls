// shared solid organs feed the illustrated and native 3D views (S12.1.1v3).
import rng: lambda.procart.rng
import camera: lambda.scene3d.camera
import transforms: lambda.scene3d.transform
import bloom: .mod_bloom

pub fn oval(position,scale,color,role,rotation=[0.0,0.0,0.0],shape="round") map =>
    {position:position,scale:scale,color:color,role:role,rotation:rotation,shape:shape}

pub fn matrix(part) array => if (part.matrix != null) part.matrix else transforms.matrix(part.position,part.rotation,part.scale)^

pub fn placed(part,parent) map {
    let m = transforms.multiply(parent,matrix(part))^;
    {*:part,matrix:m,position:[m[12],m[13],m[14]]}
}

// orient the long Y axis along the actual 3D segment, including depth.
pub fn stem(a,b,r,color,role="stem") map {
    let d = camera.subtract(b,a)
    let length = sqrt(camera.dot(d,d));
    {*:oval(camera.times(camera.add(a,b),0.5),[r,length/2.0+r,r],color,role,
        [math.atan2(d[2],d[1]),0.0,-math.asin(d[0]/length)]),a:a,b:b}
}

fn flower(position,recipe,color,form,turn=0.0) array {
    let parent = transforms.matrix(position,[form.lean[0],form.phase+turn,form.lean[1]],[form.size,form.size,form.size])^
    let parts = [*[for (p in form.petals)
        oval([0.075*cos(p.angle),0.0,0.075*sin(p.angle)],
            [0.4*p.length,0.4*p.length,0.115*p.width],color,"flower",[0.0,-p.angle,p.tilt],"petal")],
        oval([0.0,0.035,0.0],[0.12,0.075,0.12],if (recipe.center != null) recipe.center else "#dfb75d","flower")];
    [for (part in parts) placed(part,parent)]
}

fn ribbed(a,b,r,color,order) array {
    let height = b[1]-a[1];
    [oval(camera.times(camera.add(a,b),0.5),[r,height/2.0+r,r],color,"body"),
     *[for (rib in 0 to 7,let az = float(rib)*math.pi/4.0)
        oval([a[0]+r*0.78*cos(az),(a[1]+b[1])/2.0,a[2]+r*0.78*sin(az)],
            [r*0.28,height/2.0+r*0.8,r*0.28],if (rib%2 == 0) "#649267" else "#80a77c","rib")],
     *[for (j in 1 to (order+2),let y = a[1]+height*float(j)/float(order+3))
        for (rib in 0 to 7,let az = float(rib)*math.pi/4.0,
            let p = [a[0]+r*cos(az),y,a[2]+r*sin(az)])
            stem(p,camera.add(p,[0.14*cos(az),0.065,0.14*sin(az)]),0.012,"#ead9a8","spine")]]
}

pub fn cactus(k,recipe,order,angle) map {
    let height = 1.8+float(order)*0.42
    let arms = [for (i in 0 to (min(3,order)-1),let az = float(i)*2.4+rng.between(rng.at(k,i),-0.25,0.25),
        let reach = 0.6+float(angle)*0.016,let y = height*(0.3+float(i)*0.13),
        let p = [reach*cos(az),y,reach*sin(az)])
        {a:[0.0,y,0.0],b:p,c:[p[0],y+0.65+rng.between(rng.at(k,i+10),0.0,0.5),p[2]]}]
    let shoots = [{a:[0.0,0.3,0.0],b:[0.0,height,0.0],r:0.32},
        *[for (a in arms) {a:a.b,b:a.c,r:0.23}]]
    let tips = [for (i,shoot in shoots) {x:shoot.b[0],y:shoot.b[1]+shoot.r,z:shoot.b[2],phase:0.0,
        bloom:bloom.profile(rng.at(rng.derive(k,"bloom-form"),i),recipe.petals)}]
    let solids = [*[for (a in arms) stem(a.a,a.b,0.2,recipe.stem,"body")],
        *[for (shoot in shoots) *ribbed(shoot.a,shoot.b,shoot.r,recipe.stem,order)],
        *[for (tip in tips) *flower([tip.x,tip.y,tip.z],recipe,"#eab2c2",tip.bloom)]];
    {recipe:recipe,sequence:"",segments:[for (shoot in shoots) [shoot.a[0],shoot.a[1],shoot.b[0],shoot.b[1],0,shoot.r*2.0]],
     leaves:[],tips:tips,solids:solids,petal:"#eab2c2"}
}

pub fn fungus(k,recipe,order,angle) map {
    let mushrooms = [for (i in 0 to order,let key = rng.at(k,i),let az = float(i)*2.4,
        let spread = if (i == 0) 0.0 else 0.6+float(angle)*0.012,
        let height = if (i == 0) 2.6 else rng.between(key,0.8,2.0),
        let radius = (0.36+height*0.22)*rng.between(rng.at(key,1),0.85,1.1))
        {p:[spread*cos(az),height,spread*sin(az)],radius:radius}]
    let solids = [for (i,m in mushrooms)
        *[stem([m.p[0],0.0,m.p[2]],m.p,0.09+m.radius*0.08,"#e8ddba"),
          oval(m.p,[m.radius,m.radius*0.62,m.radius],recipe.petal,"cap",[0.0,0.0,0.0],"dome"),
          oval(camera.add(m.p,[0.0,-0.025,0.0]),[m.radius,0.035,m.radius],"#e9cfa8","gill"),
          *[for (gill in 0 to 17,let az = float(gill)*math.pi/9.0)
              stem(camera.add(m.p,[0.12*cos(az),-0.06,0.12*sin(az)]),
                  camera.add(m.p,[m.radius*0.94*cos(az),-0.06,m.radius*0.94*sin(az)]),0.012,"#c5aa89","gill")],
          *[for (spot in 0 to 11,let az = float(spot)*2.4,let t = rng.between(rng.at(k,i*20+spot),0.22,1.25))
              oval(camera.add(m.p,[m.radius*sin(t)*cos(az),m.radius*0.62*cos(t),m.radius*sin(t)*sin(az)]),
                  [0.075,0.035,0.075],"#f4e8ca","spot",[t*sin(az),0.0,-t*cos(az)])]]];
    {recipe:recipe,sequence:"",segments:[for (m in mushrooms) [m.p[0],0.0,m.p[0],m.p[1],0,0.2]],leaves:[],
     tips:[for (m in mushrooms) {x:m.p[0],y:m.p[1],z:m.p[2],phase:0.0}],solids:solids,petal:recipe.petal}
}

fn lavender_spike(shoot,key,recipe) array {
    let length = rng.between(rng.derive(key,"length"),0.62,0.88)
    let parent = transforms.matrix(shoot.tip,shoot.rotation,[1.0,1.0,1.0])^
    let florets = [for (level in 0 to 7,let t = float(level)/7.0,let radius = 0.09*(1.0-t)+0.025)
        for (i in 0 to 4,let k = rng.at(key,level*5+i),let a = float(i)*2.0*math.pi/5.0+float(level)*0.7,
            let size = rng.between(k,0.82,1.15)*(1.0-0.35*t))
            oval([radius*cos(a),length*t,radius*sin(a)],
                [0.048*size,0.032*size,0.035*size],
                rng.pick(rng.derive(k,"shade"),["#8c69a9","#a080ba","#b299cc","#9875b2"]),"flower",[0.0,-a,0.2])];
    [placed(stem([0.0,0.0,0.0],[0.0,length,0.0],0.012,recipe.stem),parent),
     *[for (part in florets) placed(part,parent)],
     placed(oval([0.0,length+0.035,0.0],[0.026,0.055,0.026],"#b59dca","flower"),parent)]
}

// bare stalks separate terminal spikes; tiny, spaced florets taper toward each apex.
pub fn lavender(k,recipe,order,angle) map {
    let shoots = [for (i in 0 to (order+2),let key = rng.at(k,i),let a = float(i)*2.4,
        let reach = if (i == 0) 0.05 else (0.55+float(angle)*0.018)*rng.between(key,0.85,1.15),
        let height = if (i == 0) 3.1 else rng.between(rng.derive(key,"height"),1.9,2.9),
        let root = [0.12*cos(a),0.0,0.12*sin(a)],let tip = [reach*cos(a),height,reach*sin(a)],
        let mid = [tip[0]*0.35,height*0.55,tip[2]*0.35])
        {root:root,mid:mid,tip:tip,rotation:stem(mid,tip,0.02,recipe.stem).rotation,key:key}]
    let stalks = [for (shoot in shoots) for (pair in [[shoot.root,shoot.mid],[shoot.mid,shoot.tip]])
        stem(pair[0],pair[1],0.018,recipe.stem)]
    let leaves = [for (shoot in shoots) for (j in 0 to 3,let t = 0.1+float(j)*0.11)
        for (side in [-1.0,1.0],let a = math.atan2(shoot.tip[2],shoot.tip[0])+side*1.0,
            let p = camera.add(camera.times(shoot.tip,t),[0.12*cos(a),0.07,0.12*sin(a)]))
            oval(p,[0.23-float(j)*0.025,0.018,0.032],recipe.leaf,"leaf",[0.0,-a,0.35])]
    let flowers = [for (shoot in shoots) *lavender_spike(shoot,shoot.key,recipe)];
    {recipe:recipe,sequence:"",segments:[for (part in stalks) [part.a[0],part.a[1],part.b[0],part.b[1],0,0.036]],leaves:[],
     tips:[for (shoot in shoots) {x:shoot.tip[0],y:shoot.tip[1],z:shoot.tip[2],phase:0.0}],
     solids:[*stalks,*leaves,*flowers],petal:recipe.petal}
}

pub fn spatial(x,y,turn,scale) array => [x*cos(turn)*scale,y*scale,-x*sin(turn)*scale]

pub fn branch_scale(plant) float => 3.8/(max([for (s in plant.segments) max(s[1],s[3])])+3.0)

pub fn whorls(plant) int => if (plant.solids == null) 3 else 1

// whorls grow around the Y axis; stems, leaves and flowers have real depth.
fn branches(plant,show_leaves,show_blooms) array {
    let scale = branch_scale(plant);
    [for (whorl in 0 to (whorls(plant)-1),let turn = float(whorl)*2.0*math.pi/float(whorls(plant)))
        *[*[for (s in plant.segments where whorl == 0 or s[4] > 0)
            stem(spatial(s[0],s[1],turn,scale),spatial(s[2],s[3],turn,scale),max(0.014,s[5]*0.09*scale),plant.recipe.stem)],
          *(if (show_leaves) [for (l in plant.leaves)
              oval(spatial(l.x,l.y,turn,scale),[l.size*2.7*scale,0.04,l.size*0.8*scale],plant.recipe.leaf,"leaf",
                  [0.0,turn,-l.angle*math.pi/180.0])] else []),
          *(if (show_blooms and plant.recipe.petals > 0) [for (tip in plant.tips)
              *flower(spatial(tip.x,tip.y,turn,scale),plant.recipe,plant.petal,tip.bloom,turn)] else [])]]
}

pub fn pieces(plant,show_leaves=true,show_blooms=true) array =>
    if (plant.solids == null) branches(plant,show_leaves,show_blooms)
    else [for (p in plant.solids where (show_leaves or not contains(["leaf","spine","spot"],p.role)) and (show_blooms or p.role != "flower")) p]

// reserve every organ when fitting, so hiding decorations never zooms the model.
pub fn bounds(parts) map {
    let extents = [for (p in parts,let m = matrix(p),let box = bloom.local_box(p.shape))
        if (box != null) {p:transforms.point(m,box.center)^,
            r:[for (axis in 0 to 2) sum([for (column in 0 to 2) abs(m[column*4+axis])*box.radius[column]])]}
        else {p:p.position,r:[for (axis in 0 to 2) sqrt(sum([for (column in 0 to 2) m[column*4+axis]**2]))]}]
    let lo = [for (axis in 0 to 2) min([for (e in extents) e.p[axis]-e.r[axis]])]
    let hi = [for (axis in 0 to 2) max([for (e in extents) e.p[axis]+e.r[axis]])];
    {lo:lo,hi:hi,scale:4.6/max([for (axis in 0 to 2) hi[axis]-lo[axis]])}
}
