import s: lambda.scene3d
import assets: ~~.mod_assets
import data: ~~.mod_data
import colors: ~~.mod_colors

// Golden batches have no entry-script argv; fixtures resolve from the harness working directory.
let base = "file://" ++ sys.proc.self.cwd# ++ "/test/demo/cellscope/"
let plant = assets.load(base,"plant")^
let source = input(base ++ "assets/plant/plant.gltf",'json')^
let attributes = source.meshes[0].primitives[0].attributes
let position = source.accessors[attributes.POSITION]
let normal = source.accessors[attributes.NORMAL]
let uv = source.accessors[attributes.TEXCOORD_0]
let all_nodes = assets.nodes(plant)
let geometry = ([for (n in all_nodes where name(n) == 'geometry') n])[0]
let textures = [for (n in all_nodes where name(n) == 'texture') n]
let points = [0.0,0.0,0.0,1.0,0.5,0.3,0.5,1.0,1.0]
let tinted = colors.vertices({positions:points},data.cell("bacteria"))^
// independently evaluated upstream Three.js colors, converted to display RGB for Scene3D.
let reference = [0.3914216291858563,0.710818515189163,0.6752011773744255,
    0.36639658148627197,0.6943947293049545,0.603825437333504,
    0.5467341007798414,0.7480735352166249,0.6637709247505714]
let shifted = colors.vertices({positions:[for (p in points) p+10.0]},data.cell("bacteria"))^;
let chunks = [{positions:slice(points,0,3)},{positions:slice(points,3,len(points))}]
let extent = colors.bounds(chunks)
let chunked = [for (chunk in chunks) *colors.vertices(chunk,data.cell("bacteria"),extent)^];
{
    valid:s.validate(s.scene([s.camera({id:"camera"}),plant],{camera:"camera"}))^,
    local_models:len([for (c in data.cells where assets.model(c.id).local == true) c]),
    remote_models:len([for (c in data.cells where assets.model(c.id) != null and assets.model(c.id).local != true) c]),
    pinned_https:all([for (id in ["whiteBlood","neuron","bacteria","animal"])
        starts_with(assets.source(base,id),"https://raw.githubusercontent.com/") and
        contains(assets.source(base,id),"1cab982e7a0f96af854a696430c0724707764358") and ends_with(assets.source(base,id),".glb")]),
    vertices:len(geometry.positions) div 3,
    triangles:len(geometry.indices) div 3,
    buffer_budget:source.buffers[0].byteLength < 250000,
    quantized:contains(source.extensionsRequired,"KHR_mesh_quantization") and
        position.componentType == 5122 and position.normalized == true and
        normal.componentType == 5120 and normal.normalized == true,
    compact_uvs:uv.componentType == 5123 and uv.normalized == true,
    textured:len(textures) == 1 and ends_with(textures[0].src,"/assets/plant/plant-basecolor.jpg"),
    uv_complete:len(geometry.uvs)*3 == len(geometry.positions)*2,
    normals_complete:len(geometry.normals) == len(geometry.positions),
    upstream_colors:len(tinted) == len(points) and all([for (i,c in tinted) abs(c-reference[i]) < 0.00001]),
    local_bounds:all([for (i,c in tinted) abs(c-shifted[i]) < 0.000001]),
    chunk_seams:all([for (i,c in tinted) abs(c-chunked[i]) < 0.000001])
}
