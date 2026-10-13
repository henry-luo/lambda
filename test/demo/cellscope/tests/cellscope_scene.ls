import s: lambda.scene3d
import math
import scene: ~~.mod_scene
import assets: ~~.mod_assets

fn triple(values,i) => [values[3*i],values[3*i+1],values[3*i+2]]
fn outward(mesh,i) {
    let a = triple(mesh.positions,mesh.indices[i])
    let u = triple(mesh.positions,mesh.indices[i+1])-a
    let v = triple(mesh.positions,mesh.indices[i+2])-a
    let face = [u[1]*v[2]-u[2]*v[1],u[2]*v[0]-u[0]*v[2],u[0]*v[1]-u[1]*v[0]]
    let normals = [for (axis in 0 to 2) sum([for (j in 0 to 2) mesh.normals[3*mesh.indices[i+j]+axis]])];
    math.dot(face,normals) >= -0.0000001
}

let viewport = scene.viewport("test-scene")^
let children = content(viewport)
let resources = content(([for (c in children where name(c) == 'resources') c])[0])
let groups = [for (c in children where name(c) == 'group') c]
let meshes = [for (r in resources where name(r) == 'geometry') r]
let materials = [for (r in resources where name(r) == 'material') r]
let muscle = assets.nodes(groups[1])
let bundles = [for (n in muscle where n['data-bundle'] != null) n]
let sarcomeres = [for (n in muscle where n['data-sarcomere'] == true) n]
let shell = ([for (n in meshes where n.id == "sarcolemma") n])[0]
let tube = ([for (n in meshes where n.id == "reticulum") n])[0]
{
    valid:s.validate(viewport)^,
    scene_slots:len(groups),
    selected:len([for (g in groups where g.visible) g]) == 1,
    shared_geometry:len(meshes),
    lazy_import:len(content(groups[0])) == 0,
    opaque_depth:all([for (m in materials) m.transparent != true and m['alpha-mode'] != 'blend']),
    unit_normals:all([for (m in meshes,i in 0 to (len(m.normals) div 3-1))
        abs(math.norm(triple(m.normals,i))-1.0) < 0.000001]),
    outward_winding:all([for (m in meshes,i in 0 to (len(m.indices) div 3-1)) outward(m,i*3)]),
    bundles:len(bundles),
    sarcomeres:len(sarcomeres),
    three_bands:all([for (n in sarcomeres) len(content(n)) == 3]),
    band_colors:len(unique([for (m in materials where m['data-part'] == "myofibril") m.color])) == 3,
    nuclei:len([for (n in muscle where n['data-nucleus'] == true) n]),
    mitochondria:len([for (n in muscle where n['data-mitochondrion'] == true) n]),
    reticulum_tubes:len([for (n in muscle where n.geometry == "reticulum") n]),
    capsule_extent:abs(max(shell.positions)-2.9) < 0.000001 and abs(min(shell.positions)+2.9) < 0.000001,
    tube_segments:len(tube.positions) == 81*13*3 and len(tube.indices) == 80*12*6,
    // reference vertices from Three.js TubeGeometry: start, middle, and end of the arc-length grid.
    upstream_curve:all([for (sample in [
        {i:0,p:[-2.0006866455078125,-0.7839824557304382,0.3097265660762787]},
        {i:520,p:[-1.9027398824691772,-0.08208394050598145,0.38999563455581665]},
        {i:1052,p:[-2.0005388259887695,0.6221240758895874,0.32991984486579895]}])
        math.norm(triple(tube.positions,sample.i)-sample.p) < 0.000001])
}
