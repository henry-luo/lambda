// literal art expands through bounded pure state, then lowers to existing native scenes (PG12).
import c: ._common
import a: .adjust
import p: .primitive
import rules: .rules
import rng: .rng
import s: lambda.scene3d
import native_model: lambda.scene3d.model
import t: lambda.scene3d.transform
pub let OBJECTS = ["tree","flower","stone","grass"]
pub let DEFAULTS = {max_depth:32,min_size:0.0,max_size:null,max_objects:10000,max_vertices:1000000,max_steps:200000,defaults:{}}
let CAPS = {max_depth:128,max_objects:100000,max_vertices:2000000,max_steps:2000000}
let IDENTITY = ["id","seed","key_path","count","spread"]
let RESERVED = ["procart","resources","recipe","geometry","rule","variant","group","repeat","use",*p.KINDS,*OBJECTS]
fn settings(raw) map^ {
    let input = c.value(raw,{})
    let checked_map = c.require(input is map and c.fields(input,["max_depth","min_size","max_size","max_objects","max_vertices","max_steps","defaults"]),"invalid global settings")^
    let cfg = {*:DEFAULTS,*:input}
    let checked_limits = c.require(all([for (key,cap in CAPS) c.bounded(cfg[key],if (string(key) == "max_depth") 0 else 1,cap,true)]),"global work limit exceeds host cap or has invalid type")^
    let checked_size = c.require(c.bounded(cfg.min_size,0.0,1000000.0) and (cfg.max_size == null or c.bounded(cfg.max_size,max(0.00000001,cfg.min_size),1000000.0)),"invalid frame size limits")^
    let checked_defaults = c.require(cfg.defaults is map and c.fields(cfg.defaults,p.KINDS),"unknown primitive default kind")^
    let checked_fields = [for (kind,values in cfg.defaults)
        c.require(values is map and c.fields(values,p.FIELDS[string(kind)]) and not contains(values,"points") and not contains(values,"geometry"),"invalid per-primitive defaults")^]
    let admitted_defaults = [for (kind,values in cfg.defaults where not contains(["triangle","mesh"],string(kind))) p.parameters(string(kind),<box>,cfg.defaults)^];
    cfg
}
fn handler(recipe,registry) map^ {
    let entry = registry[recipe.generator ++ "@" ++ string(recipe.version)]
    let checked = c.require(entry is map and entry.validate is function and entry.generate is function,"unregistered generator " ++ recipe.generator ++ " version " ++ string(recipe.version))^;
    entry
}
fn recipe_for(node,definitions) element^ {
    let recipe = rules.lookup(definitions,node.recipe,"recipe")^
    let checked = c.require(recipe.kind == string(name(node)),"recipe kind does not match semantic object")^;
    recipe
}
fn validate_list(nodes,defs,cfg,registry,depth=0) bool^ {
    let ids = [for (n in nodes where n.id != null) n.id]
    let checked_ids = c.require(len(unique(ids)) == len(ids),"duplicate sibling ID")^
    let checked_depth = c.require(depth <= 128,"authored nesting exceeds 128")^
    let checked_nodes = [for (n in nodes) validate_node(n,defs,cfg,registry,depth)^];
    true
}
fn validate_node(n,defs,cfg,registry,depth) bool^ {
    let checked_element = c.require(n is element,"expected a drawing element")^
    let kind = string(name(n))
    let extra = if (contains(p.KINDS,kind)) p.FIELDS[kind] else if (contains(OBJECTS,kind)) ["recipe","settings"] else if (kind == "repeat") ["step"] else []
    let checked_fields = c.require(c.fields(n,[*a.FIELDS,*IDENTITY,*extra]),"unknown attribute on " ++ kind)^
    let checked_id = c.require(n.id == null or c.identifier(n.id),"invalid node ID")^
    let checked_seed = c.require(n.seed == null or n.seed is int or n.seed is string,"seed must be integer/string")^
    let checked_path = c.require(n.key_path == null or (n.key_path is array and len(n.key_path) <= 8 and all([for (x in n.key_path) x is int or x is string])),"invalid key path")^
    let checked_count = c.require(c.bounded(c.value(n.count,1),1,cfg.max_steps,true),"count exceeds step budget")^
    let checked_spread = c.require(n.spread == null or (kind != "repeat" and t.vector(n.spread,2) and min(n.spread)>=0 and max(n.spread)<=1000000),"invalid scatter dimensions")^
    let checked_transform = a.matrix(n)^
    let checked_color = a.color_op(n)^
    let checked_children = c.require(contains(["group","repeat"],kind) or len(content(n)) == 0,"drawing leaves cannot contain children")^
    let checked_shape = if (contains(p.KINDS,kind)) {
        let params = p.parameters(kind,n,cfg.defaults)^;
        if (kind == "mesh") rules.lookup(defs,params.geometry,"geometry")^ else true
    }
    let checked_recipe = if (contains(OBJECTS,kind)) {
        let recipe = recipe_for(n,defs)^
        let checked_settings = c.require(n.settings == null or n.settings is map,"object settings must be map")^
        let entry = handler(recipe,registry)^;
        c.require(entry.validate({kind:kind,parameters:{*:recipe.parameters,*:c.value(n.settings,{})}})^,"registered generator rejected object parameters")^
    }
    let checked_rule = if (not contains(["group","repeat",*p.KINDS,*OBJECTS],kind)) rules.lookup(defs,kind,"rule")^
    let checked_step = if (kind == "repeat") {
        let checked = c.require(n.count != null and len(content(n))>0 and (n.step == null or (n.step is map and c.fields(n.step,a.FIELDS))),"repeat needs count, a body and adjustment-only step")^
        let step_matrix = a.matrix(c.value(n.step,{}))^
        let step_color = a.color_op(c.value(n.step,{}))^;
        true
    }
    let checked_descendants = if (contains(["group","repeat"],kind)) validate_list(content(n),defs,cfg,registry,depth+1)^;
    true
}
pub fn normalize(doc,registry={}) map^ {
    let checked_root = c.require(doc is element and name(doc) == 'procart' and doc.version == 2 and (doc.seed is int or doc.seed is string),"expected procart version 2 with seed")^
    let checked_fields = c.require(c.fields(doc,["version","seed","settings",*a.FIELDS]),"unknown document attribute")^
    let cfg = settings(doc.settings)^
    let root_adjustment = a.compose(a.identity(),doc)^
    let blocks = [for (x in content(doc) where x is element and name(x) == 'resources') x]
    let checked_blocks = c.require(len(blocks)==1,"expected one resources block")^
    let defs = content(blocks[0])
    let checked_resources = c.require(len(defs)<=256 and c.fields(blocks[0],[]),"invalid resources block")^
    let checked_defs = c.require(all([for (d in defs) d is element and c.identifier(rules.reference(d))]) and
        len(unique([for (d in defs) rules.reference(d)]))==len(defs),"invalid or duplicate resource name/ID")^
    let checked_definitions = [for (d in defs) {
        let kind = string(name(d))
        let checked_kind = c.require(contains(["rule","recipe","geometry"],kind) and not contains(RESERVED,rules.reference(d)),"invalid/reserved resource definition")^;
        if (kind == "recipe") {
            let checked = c.require(c.fields(d,["id","kind","generator","version","parameters"]) and contains(OBJECTS,d.kind) and d.generator is string and c.bounded(d.version,1,1000000,true) and d.parameters is map and len(content(d))==0,"invalid recipe definition")^
            let entry = handler(d,registry)^;
            c.require(entry.validate({kind:d.kind,parameters:d.parameters})^,"registered generator rejected recipe parameters")^
        } else if (kind == "geometry") {
            let checked = c.require(c.fields(d,["id","type","positions","normals","indices","uvs","colors","tangents"]) and string(d.type) == "buffer" and len(content(d))==0,"invalid prototype geometry")^;
            native_model.check_geometry(<geometry *:{*:map(d),type:'buffer'}>)^
        } else {
            let variants = content(d)
            let checked = c.require(c.fields(d,["name"]) and rules.valid_name(d.name) and len(variants)>0 and len(variants)<=256 and len(unique(variants |> ~.name))==len(variants),"invalid rule definition")^
            let bodies = [for (v in variants) {
                let checked_variant = c.require(v is element and name(v)=='variant' and c.identifier(v.name) and c.fields(v,["name","weight"]) and c.bounded(c.value(v.weight,1.0),0.000000001,1000000.0) and len(content(v))>0,"invalid rule variant")^;
                validate_list(content(v),defs,cfg,registry)^
            }]; true
        }
    }]
    let nodes = [for (x in content(doc) where not (x is element and name(x)=='resources')) x]
    let checked_nonempty = c.require(len(nodes)>0,"document needs drawing nodes")^
    let checked_nodes = validate_list(nodes,defs,cfg,registry)^;
    {document:doc,settings:cfg,definitions:defs,nodes:nodes,root:root_adjustment}
}
fn visit(acc,cfg) map^ {
    let checked = c.require(acc.steps < cfg.max_steps,"max_steps exceeded")^;
    {*:acc,steps:acc.steps+1}
}
// shared prototypes count once; admit new buffers before allocating their vertices.
fn reserve(acc,id,vertices,cfg) bool^ => c.require((acc.geometries[id] != null or acc.vertices+vertices <= cfg.max_vertices),"max_vertices exceeded")^
fn add_geometry(acc,id,geometry,vertices,cfg) map^ {
    let checked = reserve(acc,id,vertices,cfg)^
    let checked_geometry = native_model.check_geometry(geometry)^;
    if (acc.geometries[id] != null) {
        let collision = c.require(acc.geometries[id] == geometry,"geometry identity collision")^;
        acc
    } else ({*:acc,geometries:{*:acc.geometries,[id]:geometry},vertices:acc.vertices+vertices})
}
fn emit(acc,part,cfg) map^ {
    let checked = c.require(len(acc.parts) < cfg.max_objects,"max_objects exceeded")^;
    {*:acc,parts:[*acc.parts,part]}
}
fn primitive(n,ctx,acc,env) map^ {
    let kind = string(name(n)), params = p.parameters(kind,n,env.settings.defaults)^
    let id = if (kind == "mesh") "prototype-" ++ params.geometry else "primitive-" ++ string(hash(format([kind,params],'json')^,'sha256'))
    let with_geometry = if (kind == "mesh") {
        let prototype = rules.lookup(env.definitions,params.geometry,"geometry")^;
        add_geometry(acc,id,<geometry *:{*:map(prototype),id:id,type:'buffer'}>,len(prototype.positions) div 3,env.settings)^
    } else if (acc.geometries[id] != null) acc else {
        let vertices = p.estimate(kind,params)
        let checked = reserve(acc,id,vertices,env.settings)^;
        add_geometry(acc,id,p.geometry(kind,params,id),vertices,env.settings)^
    }
    let paint = a.paint(ctx.colors)^;
    emit(with_geometry,{id:ctx.path,key:ctx.key,kind:kind,role:kind,geometry:id,matrix:ctx.matrix,*:paint},env.settings)^
}
fn semantic(n,ctx,acc,env) map^ {
    let recipe = recipe_for(n,env.definitions)^
    let entry = handler(recipe,env.registry)^
    let model = entry.generate({id:ctx.path,kind:string(name(n)),key:rng.derive(ctx.key,"model"),parameters:{*:recipe.parameters,*:c.value(n.settings,{})}})^
    let checked = c.require(model.parts is array and model.resources is array and len(acc.parts)+len(model.parts)<=env.settings.max_objects,"semantic output exceeds object budget or is invalid")^
    let definitions = reduce([acc,*model.resources],(st,g) => add_geometry(st,g.id,g,if (string(g.type) == "box") 24 else len(g.positions) div 3,env.settings)^);
    reduce([definitions,*[for (i,part in model.parts) {part:part,index:i}]],(st,item) => {
        let part = item.part
        let paint = a.paint(ctx.colors,part.color)^
        let matrix = t.multiply(ctx.matrix,(if (part.matrix != null) part.matrix else t.matrix(part.position,part.rotation,part.scale)^))^
        let opacity=c.value(part.opacity,1.0)
        let checked = c.require(st.geometries[part.shape] != null and t.affine(matrix) and c.bounded(opacity,0.0,1.0),"semantic generator returned missing geometry, invalid transform or opacity")^;
        // intrinsic organ alpha multiplies inherited paint, including registered plant palettes.
        emit(st,{id:ctx.path++"/organ-"++string(item.index),key:ctx.key,kind:string(name(n)),role:part.role,geometry:part.shape,matrix:matrix,*:paint,
            opacity:paint.opacity*opacity},env.settings)^
    })
}
fn walk_list(nodes,ctx,acc,env) map^ => reduce([acc,*[for (i,node in nodes) {node:node,index:i}]],(st,item) => walk(item.node,item.index,ctx,st,env)^)
fn draw(n,ctx,acc,env) map^ {
    let kind = string(name(n)), cfg = env.settings
    let frame_size = a.frame_size(ctx.matrix)
    let retired = frame_size < cfg.min_size or (cfg.max_size != null and frame_size > cfg.max_size);
    if (retired) {*:acc,size_retired:acc.size_retired+1}
    else (let valid_frame = c.require(t.affine(ctx.matrix),"expanded frame exceeds native affine range")^,
    if (kind == "group") walk_list(content(n),ctx,acc,env)^
    else if (kind == "repeat") {
        let result = reduce([{acc:acc,ctx:ctx},*[for (i in 0 to (n.count-1)) i]],(st,i) => {
            let counted = visit(st.acc,cfg)^
            let body = {*:st.ctx,key:rng.at(ctx.key,i),path:ctx.path++"/repeat-"++string(i)}
            let next = walk_list(content(n),body,counted,env)^;
            {acc:next,ctx:if (i+1 < n.count) {*:st.ctx,*:a.compose(st.ctx,c.value(n.step,{}))^} else st.ctx}
        }); result.acc
    } else if (contains(p.KINDS,kind)) primitive(n,ctx,acc,env)^
    else if (contains(OBJECTS,kind)) semantic(n,ctx,acc,env)^
    else {
        if (ctx.depth >= cfg.max_depth) {*:acc,depth_retired:acc.depth_retired+1}
        else {
            let chosen = rules.choose(rules.lookup(env.definitions,kind,"rule")^,ctx.key)
            let body = {*:ctx,key:rng.derive(ctx.key,chosen.name),path:ctx.path++"/"++chosen.name,depth:ctx.depth+1};
            walk_list(content(chosen),body,acc,env)^
        }
    })
}
fn walk(n,index,parent,acc,env) map^ {
    let counted = visit(acc,env.settings)^
    let adjusted = a.compose(parent,n)^
    let key = rules.key(parent.key,n,index)
    let base = {*:parent,*:adjusted,key:key,path:parent.path++"/"++c.value(n.id,string(index))}
    let copies = if (name(n)=='repeat') 1 else c.value(n.count,1);
    reduce([counted,*[for (i in 0 to (copies-1)) i]],(st,i) => {
        // index even the first copy so increasing count preserves every existing stream.
        let k = rng.at(key,i), spread = c.value(n.spread,[0.0,0.0]), place = rng.derive(k,"placement")
        let offset = t.matrix([rng.between(place,-spread[0]/2.0,spread[0]/2.0),0.0,rng.between(rng.at(place,1),-spread[1]/2.0,spread[1]/2.0)],
            [0.0,if (n.spread != null) rng.between(rng.at(place,2),0.0,2.0*math.pi) else 0.0,0.0],[1.0,1.0,1.0])^
        let ctx = {*:base,key:k,path:base.path++"-"++string(i),matrix:t.multiply(base.matrix,offset)^};
        draw(n,ctx,visit(st,env.settings)^,env)^
    })
}
pub fn expand(doc,registry={}) map^ {
    let normalized = normalize(doc,registry)^
    let env = {*:normalized,registry:registry}
    let ctx = {*:normalized.root,key:rng.key(doc.seed),path:"art",depth:0}
    let result = walk_list(normalized.nodes,ctx,{parts:[],geometries:{},vertices:0,steps:0,depth_retired:0,size_retired:0},env)^;
    {resources:[for (id,g in result.geometries) g],parts:result.parts,seed:doc.seed,
        stats:{objects:len(result.parts),vertices:result.vertices,steps:result.steps,depth_retired:result.depth_retired,size_retired:result.size_retired},settings:normalized.settings}
}
pub fn meshes(model,options={}) array => [for (part in model.parts group by [part.geometry,part.color,part.opacity,part.kind,part.role] as key into batch)
    <mesh *:options,geometry:batch[0].geometry,instances:[for (p in batch) p.matrix],
        'data-kind':batch[0].kind,'data-organ':batch[0].role,'data-count':string(len(batch)),
        'data-source-ids':join(batch |> ~.id," "),s.material('lambert',{color:batch[0].color,opacity:batch[0].opacity,transparent:batch[0].opacity<1.0,side:'double'})>]
pub fn to_scene3d(model,presentation={}) element {
    let lens={id:"procart-camera",position:c.value(presentation.position,[8.0,6.0,10.0]),target:c.value(presentation.target,[0.0,0.0,0.0]),fov:c.value(presentation.fov,42.0),*:c.value(presentation.camera,{})}
    let lights=c.value(presentation.lights,[s.light('ambient',{color:"#ffffff",intensity:0.65}),s.light('directional',{color:"#fff0da",position:[-4.0,10.0,8.0],intensity:0.85})]);
    s.scene([s.camera(lens),*lights,s.resources(model.resources),s.group(meshes(model,c.value(presentation.mesh_options,{})),{id:"procart-model"})],
        {id:c.value(presentation.id,"procart-scene"),camera:lens.id,width:c.value(presentation.width,1000),height:c.value(presentation.height,650),background:c.value(presentation.background,"#eeeae0"),
         tabindex:"0",style:"display:block;width:100%;height:auto;touch-action:none"})
}
