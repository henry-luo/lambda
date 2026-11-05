// Crystal Garden uses native Scene3D geometry, materials and Lambda camera controls.
import s: lambda.scene3d
import camera: lambda.scene3d.camera
import viewer: lambda.scene3d.viewer
import geometry: ._geometry
import mesh: .skymaster.mod_mesh
import navigation: ._navigation

fn home() map^ => camera.prepare({position:[8.0,7.0,11.0],target:[0.0,0.4,0.0],fov:42.0},800,500)
fn height(x,z) => 1.05+0.35*sin(x*1.21+z*0.63)*cos(z*0.91-x*0.32)
// A single vertex-colored mesh keeps the checkerboard in one native draw.
fn checkerboard() element^ {
    let cells=[for (x in 0 to 23,z in 0 to 23) {
        x: -4.5+x*0.375,z: -4.5+z*0.375,color:if ((x+z)%2 == 0) [0.10,0.15,0.25] else [0.18,0.26,0.38]}]
    let corners=[[0.0,0.0],[0.0,0.375],[0.375,0.0],[0.375,0.0],[0.0,0.375],[0.375,0.375]];
    s.geometry([for (cell in cells,corner in corners) *[cell.x+corner[0],0.0,cell.z+corner[1]]],
        {colors:[for (cell in cells,corner in corners) *cell.color]})
}
fn button(id,action,label) => <button id:id,type:"button",'data-camera':action,label>
fn controls() => <div class:"garden-controls",role:"group",'aria-label':"Camera controls",
    <button id:"orbit-mode",type:"button",'data-camera':"orbit-mode",'aria-pressed':"true","Orbit">
    <button id:"pan-mode",type:"button",'data-camera':"pan-mode",'aria-pressed':"false","Pan">
    button("orbit-left","left","Orbit ←"); button("orbit-right","right","Orbit →");
    button("tilt-up","up","Tilt ↑"); button("tilt-down","down","Tilt ↓");
    button("zoom-in","in","Zoom +"); button("zoom-out","out","Zoom −"); button("reset","reset","Reset view")>

let palette=["#64d8c4","#72e5c9","#82dce8"]
let scene=s.scene([
    s.camera({id:"garden-camera",position:[8.0,7.0,11.0],target:[0.0,0.4,0.0],fov:42.0}),
    s.light('ambient',{color:"#809fff",intensity:0.7}),
    s.light('directional',{position:[4.0,8.0,3.0],color:"#ffd2b0",intensity:1.2}),
    s.resources([
        mesh.geometry("crystal",mesh.cylinder([0.0,0.0,0.0],[0.0,2.4,0.0],0.38,5,0.0,false))^,
        mesh.geometry("garden-ring",mesh.torus([0.0,0.0,0.0],[0.0,1.0,0.0],3.9,0.045,0.045,64,8))^,
        geometry.sphere("halo",20,28)^,
        for (i in 0 to 2) s.material('lambert',{id:"crystal-"++string(i),color:palette[i]})
    ]),
    for (band in 0 to 2) <mesh geometry:"crystal",material:"crystal-"++string(band),
        instances:[for (x in -3 to 3,z in -3 to 3 where (x+z+6)%3 == band,
            let h=height(x,z)) s.transform([x*0.86,0.0,z*0.86],[0.05*z,x*0.4,0.08*x],[0.8,h,0.8])^]>,
    s.mesh(checkerboard()^,s.material('basic',{color:"#ffffff"}))^,
    <mesh geometry:"garden-ring",position:[0.0,0.15,0.0],s.material('basic',{color:"#ffc17a"})>,
    <mesh geometry:"halo",position:[0.0,3.8,0.0],scale:[1.0,1.0,1.0],
        s.material('basic',{color:"#aab9ff",opacity:0.2,transparent:true,'depth-write':false})>
],{id:"garden",camera:"garden-camera",width:800,height:500,viewBox:"0 0 800 500",background:"#080f22",
   tabindex:"0",role:"img",'aria-label':"Interactive crystal garden",'aria-describedby':"garden-help",
   'data-ready':"true",style:"display:block;width:100%;max-width:800px;height:auto;touch-action:none"});

<html <head <meta name:"viewport",content:"width=device-width,initial-scale=1"><meta charset:"utf-8">
    <title "Crystal garden / Scene3D"><link rel:"stylesheet",href:"demo.css"><link rel:"stylesheet",href:"three-gallery.css">>
    <body <main class:"scene3d-page",
        navigation.breadcrumbs("Crystal garden");
        <header <h1 "Crystal garden"><p "Lambda Scene3D · procedural geometry and light">>
        viewer.interactive(scene,home()^,null,{min_distance:4.0,max_distance:35.0,min_polar:0.08,max_polar:math.pi*0.49,pan_arrows:true,after:[controls()]},false)^;
        <p id:"garden-help","Drag to orbit · Pan mode or Shift-drag to move · Scroll to zoom · Arrow keys pan · Shift + arrows tilt · + / − zoom · R resets">
        <footer id:"status",'data-ready':"true","49 instanced crystals · Lambert lighting · checkerboard · transparent halo">
    >>
>
