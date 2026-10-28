// S12.1.1v3 / D4.5.3v2: style tracks change retained drawable uniforms.
import dom
import s: lambda.scene3d
fn stage(size) element => s.scene([
    s.camera({id:"camera",type:'orthographic',position:[0.0,0.0,4.0],view_height:4.0}),
    s.points(s.geometry([-1.0,0.0,0.0,0.0,0.0,0.0,1.0,0.0,0.0],{colors:[1.0,0.0,0.0,0.0,1.0,0.0,0.0,0.0,1.0]}),s.material(),{id:"cloud",size:size}),
    s.sprite(s.material('basic',{color:"#ffff00"}),{size:[24.0,16.0],'size-mode':'screen',position:[0.0,1.0,0.0]}),
    s.line(s.geometry([-1.0,-1.0,0.0,0.0,-0.5,0.0,1.0,-1.0,0.0]),s.material('basic',{color:"#ff00ff",opacity:0.7}),{width:6.0}),
    s.line_segments(s.geometry([-1.0,1.4,0.0,1.0,1.4,0.0]),s.material('basic',{color:"#00ffff"}),{width:3.0}),
    s.clip("drawable-change",[s.track("cloud.size",[0.0,1.0],[8.0,24.0])],{duration:1.0})
],{id:"scene",camera:"camera",width:128,height:128,background:"#000000",style:"display:block"})
view <panel> state size: 8.0 {
    <div <button id:"drawable-change","Resize"> stage(size)>
}
on click(evt) {
    if (dom.get_attribute(evt.target,"id")=="drawable-change") {size=24.0}
}
<html <head <style "html,body{margin:0}">> <body apply(<panel>)>>
