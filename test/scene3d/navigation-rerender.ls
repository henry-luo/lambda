// S12.1.5v2/D4.5.2: template intent reconciles into the retained native asset owner.
import dom
import nav: lambda.navigation

fn stage(blocked) element => <scene3d id: "world", width: 160, height: 120, camera: "main", style: "display:none",
    <camera id: "main", type: 'perspective', position: [0,0,4], target: [0,0,0]>
    <resources *[
        nav.grid([3,3], {id:"routes",blocked:if (blocked) [[1,0],[1,1]] else []}),
        <'navigation-graph' id: "roads", data: {type:'graph',version:1,
            nodes:[nav.node("a",[0,0]),nav.node("b",[1,0])],edges:[nav.edge("a","b",2)]}>,
        nav.mesh([[0,0],[2,0],[2,1],[0,1]],[[0,1,2],[0,2,3]],{id:"floor"})]>>

view <navigation_lab> state redraws: 0, blocked: true {
    <div id: "lab", 'data-redraws': string(redraws), 'data-blocked': string(blocked),
        <button id: "redraw", "redraw"><button id: "open", "open">
        stage(blocked)>
}
on click(evt) {
    let id=dom.get_attribute(evt.target,"id")
    if (id=="redraw") { redraws=redraws+1 }
    else if (id=="open") { blocked=false }
}
<html
    <head <script type: "module", src: "_navigation_client.js">>
    <body apply(<navigation_lab>)>>
