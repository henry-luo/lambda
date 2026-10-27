// S12.1.1v3 / D4.5.3v2: native visibility retains geometry and document-owned level history.
import dom
import s: lambda.scene3d
let levels=[s.group([s.mesh(s.plane([1.5,1.5,1.0]),s.material('basic',{color:"#ff0000"}))]),
    s.group([s.sprite(s.material('basic',{color:"#00ff00"}),{size:[40.0,40.0],'size-mode':'screen'})]),
    s.group([s.points(s.geometry([0.0,0.0,0.0]),s.material('basic',{color:"#0000ff"}),{size:24.0})])]
fn stage(culling,enabled,revision) element => s.scene([
    s.camera({id:"camera",type:'orthographic',position:[0.0,0.0,10.2],view_height:4.0}),
    s.resources([s.box([0.4,0.4,0.4],{id:"box"}),s.material('basic',{id:"white"})]),
    if (enabled) s.lod(levels,[0.0,10.0,30.0],{id:"detail"}) else levels[0],
    *[for (i in 0 to 99) s.mesh("box","white",{position:[20.0+i,0.0,0.0]})]
],{id:"scene",camera:"camera",width:192,height:192,background:"#000000",style:"display:block",'frustum-culling':culling,'data-revision':revision})
view <panel> state culling: true, enabled: true, revision: 0 {
    <div <button id:"near","Near"> <button id:"far","Far"> <button id:"refresh","Refresh">
        <button id:"culling","Culling"> <button id:"lod","LOD"> stage(culling,enabled,revision)>
}
on click(evt) {
    let id=dom.get_attribute(evt.target,"id")
    if (id=="near" or id=="far") {dom.set_attribute(dom.query_selector(dom.owner_document(evt.target),"#camera"),"position",if (id=="near") "0 0 8.9" else "0 0 40")}
    else if (id=="refresh") {revision=revision+1}
    else if (id=="culling") {culling=not culling}
    else if (id=="lod") {enabled=not enabled}
}
<html <head <style "html,body{margin:0}">> <body apply(<panel>)>>
