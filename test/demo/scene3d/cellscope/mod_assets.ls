import s: lambda.scene3d
import dom
import data: .mod_data

let REMOTE = "https://raw.githubusercontent.com/cclank/cell-architecture-studio/1cab982e7a0f96af854a696430c0724707764358/public/"
// Centers come from the source accessor bounds; rotations follow the upstream gallery.
let models = {
    plant:{file:"assets/plant/plant.gltf",local:true,scale:3.2,rotation:[0.08,-1.42,-0.02],center:[0.0,0.0,0.0],textured:true},
    whiteBlood:{file:"white-blood-cell-user.glb",scale:3.18,rotation:[0.02,-0.18,0.0],
        center:[-0.000030517578125,-0.0002593994140625,0.0000457763671875],textured:true},
    // the deployed gallery uses this textured GLB; face its cutaway forward and fit the shared camera.
    muscle:{url:"https://assets.lanshuagent.com/models/muscle-cell-tripo-skeletal-fiber-textured-pbr.glb.bin",
        scale:5.5,rotation:[0.12,3.141592653589793-0.34,-0.03],
        center:[0.0014495849609375,-0.1654205322265625,0.000034332275390625],textured:true}
}

pub fn model(id) => models[id]
pub fn needs_load(viewport,id) => models[id] != null and
    dom.get_attribute(dom.query_selector(viewport,"#imported-specimen"),"data-model") != id
pub fn nodes(tree) => [tree,*[for (child in content(tree)) *nodes(child)]]
pub fn source(base,id) => if (models[id].local == true) url_resolve(base,models[id].file)
    else if (models[id].url != null) models[id].url else REMOTE ++ "models/" ++ models[id].file
pub fn thumbnail(id) => if (id == "whiteBlood") REMOTE ++ "cell-renders-transparent/white-blood.png"
    else "assets/" ++ id ++ ".png"

pub fn load(base,id) element^ {
    let asset = models[id]
    let loaded = s.load(source(base,id),{id:"cell-model",transform:{position:asset.center}})^;
    s.group([loaded],{id:"imported-model",rotation:asset.rotation,scale:[asset.scale,asset.scale,asset.scale]})
}

// DOM attributes accept JSON component arrays; HTML's formatter omits typed array attributes.
pn mount(parent,tree,texture_size) {
    let node = dom.create_element(dom.owner_document(parent),string(name(tree)))
    for (key,value in tree where key is string or key is symbol)
        dom.set_attribute(node,string(key),if (value is array) format(value,'json') else string(value))
    if (name(tree) == 'texture') dom.set_attribute(node,"max-size",string(texture_size))
    for (child in content(tree)) mount(node,child,texture_size)
    dom.append_child(parent,node)
}

pub pn ensure(viewport,base,id) {
    let slot = dom.query_selector(viewport,"#imported-specimen")
    let asset = models[id]
    dom.set_attribute(slot,"visible",string(asset != null))
    if (asset == null) { return "" }
    if (dom.get_attribute(slot,"data-model") == id) {
        return dom.get_attribute(slot,"data-error")
    }
    // Only the selected imported model occupies this viewport; camera changes retain its buffers.
    for (child in dom.child_nodes(slot)) dom.remove_child(slot,child)
    dom.set_attribute(slot,"data-model",id)
    var loaded = null
    var problem = ""
    load(base,id) ^ { problem = "Could not load " ++ data.cell(id).name ++ ". " ++ ^.message } ~ { loaded = ~ }
    if (loaded != null) {
        mount(slot,loaded,if (id == "whiteBlood") 1024 else 2048)
    }
    dom.set_attribute(slot,"data-error",problem)
    dom.set_attribute(viewport,"data-model-source",source(base,id))
    return problem
}
