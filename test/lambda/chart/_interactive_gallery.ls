// Live companion to chart_dashboard.ls: every chart keeps its own template state.
import chart:lambda.chart.chart
import dom
let encoding={x:{field:"x",dtype:"quantitative",scale:{domain:[0,10]},axis_enabled:false},
    y:{field:"y",dtype:"quantitative",scale:{domain:[0,10]},axis_enabled:false},key:{field:"id"}}
let rows=[{id:"a",x:2,y:3},{id:"b",x:5,y:7},{id:"c",x:8,y:4}]
let flow=chart.model({width:300,height:240,padding:20,data:{nodes:[{id:"a",fx:45,fy:90},{id:"b",fx:200,fy:90}],
    links:[{id:"ab",source:"a",target:"b",value:3}]},encoding:{key:{field:"id"}},
    params:[{name:"picked",select:{type:"point",fields:["id"]}}],mark:{kind:"force_graph",iterations:0,labels:false,parts:{
        node:{interaction:{element_select:{param:"picked"}}},link:{interaction:{element_select:{param:"picked",
            relationship:{node:"id",source:"source",target:"target"}}}}}},state:{selected:{fill:"#e15759",stroke:"#e15759"},unselected:{opacity:0.25}}})
let polar=chart.model({width:300,height:240,padding:20,data:rows,mark:{kind:"point",size:90},encoding:encoding,
    coordinate:{type:"polar",inner_radius:0.2},interaction:{brush_highlight:true,crosshair:true},state:{active:{fill:"#e15759"},inactive:{opacity:0.2}}})
let parallel=chart.model({width:300,height:240,padding:20,data:[{id:"a",speed:2,quality:5,cost:7},{id:"b",speed:8,quality:3,cost:2}],
    mark:{kind:"line"},coordinate:{type:"parallel"},encoding:{position:["speed","quality","cost"],key:{field:"id"}},
    interaction:{brush_axis_highlight:true},state:{active:{stroke:"#e15759"},inactive:{opacity:0.2}}})
let geographic=chart.model({width:300,height:240,padding:20,data:[{id:"a",lon:-35,lat:15,direction:0.4,magnitude:20},
    {id:"b",lon:40,lat:-15,direction:1.2,magnitude:20}],mark:{kind:"vector"},coordinate:{type:"geo",projection:{type:"equirectangular"}},
    encoding:{longitude:{field:"lon"},latitude:{field:"lat"},key:{field:"id"}},interaction:{pan_zoom:true}})
let first={width:300,height:240,padding:20,data:rows,mark:{kind:"point",size:100},encoding:encoding}
let player=chart.model({*:first,timeline:{autoplay:true,keyframes:[{at:0,spec:first},{at:2000,spec:{*:first,
    data:[{id:"a",x:7,y:7},{id:"b",x:3,y:3},{id:"c",x:5,y:8}]}}]}})
view <gallery> state phase:"none", notice:"none", mounted:true {
    <html <head <style "body{font-family:Arial;margin:20px}main{display:flex;flex-wrap:wrap;gap:16px}.card{border:1px solid #ddd;padding:12px}button{margin:4px}">>
    <body <h1 "Lambda Chart interactions and playback">
        <p "Select a graph node with a click or Enter. Drag to brush the polar and parallel charts. Drag or scroll the map.">
        <p "Interaction: ";<span id:"interaction-phase",notice>;"; animation: ";<span id:"animation-phase",phase>>
        <main
            <section id:"flow",class:"card",<h2 "Related graph selection">;apply(~.flow)>
            <section id:"polar",class:"card",<h2 "Polar brush">;apply(~.polar)>
            <section id:"parallel",class:"card",<h2 "Field-axis brush">;apply(~.parallel)>
            <section id:"map",class:"card",<h2 "Geographic navigation">;apply(~.map)>
            <section id:"playback",class:"card",<h2 "Keyframe playback">;
                <div for (command in ["play","pause","resume","seek","reverse","cancel"]) <button id:command,command>>;
                <button id:"mount",if (mounted) "Remove player" else "Restore player">;
                if (mounted) apply(~.player)>
        >
    >>
}
on click(evt) {
    let id=dom.get_attribute(evt.target,"id")
    if (id=="mount") { mounted=not mounted;return 'handled' }
    if (not contains(["play","pause","resume","seek","reverse","cancel"],id)) { return 'pass' }
    let host=dom.query_selector(dom.closest(evt.target,"#playback"),".lambda-chart")
    dom.dispatch(host,{type:"chart_action",bubbles:true,detail:{command:id,position:1000}})
    return 'handled'
}
on chart_interaction(evt) { notice=evt.type++":"++evt.phase;return 'handled' }
on chart_animation(evt) { phase=evt.phase;return 'handled' }
apply(<gallery flow:flow,polar:polar,parallel:parallel,map:geographic,player:player>)
