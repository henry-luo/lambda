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
import rng: lambda.procart.rng
import terrain: lambda.procart.terrain
import field: lambda.procart.field
import adapt: lambda.procart.adapt
import env: lambda.procart.environment
import herb: lambda.procart.herb
import pond: .mod_pond

let PLANT_FIELDS = ["rules","axiom","kind","stem","leaf","petals","petal","center","leaf_mode","leaf_size","order","angle"]
let FIELDS = {flower:PLANT_FIELDS,tree:[*PLANT_FIELDS,"canopy_radius","canopy_colors"],stone:["detail","radius","color"],
    grass:["clumps","blades","radius","height","width","bend","colors"]}

pub fn check_parameters(o) bool^ {
    let p = o.parameters
    let checked_fields = if (o.generator != "plant.herb" and any([for (k,v in p) not contains(FIELDS[o.kind],string(k))]))
        raise error("procart meadow: unknown parameter on "++o.id);
    if (o.generator == "plant.herb") { let checked=herb.normalize(p)^; if (o.kind != (if (checked.species=="reed") "grass" else "flower")) raise error("procart meadow: herb species and tag disagree") else true }
    else if (o.kind == "grass") grass.validate(p)^
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

pub fn specimen(o,climate=null) map^ {
    let p = o.parameters;
    if (o.generator == "plant.herb") herb.generate(o.key,p,climate,o.matrix)^
    else if (o.kind == "grass") {parts:grass.generate(o.key,p)^,resources:[]}
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
    let aquatic=o.parameters.species == "water-lily"
    let anchored = if (aquatic and ground.pond != null) ground.pond.level else elevation(ground,origin[0],origin[2]);
    [for (p in parts,let world = solid.placed(p,o.matrix),
        let lift = if (o.kind == "grass") elevation(ground,world.position[0],world.position[2]) else anchored
        where (aquatic or ground.pond == null or pond.distance(ground.pond,world.position[0],world.position[2])>1.18) and
            (o.kind != "grass" or not on_path(world.position[0],world.position[2],ground.path_width)))
        {*:solid.placed(world,t.matrix([0.0,lift,0.0],[0.0,0.0,0.0],[1.0,1.0,1.0])^),category:o.kind}]
}
fn assemble(objects,models,ground) array => [for (i,o in objects) {
    let origin=t.point(o.matrix,[0.0,0.0,0.0])^
    let aquatic=o.parameters.species == "water-lily"
    let root=[origin[0],if (aquatic and ground.pond != null) ground.pond.level else elevation(ground,origin[0],origin[2]),origin[2]]
    let parts=placed(o,models[i].parts,ground);
    {id:"plant-"++string(i),key:o.id,kind:o.kind,species:C.value(o.parameters.species,if (o.kind=="flower") o.recipe else o.kind),
     root:root,parts:parts,response:models[i].response}
}]
pub fn generate(doc) map^ {
    let objects = C.resolve(doc)^
    // admit every effective recipe before any geometry allocation.
    let checked_parameters = [for (o in objects) check_parameters(o)^]
    let expected_blades = sum([for (o in objects where o.kind == "grass" and o.generator != "plant.herb") o.parameters.clumps*o.parameters.blades])
    let checked_blades = if (expected_blades > 12000) raise error("procart meadow: grass exceeds 12000 blades")
    let path_width = C.value(doc.path_width,1.05)
    let checked_path = if (not C.bounded(path_width,0.0,3.0)) raise error("procart meadow: path width must be 0..3")
    let raw_height = terrain.heightmap(rng.derive(rng.key(doc.seed),"terrain"),C.value(doc.terrain,{width:33,height:25,octaves:3,island:0.35}))^
    let water=pond.from_document(doc)^
    let lake=if (water == null) null else {*:water,level:field.sample(raw_height,
        (water.position[0]/doc.width+0.5)*float(len(raw_height[0])-1),(water.position[1]/doc.depth+0.5)*float(len(raw_height)-1))*doc.relief-0.08}
    let height=if (lake == null) raw_height else [for (j,row in raw_height) [for (i,h in row)
        pond.height(lake,float(i)/float(len(row)-1)*doc.width-doc.width/2.0,float(j)/float(len(raw_height)-1)*doc.depth-doc.depth/2.0,h*doc.relief)/doc.relief]]
    let ground = {height:height,width:doc.width,depth:doc.depth,relief:doc.relief,path_width:path_width,pond:lake,bottom:min(-0.4,min(flatten(height))*doc.relief-0.2)}
    let climate=env.sample(env.from_document(doc)^)^
    let models = [for (o in objects) specimen(o,climate)^]
    let instances=assemble(objects,models,ground);
    {*:ground,seed:doc.seed,objects:objects,specimens:models,instances:instances,environment:climate,
     parts:[for (item in instances) *item.parts],resources:unique([for (m in models) *m.resources]),
     counts:map([for (kind in ["flower","tree","stone","grass"]) (kind,len([for (o in objects where o.kind == kind) o]))] )}
}
// resample poses from the same keys; terrain, placement and legacy architecture stay retained.
pub fn conditions(model,config) map^ {
    let climate=env.sample(config)^
    let models=[for (i,o in model.objects) if (o.generator=="plant.herb") specimen(o,climate)^ else model.specimens[i]]
    let instances=[for (i,o in model.objects) if (o.generator=="plant.herb")
        {*:assemble([o],[models[i]],model)[0],id:model.instances[i].id} else model.instances[i]];
    {*:model,environment:climate,specimens:models,instances:instances,parts:[for (item in instances) *item.parts]}
}

fn ground_mesh(model) element^ {
    let mesh = adapt.heightfield(model.height,{dx:model.width/float(len(model.height[0])-1),
        dz:model.depth/float(len(model.height)-1),height_scale:model.relief})^
    let positions = [for (i in 0 to (len(mesh.positions) div 3-1))
        *[mesh.positions[i*3]-model.width/2.0,mesh.positions[i*3+1],mesh.positions[i*3+2]-model.depth/2.0]]
    let colors = [for (i in 0 to (len(positions) div 3-1),let x = positions[i*3],let z = positions[i*3+2],
        let v = positions[i*3+1]/max(model.relief,0.001))
        *(if (model.pond != null and pond.distance(model.pond,x,z)<1.22) [0.57+v*0.04,0.54+v*0.04,0.37+v*0.04]
            else if (on_path(x,z,model.path_width)) [0.69+v*0.06,0.63+v*0.06,0.43+v*0.06] else [0.35+v*0.09,0.45+v*0.08,0.24+v*0.05])];
    s.mesh(s.geometry(positions,{indices:mesh.indices,colors:colors}),s.material('lambert',{color:"#ffffff",side:'double'}),{'receive-shadow':true})
}

fn ground_sides(model) element {
    let h = len(model.height),w = len(model.height[0])
    let border = [*[for (x in 0 to (w-1)) [x,0]],*[for (y in 1 to (h-1)) [w-1,y]],
        *[for (x in 0 to (w-2)) [w-2-x,h-1]],*[for (y in 0 to (h-3)) [0,h-2-y]]]
    let points = [for (p in border) [float(p[0])/float(w-1)*model.width-model.width/2.0,
        model.height[p[1]][p[0]]*model.relief,float(p[1])/float(h-1)*model.depth-model.depth/2.0]];
    s.mesh(s.geometry([for (i,a in points,let b = points[(i+1)%len(points)])
        *[*a,a[0],model.bottom,a[2],*b,*b,a[0],model.bottom,a[2],b[0],model.bottom,b[2]]]),
        s.material('lambert',{color:"#8a7757",side:'double'}))
}

pub fn home() map => camera.prepare({position:[26.0,21.0,29.0],target:[0.0,0.7,0.0],fov:40.0})
fn organism(item,climate) element {
    let local=env.rooted(item.parts,item.root);
    <group id:item.id,position:item.root,'data-species':item.species,
        'data-openness':string(if (item.response != null) round(item.response.openness*1000.0)/1000.0 else 1.0),
        *viewer.meshes(local,{'cast-shadow':true,'receive-shadow':true})>
}
fn atmosphere(model,motion) array {
    let e=model.environment
    let drops=[for (i in 0 to 95,let key=rng.at(rng.derive(rng.key(model.seed),"rain"),i))
        s.transform([rng.between(key,-model.width/2.0,model.width/2.0),rng.between(rng.at(key,1),0.0,9.0),
            rng.between(rng.at(key,2),-model.depth/2.0,model.depth/2.0)],[0.0,0.0,-0.12],[0.015,0.22,0.015])^];
    [*(if (e.weather.rain>0.0) [<group id:"meadow-rain",'data-weather':"rainy",
        <mesh geometry:"rain-drop",instances:drops,s.material('basic',{color:"#b0c9cf",opacity:0.5,transparent:true})>>,
        s.clip("rainfall",[s.track("meadow-rain.position",[0.0,1.0],[0.0,0.0,0.0,0.0,-9.0,0.0],'vector')],{duration:1.0,autoplay:motion})] else []),
     *(if (e.weather.fog>0.0) [for (i in 0 to 2)
        <mesh geometry:"round",position:[float(i-1)*6.0,0.4,0.0],scale:[9.0,0.8,13.0],'data-weather':"foggy",
            s.material('basic',{color:e.background,opacity:e.weather.fog*(0.12+e.config.humidity*0.12),transparent:true,side:'double'})>] else [])]
}
// camera snapshots belong to presentation; generation does not consume them.
fn scene_frame(model,children,lens) element {
    let e=model.environment;
    s.scene([s.camera({id:"meadow-camera",position:lens.position,target:lens.target,fov:lens.fov,far:160.0}),*children],
        {id:"meadow-scene",camera:"meadow-camera",width:1100,height:780,background:e.background,tabindex:"0",
         role:"img",'aria-label':"A living meadow with a pond, wildflowers, trees and grass. Drag to orbit.",
         'data-weather':e.config.weather,'data-time':string(e.config.time),
         style:"display:block;width:100%;height:auto;touch-action:none"})
}
pub fn viewport(model,visibility={},motion=true,lens=home()) element^ {
    let e=model.environment
    let visible=[for (item in model.instances where visibility[item.kind] != false and len(item.parts)>0) item]
    // calm plants batch by species; wind retains a separate root pivot for each specimen.
    let display=if (e.wind.speed>0.0) visible else [for (item in visible group by item.species as species into batch)
        {*:batch[0],root:[0.0,0.0,0.0],parts:[for (instance in batch) *instance.parts]}]
    let tracks=if (e.wind.speed<=0.0) [] else [for (item in visible where item.kind != "stone")
        env.wind_track(item.id,e,item.key,env.response(item.species,e).flexibility)];
    scene_frame(model,[
        *env.lights(e,20.0),
        s.resources([geometry.sphere("round",10,16),s.box([1.0,1.0,1.0],{id:"rain-drop"}),*botanical.resources(),grass.geometry(),*model.resources]),
        ground_mesh(model)^,ground_sides(model),
        *(if (model.pond != null and visibility.pond != false) pond.scene(model.pond) else []),
        <group id:"meadow-model",*[for (item in display) organism(item,e)]>,
        *(if (len(tracks)>0) [s.clip("meadow-wind",tracks,{duration:8.0,autoplay:motion})] else []),*atmosphere(model,motion)
    ],lens)
}

pub fn initial(doc) map^ => {document:doc,model:generate(doc)^,base_seed:doc.seed,step:0,base_environment:env.normalize(env.from_document(doc)^)^,
    camera:home(),motion:true,visibility:{flower:true,tree:true,stone:true,grass:true,pond:true}}

pub fn command(st,action) map^ {
    if (contains(["flower","tree","stone","grass","pond"],action))
        {*:st,visibility:{*:st.visibility,[action]:not st.visibility[action]}}
    else if (action == "motion") {*:st,motion:not st.motion}
    else if (action == "reset-environment") environment(st,st.base_environment)^
    else if (action == "next" or action == "previous") {
        let step = max(if (st.base_seed is int) -st.base_seed else 0,st.step+(if (action == "next") 1 else -1))
        let seed = if (st.base_seed is int) st.base_seed+step else if (step == 0) st.base_seed else st.base_seed++":"++string(step)
        let doc = <meadow *:{*:map(st.document),seed:seed},*content(st.document)>;
        {*:st,document:doc,model:generate(doc)^,step:step}
    }
    else st
}

// keep the source element editable and serializable alongside its generated model.
pub fn environment(st,config) map^ {
    let checked=env.normalize(config)^
    let document=<meadow *:map(st.document),*[*[for (n in content(st.document) where name(n) != 'environment') n],env.description(checked)^]>;
    {*:st,document:document,model:conditions(st.model,checked)^}
}
pub fn edit_environment(st,key,value) map^ {
    let current=st.model.environment.config
    let config=if (key=="weather") {*:current,weather:value,wind_speed:env.PRESETS[value].wind_speed,humidity:env.PRESETS[value].humidity}
        else if (key=="sun") {*:current,lighting:{*:current.lighting,sun:value}}
        else {*:current,[key]:value};
    environment(st,config)^
}

pub fn clock(hour) string {
    let minutes=int(round(hour*60.0))%1440,h=minutes div 60,m=minutes%60;
    (if (h<10) "0" else "")++string(h)++":"++(if (m<10) "0" else "")++string(m)
}
