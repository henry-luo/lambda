import s: lambda.scene3d
import g: .mod_geometry
import model: .mod_model

let HALF_PI = 1.5707963267948966
let bundles = [[0.36,0.0],[0.18,0.34],[0.18,-0.34],[-0.18,0.34],[-0.18,-0.34],[-0.36,0.0],[0.0,0.0]]
// all three bands retain upstream's radii, lengths, spacing, and distinct colors.
let bands = [
    {id:"a-band",x:0.0,radius:0.13,length:0.36*0.55,color:"#a02f43"},
    {id:"i-band",x:0.36*0.32,radius:0.115,length:0.36*0.32,color:"#e09aa5"},
    {id:"z-disc",x:0.36*0.5,radius:0.145,length:0.04,color:"#3d1418"}]

fn mesh(geometry,material,position=[0.0,0.0,0.0],scale=[1.0,1.0,1.0],options={}) =>
    <mesh geometry:geometry,material:material,position:position,scale:scale,*:options>
fn material(id,part,color,opacity=1.0,cut_opacity=null) => s.material('lambert',{
    // DOM metadata is text; the renderer reads the separate typed opacity attribute.
    id:id,color:color,'data-part':part,'data-opacity':string(opacity),
    'data-cut-opacity':if (cut_opacity == null) null else string(cut_opacity),
    opacity:model.opacity(part,model.initial(),opacity,cut_opacity)})

fn muscle() => s.group([
    mesh("sarcolemma","muscle-sarcolemma",[0.0,0.0,0.0],[1.0,1.0,1.0],
        {id:"muscle-shell",rotation:[0.0,0.0,HALF_PI]}),
    *[for (i,p in bundles) s.group([
        *[for (j in 0 to 11) s.group([
            *[for (band in bands) mesh("cylinder",band.id,[band.x,0.0,0.0],
                [band.radius,band.length/2,band.radius],{rotation:[0.0,0.0,HALF_PI]})]
        ],{position:[-2.2+j*0.36+0.18,0.0,0.0],'data-sarcomere':true})]
    ],{position:[0.0,p[0],p[1]],'data-bundle':i})],
    *[for (x in [-1.45,-0.1,1.25]) s.group([
        mesh("nucleus","muscle-nucleus"),
        mesh("nucleolus","muscle-nucleolus",[0.2,0.16,0.38],[0.23,0.23,0.23])
    ],{position:[x,0.62,0.18],scale:[0.36,0.16,0.22],'data-nucleus':true})],
    *[for (p in [[-1.8,-0.5,0.2],[-0.6,-0.55,-0.22],[0.6,-0.5,0.22],[1.8,-0.55,-0.2]])
        mesh("mitochondrion","muscle-mitochondria",p,[1.0,1.0,1.0],
            {rotation:[0.0,0.0,HALF_PI],'data-mitochondrion':true})],
    *[for (i in 0 to 3) mesh("reticulum","muscle-reticulum",[i*1.0,0.0,0.0])]
],{id:"specimen-muscle",rotation:[0.12,-0.18,-0.03]})

pub fn viewport(id,selected="plant") element^ => s.scene([
    s.camera({id:"camera",position:model.camera(model.initial()),fov:model.fov(model.initial())}),
    s.light('ambient',{color:"#ffffff",intensity:0.5}),
    s.light('directional',{position:[-4.0,7.0,8.0],color:"#fff2de",intensity:0.75}),
    s.light('directional',{position:[5.0,-3.0,2.0],color:"#d7e9ff",intensity:0.2}),
    s.resources([
        g.sphere("nucleus",48),g.sphere("nucleolus",28),g.cylinder("cylinder",18),
        g.capsule("sarcolemma",0.7,4.4,14,48),g.capsule("mitochondrion",0.09,0.32,8,14),
        g.tube("reticulum",[[-2.0,-0.78,0.28],[-1.9,-0.2,0.36],[-2.0,0.62,0.3]])^,
        *[for (band in bands) material(band.id,"myofibril",band.color)],
        material("muscle-sarcolemma","sarcolemma","#d7b284",0.36,0.22),
        material("muscle-nucleus","nucleus","#7a4aa2",0.92),
        material("muscle-nucleolus","nucleus","#b56ad8",0.9),
        material("muscle-mitochondria","mitochondria","#cf7042"),
        material("muscle-reticulum","sarcolemma","#ead2a7")]),
    s.group([],{id:"imported-specimen",visible:not model.procedural(selected)}),
    s.group([muscle()],{id:"procedural-specimen",visible:model.procedural(selected)})
],{id:id,camera:"camera",width:720,height:480,viewBox:"0 0 720 480",background:"transparent",
    tabindex:"0",role:"img",'aria-label':"Interactive cell specimen",style:"display:block;width:100%;height:420px;touch-action:none"})
