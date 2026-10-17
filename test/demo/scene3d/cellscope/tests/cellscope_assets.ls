import s: lambda.scene3d
import assets: ~~.mod_assets
import data: ~~.mod_data

// Golden batches have no entry-script argv; fixtures resolve from the harness working directory.
let base = "file://" ++ sys.proc.self.cwd# ++ "/test/demo/scene3d/cellscope/"
let plant = assets.load(base,"plant")^
let source = input(base ++ "assets/plant/plant.gltf",'json')^
let attributes = source.meshes[0].primitives[0].attributes
let position = source.accessors[attributes.POSITION]
let normal = source.accessors[attributes.NORMAL]
let uv = source.accessors[attributes.TEXCOORD_0]
let all_nodes = assets.nodes(plant)
let geometry = ([for (n in all_nodes where name(n) == 'geometry') n])[0]
let textures = [for (n in all_nodes where name(n) == 'texture') n]

{
    valid:s.validate(s.scene([s.camera({id:"camera"}),plant],{camera:"camera"}))^,
    local_models:len([for (c in data.cells where assets.model(c.id).local == true) c]),
    remote_models:len([for (c in data.cells where assets.model(c.id) != null and assets.model(c.id).local != true) c]),
    pinned_https:all([for (id in ["whiteBlood"])
        starts_with(assets.source(base,id),"https://raw.githubusercontent.com/") and
        contains(assets.source(base,id),"1cab982e7a0f96af854a696430c0724707764358") and ends_with(assets.source(base,id),".glb")]),
    deployed_muscle:assets.source(base,"muscle") == "https://assets.lanshuagent.com/models/muscle-cell-tripo-skeletal-fiber-textured-pbr.glb.bin"
        and assets.model("muscle").textured == true,
    vertices:len(geometry.positions) div 3,
    triangles:len(geometry.indices) div 3,
    buffer_budget:source.buffers[0].byteLength < 250000,
    quantized:contains(source.extensionsRequired,"KHR_mesh_quantization") and
        position.componentType == 5122 and position.normalized == true and
        normal.componentType == 5120 and normal.normalized == true,
    compact_uvs:uv.componentType == 5123 and uv.normalized == true,
    textured:len(textures) == 1 and ends_with(textures[0].src,"/assets/plant/plant-basecolor.jpg"),
    uv_complete:len(geometry.uvs)*3 == len(geometry.positions)*2,
    normals_complete:len(geometry.normals) == len(geometry.positions)
}
