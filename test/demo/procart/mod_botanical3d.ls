// S12.1.3 / D7.5.3v2: pure scene descriptions; camera events update retained DOM.
import s: lambda.scene3d
import viewer: .mod_scene_view
import camera: lambda.scene3d.camera
import geometry: ~~.scene3d._geometry
import solids: .mod_plant_solids
import bloom: .mod_bloom

fn home(plant=null) map => camera.prepare({position:if (plant.recipe.camera != null) plant.recipe.camera else [6.8,4.8,8.2],
    target:[0.0,2.0,0.0],fov:38.0})

pub fn tip_count(plant) int => len(plant.tips)*solids.whorls(plant)

fn dome() element {
    let globe = geometry.sphere("cap-source",16,24)
    let size = 9*25*3;
    s.geometry(slice(globe.positions,0,size),{id:"dome",normals:slice(globe.normals,0,size),indices:geometry.grid_indices(8,24)})
}

fn surface(kind) element {
    let along = if (kind == "oak-leaf") 32 else if (kind == "round-petal" or kind == "fan-leaf") 16 else 8
    let across = if (kind == "pad") 48 else if (kind == "fan-leaf") 16 else 8
    let domain = if (kind == "pad") [0.0,1.0] else [-1.0,2.0];
    s.geometry([for (j in 0 to along, i in 0 to across) *bloom.point(float(j)/float(along),domain[0]+domain[1]*float(i)/float(across),kind)],
        {id:kind,normals:[for (j in 0 to along, i in 0 to across) *bloom.normal(float(j)/float(along),domain[0]+domain[1]*float(i)/float(across),kind)],
         indices:geometry.grid_indices(along,across)})
}

pub fn resources() array => [dome(),*[for (kind in ["petal","round-petal","pad","oak-leaf","fan-leaf"]) surface(kind)]]

pub fn viewport(plant,show_leaves,show_blooms) element => viewer.viewport(
    solids.pieces(plant,show_leaves,show_blooms),resources(),
    {id:"botanical-scene",camera_id:"botanical-camera",model_id:"botanical-model",camera:home(plant),
     label:plant.recipe.title++" in 3D. Drag to orbit, scroll to zoom, R to reset.",fit:solids.pieces(plant)})

pub fn specimen(plant,show_leaves,show_blooms) element => viewer.interactive(viewport(plant,show_leaves,show_blooms),home(plant))
