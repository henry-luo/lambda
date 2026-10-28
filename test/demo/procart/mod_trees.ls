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
pub fn stage_label(st,level) string => (if (st.document.generator == "plant.apex") "Generation " else "Level ")++string(level)
pub fn viewport(st,options={}) element {
    let id=options.id or "tree-scene",preview=options.preview == true
    let lens=home(),floor=shapes.geometry("cylinder",{segments:48},"tree-plinth")
    let model={*:st.model,resources:[*st.model.resources,floor],parts:[
        {id:"plinth",kind:"plinth",role:"ground",geometry:"tree-plinth",color:"#d8d1bd",opacity:1.0,
            matrix:[0.0,-0.12,0.0,0.0,2.7,0.0,0.0,0.0,0.0,0.0,2.7,0.0,0.0,-0.1,0.0,1.0]},
        *[for (part in st.model.parts where st.leaves or part.role != "leaf") part]]};
    let rendered=scene.to_scene3d(model,{id:id,camera:{id:id++"-camera"},position:lens.position,target:lens.target,fov:lens.fov,
        width:if (preview) 300 else 1000,height:if (preview) 160 else 320,background:"#eeeae0",
        lights:[native.light('hemisphere',{color:"#fff7df",'ground-color':"#7e896e",intensity:0.7}),
            native.light('directional',{position:[-4.0,9.0,7.0],color:"#fff1cd",intensity:0.9}),
            native.light('ambient',{color:"#dde8d3",intensity:0.35})]});
    <scene3d *:{*:map(rendered),style:"display:block;width:100%;touch-action:none",tabindex:if (preview) "-1" else "0",role:"img",
        'aria-label':if (preview) stage_label(st,options.level)++" tree preview" else "Selected tree; drag to orbit"},*content(rendered)>
}
// derive each stage from the saved levels, preserving seed, force and organ settings.
fn specimen(source,base,count) element => <tree *:{*:map(source),parameters:{*:source.parameters,
    *:(if (source.generator == "plant.apex") {iterations:count} else {levels:[for (i in 0 to (count-1))
        {*:base.parameters.levels[i],*:(if (i == count-1) {children:0} else {})}]})}}>
fn remesh(st) map^ {
    let model=plant.mesh(st.skeleton,DETAILS[st.detail])^;
    {*:st,model:model,stages:[for (item in st.stages) {*:item,
        model:if (item.level == depth(st)) model else plant.mesh(item.skeleton,DETAILS[st.detail])^}]}
}
fn regenerate(st,doc) map^ {
    let base=st.documents[st.id],sk=plant.architecture(doc)^
    let levels=if (doc.generator == "plant.apex") [1,int(ceil(base.parameters.iterations/2.0)),base.parameters.iterations]
        else [for (level in 1 to min(3,len(base.parameters.levels))) level]
    let current={*:st,document:doc,skeleton:sk}
    let stages=[for (level in levels) {level:level,
        skeleton:if (level == depth(current)) sk else plant.architecture(specimen(doc,base,level))^}];
    remesh({*:current,stages:stages})^
}
pub fn initial(documents) map^ {
    let doc=documents.broadleaf;
    regenerate({seed:doc.seed,id:"broadleaf",documents:documents,detail:"full",leaves:true},doc)^
}
pub fn depth(st) int => if (st.document.generator == "plant.apex") st.document.parameters.iterations else len(st.document.parameters.levels)
pub fn command(st,action) map^ {
    if (action == "leaves") {*:st,leaves:not st.leaves}
    else if (action == "full" or action == "coarse")
        if (action == st.detail) st else remesh({*:st,detail:action})^
    else {
        let id=if (st.documents[action] != null) action else st.id
        let source=if (id == st.id) st.document else st.documents[id]
        let seed=if (action == "next") st.seed+1 else if (action == "previous") max(0,st.seed-1) else st.seed
        let p=source.parameters
        let old=if (source.generator == "plant.apex") p.iterations else len(p.levels)
        let limit=if (source.generator == "plant.apex") 7 else len(st.documents[id].parameters.levels)
        let selected=[for (item in st.stages where action == "stage-"++string(item.level)) item][0]
        let next=if (selected != null) selected.level else if (action == "prune") max(1,old-1) else if (action == "grow") min(limit,old+1) else old
        let strength=if (action == "bend-up") min(1.5,p.force.strength+0.15) else if (action == "bend-down") max(0.0,p.force.strength-0.15) else p.force.strength
        let doc=specimen(<tree *:{*:map(source),seed:seed,parameters:{*:p,force:{*:p.force,strength:strength}}}>,st.documents[id],next);
        if (doc == st.document) st
        else if (selected != null) {*:st,document:doc,skeleton:selected.skeleton,model:selected.model}
        else regenerate({*:st,id:id,seed:seed},doc)^
    }
}
