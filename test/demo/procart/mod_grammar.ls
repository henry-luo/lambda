// the caller owns its trusted generator registry; stored recipes remain inert data (PG12.8–9).
import scene: lambda.procart.scene
import meadow: .mod_meadow
import botanical: .mod_botanical3d
import grass: .mod_grass
import geometry: ~~.scene3d._geometry
import camera: lambda.scene3d.camera
import plants: lambda.procart.plant
fn generated(request) map^ {
    let model = meadow.specimen(request)^;
    {parts:model.parts,resources:[geometry.sphere("round",10,16),*botanical.resources(),grass.geometry(),*model.resources]}
}
pub fn registry() map => {*:plants.registry(),'plant@1':{validate:meadow.check_parameters,generate:generated},
    'rock.faceted@1':{validate:meadow.check_parameters,generate:generated},
    'grass.blades@1':{validate:meadow.check_parameters,generate:generated}}
pub fn model(document) map^ => scene.expand(document,registry())^
pub fn home(id) map => camera.prepare({position:if (id == "primitives") [9.8,8.0,11.5] else if (id == "garden") [8.8,7.2,11.0] else [8.0,7.5,12.0],
    target:[0.0,0.6,0.0],fov:42.0})
pub fn viewport(model,id) element {
    let lens=home(id);
    scene.to_scene3d(model,{id:"grammar-scene",position:lens.position,target:lens.target,background:"#eeeae0"})
}
// the saved preset owns the initial seed; the controller must not replace it.
pub fn initial(documents) map^ => {seed:documents.primitives.seed,id:"primitives",documents:documents,document:documents.primitives,model:model(documents.primitives)^}
pub fn command(st,action) map^ {
    let id=if (st.documents[action] != null) action else st.id
    let seed=if (action == "next") st.seed+1 else if (action == "previous") max(0,st.seed-1) else st.seed
    let source=if (id == st.id) st.document else st.documents[id]
    let old_depth=if (source.settings.max_depth != null) source.settings.max_depth else 32
    let depth=if (action == "less-depth") max(0,old_depth-1) else if (action == "more-depth") min(64,old_depth+1) else old_depth
    let doc=<procart *:{*:map(source),seed:seed,settings:{*:source.settings,max_depth:depth}},*content(source)>;
    if (seed == st.seed and id == st.id and depth == old_depth) st else {*:st,seed:seed,id:id,document:doc,model:model(doc)^}
}
