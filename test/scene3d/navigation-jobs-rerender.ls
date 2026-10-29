// S12.1.5v2/D4.5.2: source-bake descriptors reconcile with the retained native owner.
import dom
import scene: lambda.navigation.scene
import nav: lambda.navigation

fn stage(links_enabled) element => <scene3d id:"world", width:160, height:120, camera:"main", style:"display:none",
    <camera id:"main", type:'perspective', position:[0,0,4], target:[0,0,0]>
    <resources *[
        <geometry id:"ground", type:'buffer', positions:[0,0,0,0,0,4,4,0,4,4,0,0], indices:[0,1,2,0,2,3]>,
        scene.bake_asset("routes",["floor"],{low:[0,-1,0],high:[4,4,4],cell_size:0.5,cell_height:0.125,radius:0.25,height:1.5,max_step:0.375},{links:[nav.link("warp",[1,0,1],[3,0,3],{kind:"warp",cost:0,disabled:not links_enabled})]})]>
    <mesh id:"floor", geometry:"ground">>

view <bake_lab> state redraws:0, links_enabled:true {
    <div id:"lab", 'data-redraws':string(redraws), <button id:"redraw", "redraw"> <button id:"toggle", "toggle link"> stage(links_enabled)>
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="redraw") { redraws=redraws+1 }
    if (dom.get_attribute(evt.target,"id")=="toggle") { links_enabled=not links_enabled }
}
<html <head <script type:"module",src:"_navigation_jobs_client.js">> <body apply(<bake_lab>)>>
