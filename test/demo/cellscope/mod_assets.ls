import s: lambda.scene3d
import dom
import data: .mod_data
import colors: .mod_colors

let REMOTE = "https://raw.githubusercontent.com/cclank/cell-architecture-studio/1cab982e7a0f96af854a696430c0724707764358/public/"
// Centers come from the source accessor bounds; rotations follow the upstream gallery.
let models = {
    plant:{file:"assets/plant/plant.gltf",local:true,scale:3.2,rotation:[0.08,-1.42,-0.02],center:[0.0,0.0,0.0],textured:true},
    whiteBlood:{file:"white-blood-cell-user.glb",scale:3.18,rotation:[0.02,-0.18,0.0],
        center:[-0.000030517578125,-0.0002593994140625,0.0000457763671875],textured:true},
    neuron:{file:"neuron-nih.glb",scale:3.8,rotation:[0.18,-0.24,-0.18],
        center:[-0.04237180948257445,0.031718999147415175,-0.01617757888743654]},
    bacteria:{file:"bacteria-wall-nih.glb",scale:0.0015,rotation:[0.08,-0.44,-0.08],
        center:[-51.59130859375,-559.3240051269531,-168.43707275390625]},
    animal:{file:"animal-cell-nih.glb",scale:0.041,rotation:[0.24,-0.08,0.03],center:[-31.0,-1.0,-17.672500610351562]}
}

pub fn model(id) => models[id]
pub fn nodes(tree) => [tree,*[for (child in content(tree)) *nodes(child)]]
pub fn source(base,id) => if (models[id].local == true) url_resolve(base,models[id].file)
    else REMOTE ++ "models/" ++ models[id].file
pub fn thumbnail(id) => if (contains(["neuron","bacteria","animal"],id))
    REMOTE ++ "nih-previews/" ++ replace(models[id].file,".glb",".png")
    else if (id == "whiteBlood") REMOTE ++ "cell-renders-transparent/white-blood.png"
    else "assets/" ++ id ++ ".png"

pub fn load(base,id) element^ {
    let asset = models[id]
    let loaded = s.load(source(base,id),{id:"cell-model",transform:{position:asset.center}})^;
    s.group([loaded],{id:"imported-model",rotation:asset.rotation,scale:[asset.scale,asset.scale,asset.scale]})
}

// DOM attributes accept JSON component arrays; HTML's formatter omits typed array attributes.
pn mount(parent,tree,cell,texture_size,extent) {
    let node = dom.create_element(dom.owner_document(parent),string(name(tree)))
    for (key,value in tree where key is string or key is symbol)
        dom.set_attribute(node,string(key),if (value is array) format(value,'json') else string(value))
    if (name(tree) == 'texture') dom.set_attribute(node,"max-size",string(texture_size))
    // the NIH meshes need upstream's generated vertex colors; white avoids multiplying by the tint twice.
    if (models[cell.id].textured != true) {
        if (name(tree) == 'geometry') dom.set_attribute(node,"colors",format(colors.vertices(tree,cell,extent)^,'json'))
        if (name(tree) == 'material') {
            dom.set_attribute(node,"color","#ffffff")
            dom.set_attribute(node,"side","double")
        }
    }
    for (child in content(tree)) mount(node,child,cell,texture_size,extent)
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
        // each pinned NIH asset has one source primitive; draw chunks must share its color coordinates.
        let extent = if (asset.textured == true) null else colors.bounds([for (n in nodes(loaded) where name(n) == 'geometry') n])
        mount(slot,loaded,data.cell(id),if (id == "whiteBlood") 1024 else 2048,extent)
    }
    dom.set_attribute(slot,"data-error",problem)
    dom.set_attribute(viewport,"data-model-source",source(base,id))
    return problem
}
