// semantic objects share botanical/mineral generators and native scene data (PG11).
import C: .mod_composition
import grass: .mod_grass
import botany: .mod_plants
import botanical: .mod_botanical3d
import rocks: .mod_minerals
import solid: .mod_plant_solids
import viewer: .mod_scene_view
import geometry: ~~.scene3d._geometry
import s: lambda.scene3d
import t: lambda.scene3d.transform
import camera: lambda.scene3d.camera
import rng: lambda.procgen.rng
import terrain: lambda.procgen.terrain
import field: lambda.procgen.field
import adapt: lambda.procgen.adapt

let PLANT_FIELDS = ["rules","axiom","kind","stem","leaf","petals","petal","center","leaf_mode","leaf_size","order","angle"]
let FIELDS = {flower:PLANT_FIELDS,tree:[*PLANT_FIELDS,"canopy_radius","canopy_colors"],stone:["detail","radius","color"],
    grass:["clumps","blades","radius","height","width","bend","colors"]}

fn check_parameters(o) bool^ {
    let p = o.parameters
    let checked_fields = if (any([for (k,v in p) not contains(FIELDS[o.kind],string(k))]))
        raise error("procart meadow: unknown parameter on "++o.id);
    if (o.kind == "grass") grass.validate(p)^
    else if (o.kind == "stone") {
        if (not C.bounded(p.detail,1,4,true) or not C.bounded(p.radius,0.1,2.0) or not (p.color is string))
            raise error("procart meadow: stone needs detail 1..4, radius 0.1..2 and a color") else true
    }
    else {
        if (not C.bounded(p.order,1,4,true) or not C.bounded(p.angle,10,45) or not C.bounded(p.petals,0,20,true) or
            not (p.stem is string) or not (p.leaf is string) or
            (p.kind != null and p.kind != "lavender") or (p.kind == null and not (p.rules is map)) or
            (p.axiom != null and not (p.axiom is string)) or
            (p.leaf_mode != null and not contains(["all","alternate"],p.leaf_mode)) or
            (p.leaf_size != null and not C.bounded(p.leaf_size,0.05,2.0)) or
            (p.petal != null and not (p.petal is string)) or (p.center != null and not (p.center is string)) or
            (o.kind == "tree" and (p.petals != 0 or p.kind != null or not C.bounded(p.canopy_radius,0.15,1.0) or
                not (p.canopy_colors is array) or len(p.canopy_colors) < 1 or len(p.canopy_colors) > 4)))
            raise error("procart meadow: invalid plant recipe on "++o.id) else true
    }
}

fn tree_parts(plant,k) array {
    let scale = solid.branch_scale(plant),recipe = plant.recipe;
    [*solid.pieces(plant,false,false),
     *[for (whorl in 0 to (solid.whorls(plant)-1),let turn = float(whorl)*2.0*math.pi/float(solid.whorls(plant)))
        for (i,tip in plant.tips,let key = rng.at(rng.derive(k,whorl),i),let r = recipe.canopy_radius*rng.between(key,0.8,1.2))
            solid.oval(solid.spatial(tip.x,tip.y,turn,scale),[r*1.15,r*0.8,r],
                rng.pick(rng.at(key,1),recipe.canopy_colors),"canopy")]]
}

fn specimen(o) map^ {
    let p = o.parameters;
    if (o.kind == "grass") {parts:grass.generate(o.key,p)^,resources:[]}
    else if (o.kind == "stone") {
        let definition = rocks.rock(o.key,p.detail,"rock-"++o.id);
        {parts:[solid.oval([0.0,p.radius*0.65,0.0],[p.radius,p.radius*0.8,p.radius],p.color,"stone",[0.0,0.0,0.0],definition.tree.id)],
         resources:[definition.tree]}
    }
    else {
        let plant = botany.grow(o.key,p,p.order,p.angle)^;
        {parts:if (o.kind == "tree") tree_parts(plant,o.key) else solid.pieces(plant),resources:[]}
    }
}

pub fn elevation(model,x,z) float => field.sample(model.height,
    (x/model.width+0.5)*float(len(model.height[0])-1),(z/model.depth+0.5)*float(len(model.height)-1))*model.relief

fn on_path(x,z,width) bool => abs(x-0.9*sin(z*0.45)) < width/2.0

fn placed(o,parts,ground) array {
    let origin = t.point(o.matrix,[0.0,0.0,0.0])^
    let anchored = elevation(ground,origin[0],origin[2]);
    [for (p in parts,let world = solid.placed(p,o.matrix),
        let lift = if (o.kind == "grass") elevation(ground,world.position[0],world.position[2]) else anchored
        where o.kind != "grass" or not on_path(world.position[0],world.position[2],ground.path_width))
        {*:solid.placed(world,t.matrix([0.0,lift,0.0],[0.0,0.0,0.0],[1.0,1.0,1.0])^),category:o.kind}]
}

pub fn generate(doc) map^ {
    let objects = C.resolve(doc)^
    // admit every effective recipe before any geometry allocation.
    let checked_parameters = [for (o in objects) check_parameters(o)^]
    let expected_blades = sum([for (o in objects where o.kind == "grass") o.parameters.clumps*o.parameters.blades])
    let checked_blades = if (expected_blades > 12000) raise error("procart meadow: grass exceeds 12000 blades")
    let path_width = C.value(doc.path_width,1.05)
    let checked_path = if (not C.bounded(path_width,0.0,3.0)) raise error("procart meadow: path width must be 0..3")
    let height = terrain.heightmap(rng.derive(rng.key(doc.seed),"terrain"),C.value(doc.terrain,{width:33,height:25,octaves:3,island:0.35}))^
    let ground = {height:height,width:doc.width,depth:doc.depth,relief:doc.relief,path_width:path_width}
    let models = [for (o in objects) specimen(o)^]
    let parts = [for (i,o in objects) *placed(o,models[i].parts,ground)];
    {*:ground,seed:doc.seed,objects:objects,parts:parts,resources:[for (m in models) *m.resources],
     counts:map([for (kind in ["flower","tree","stone","grass"]) (kind,len([for (o in objects where o.kind == kind) o]))] )}
}

fn ground_mesh(model) element^ {
    let mesh = adapt.heightfield(model.height,{dx:model.width/float(len(model.height[0])-1),
        dz:model.depth/float(len(model.height)-1),height_scale:model.relief})^
    let positions = [for (i in 0 to (len(mesh.positions) div 3-1))
        *[mesh.positions[i*3]-model.width/2.0,mesh.positions[i*3+1],mesh.positions[i*3+2]-model.depth/2.0]]
    let colors = [for (i in 0 to (len(positions) div 3-1),let x = positions[i*3],let z = positions[i*3+2],
        let v = positions[i*3+1]/max(model.relief,0.001))
        *(if (on_path(x,z,model.path_width)) [0.69+v*0.06,0.63+v*0.06,0.43+v*0.06] else [0.35+v*0.09,0.45+v*0.08,0.24+v*0.05])];
    s.mesh(s.geometry(positions,{indices:mesh.indices,colors:colors}),s.material('lambert',{color:"#ffffff",side:'double'}),{'receive-shadow':true})
}

fn ground_sides(model) element {
    let h = len(model.height),w = len(model.height[0])
    let border = [*[for (x in 0 to (w-1)) [x,0]],*[for (y in 1 to (h-1)) [w-1,y]],
        *[for (x in 0 to (w-2)) [w-2-x,h-1]],*[for (y in 0 to (h-3)) [0,h-2-y]]]
    let points = [for (p in border) [float(p[0])/float(w-1)*model.width-model.width/2.0,
        model.height[p[1]][p[0]]*model.relief,float(p[1])/float(h-1)*model.depth-model.depth/2.0]];
    s.mesh(s.geometry([for (i,a in points,let b = points[(i+1)%len(points)])
        *[*a,a[0],-0.4,a[2],*b,*b,a[0],-0.4,a[2],b[0],-0.4,b[2]]]),
        s.material('lambert',{color:"#8a7757",side:'double'}))
}

pub fn home() map => camera.prepare({position:[17.0,13.5,19.0],target:[0.0,1.0,0.0],fov:40.0})

pub fn viewport(model,visibility={}) element^ {
    let lens = home()
    let parts = [for (p in model.parts where visibility[p.category] != false) p];
    s.scene([
        s.camera({id:"meadow-camera",position:lens.position,target:lens.target,fov:lens.fov,far:100.0}),
        s.light('hemisphere',{color:"#fff7dc",'ground-color':"#7a885e",intensity:0.8}),
        s.light('directional',{position:[-7.0,14.0,8.0],color:"#fff1c9",intensity:1.15,
            'cast-shadow':true,'shadow-map-size':2048,'shadow-extent':13.0,'shadow-intensity':0.4,'shadow-softness':2.0}),
        s.light('ambient',{color:"#dbe5ce",intensity:0.3}),
        s.resources([geometry.sphere("round",10,16),*botanical.resources(),grass.geometry(),*model.resources]),
        ground_mesh(model)^,ground_sides(model),
        <group id:"meadow-model",*viewer.meshes(parts,{'cast-shadow':true,'receive-shadow':true})>
    ],{id:"meadow-scene",camera:"meadow-camera",width:1000,height:650,background:"#e5e9d7",tabindex:"0",
        role:"img",'aria-label':"A seeded meadow of trees, wildflowers, stones and grass. Drag to orbit.",
        style:"display:block;width:100%;height:auto;touch-action:none"})
}

pub fn initial(doc) map^ => {document:doc,model:generate(doc)^,base_seed:doc.seed,step:0,
    visibility:{flower:true,tree:true,stone:true,grass:true}}

pub fn command(st,action) map^ {
    if (contains(["flower","tree","stone","grass"],action))
        {*:st,visibility:{*:st.visibility,[action]:not st.visibility[action]}}
    else if (action == "next" or action == "previous") {
        let step = max(if (st.base_seed is int) -st.base_seed else 0,st.step+(if (action == "next") 1 else -1))
        let seed = if (st.base_seed is int) st.base_seed+step else if (step == 0) st.base_seed else st.base_seed++":"++string(step)
        let doc = <meadow *:{*:map(st.document),seed:seed},*content(st.document)>;
        {*:st,document:doc,model:generate(doc)^,step:step}
    }
    else st
}
