// workshop controls preserve the architecture when only mesh detail changes (PG13.2).
import plant: lambda.procart.plant
import scene: lambda.procart.scene
import camera: lambda.scene3d.camera
import native: lambda.scene3d
import shapes: lambda.procart.primitive

pub let CATALOG=[
 {id:"broadleaf",title:"Summer broadleaf",tag:"01 / BRANCH ARCHITECTURE",note:"Tapered limbs carry a loose crown of folded leaves. Each branch level has its own proportions.",file:"tree-broadleaf.mark"},
 {id:"conifer",title:"Silver conifer",tag:"02 / PHYLLOTAXIS",note:"A golden-angle spiral arranges needle-bearing branches. Upper limbs shorten into a pointed crown.",file:"tree-conifer.mark"},
 {id:"windswept",title:"Shaped by the wind",tag:"03 / DIRECTIONAL GROWTH",note:"A steady growth force bends the trunk and its descendants toward the same horizon.",file:"tree-windswept.mark"},
 {id:"apex",title:"Growing apices",tag:"04 / DEVELOPMENT",note:"Parametric buds divide in parallel. A spatial turtle interprets the result as stems and leaves.",file:"tree-apex.mark"}]
pub let DETAILS={full:{segments:10,section_stride:1,leaf_stride:1,leaf_scale:1.0},
    coarse:{segments:5,section_stride:3,leaf_stride:3,leaf_scale:1.45}}
pub fn description(id) map => [for (item in CATALOG where item.id == id) item][0]
pub fn home() map => camera.prepare({position:[8.0,6.0,10.0],target:[0.0,2.2,0.0],fov:36.0})
pub fn viewport(st) element {
    let lens=home(),floor=shapes.geometry("cylinder",{segments:48},"tree-plinth")
    let model={*:st.model,resources:[*st.model.resources,floor],parts:[
        {id:"plinth",kind:"plinth",role:"ground",geometry:"tree-plinth",color:"#d8d1bd",opacity:1.0,
            matrix:[0.0,-0.12,0.0,0.0,2.7,0.0,0.0,0.0,0.0,0.0,2.7,0.0,0.0,-0.1,0.0,1.0]},
        *[for (part in st.model.parts where st.leaves or part.role != "leaf") part]]};
    scene.to_scene3d(model,{id:"tree-scene",position:lens.position,target:lens.target,fov:lens.fov,background:"#eeeae0",
        lights:[native.light('hemisphere',{color:"#fff7df",'ground-color':"#7e896e",intensity:0.7}),
            native.light('directional',{position:[-4.0,9.0,7.0],color:"#fff1cd",intensity:0.9}),
            native.light('ambient',{color:"#dde8d3",intensity:0.35})]})
}
pub fn initial(documents) map^ {
    let doc=documents.broadleaf,sk=plant.architecture(doc)^;
    {seed:doc.seed,id:"broadleaf",documents:documents,document:doc,skeleton:sk,model:plant.mesh(sk,DETAILS.full)^,detail:"full",leaves:true}
}
pub fn depth(st) int => if (st.document.generator == "plant.apex") st.document.parameters.iterations else len(st.document.parameters.levels)
pub fn command(st,action) map^ {
    if (action == "leaves") {*:st,leaves:not st.leaves}
    else if (action == "full" or action == "coarse")
        if (action == st.detail) st else {*:st,detail:action,model:plant.mesh(st.skeleton,DETAILS[action])^}
    else {
        let id=if (st.documents[action] != null) action else st.id
        let source=if (id == st.id) st.document else st.documents[id]
        let seed=if (action == "next") st.seed+1 else if (action == "previous") max(0,st.seed-1) else st.seed
        let p=source.parameters
        let old=if (source.generator == "plant.apex") p.iterations else len(p.levels)
        let limit=if (source.generator == "plant.apex") 7 else len(st.documents[id].parameters.levels)
        let next=if (action == "prune") max(1,old-1) else if (action == "grow") min(limit,old+1) else old
        let strength=if (action == "bend-up") min(1.5,p.force.strength+0.15) else if (action == "bend-down") max(0.0,p.force.strength-0.15) else p.force.strength
        let parameters={*:p,force:{*:p.force,strength:strength},
            *:(if (source.generator == "plant.apex") {iterations:next} else {levels:[for (i in 0 to (next-1))
                {*:st.documents[id].parameters.levels[i],*:(if (i == next-1) {children:0} else {})}]})}
        let doc=<tree *:{*:map(source),seed:seed,parameters:parameters}>;
        if (doc == st.document) st else {
            let sk=plant.architecture(doc)^;
            {*:st,id:id,seed:seed,document:doc,skeleton:sk,model:plant.mesh(sk,DETAILS[st.detail])^}
        }
    }
}
