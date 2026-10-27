import s: lambda.scene3d
import geometry: ~~.scene3d._geometry
import model: .mod_model

fn material(color,opacity=1.0) => s.material('lambert',{color:color,opacity:opacity,side:'double'})
fn ellipsoid(position,scale,color,options={}) => <mesh geometry:"smooth",position:position,scale:scale,*:options,material(color)>
fn box(position,size,color,opacity=1.0) => s.mesh(s.box(size),material(color,opacity),{position:position})
fn fin(points,color,options={}) => s.mesh(s.geometry(points),material(color,0.85),options)

fn fish(o) array {
    let angel = o.kind == "angel"
    let gold = o.kind == "goldfish"
    let color = model.species(o.kind).color
    let body = if (angel) [0.52,0.65,0.2] else if (gold) [0.65,0.42,0.32] else [0.65,0.25,0.2];
    [ellipsoid([0.0,0.0,0.0],body,color),
     ellipsoid([0.02,0.12,0.0],[body[0]*0.9,body[1]*0.65,body[2]*1.015],if (gold) "#e98738" else if (angel) "#bdbb9a" else "#258ea8"),
     if (not gold and not angel) ellipsoid([-0.22,-0.1,0.0],[0.4,0.09,0.205],"#f26868"),
     if (angel) for (x in [-0.25,0.12]) ellipsoid([x,0.0,0.0],[0.065,0.6,0.21],"#485e60"),
     for (z in [-1.0,1.0]) *[
         ellipsoid([body[0]*0.62,body[1]*0.21,z*body[2]*0.85],[0.095,0.095,0.04],"#f6eecb"),
         ellipsoid([body[0]*0.66,body[1]*0.22,z*(body[2]*0.85+0.035)],[0.048,0.052,0.025],"#122e36"),
         fin([0.12,-0.02,z*0.1,-0.24,-0.25,z*0.58,-0.2,0.12,z*0.3],color)],
     fin([-0.4,0.14,0.0,-0.28,if (angel) 0.96 else 0.65,0.0,0.24,0.22,0.0],color),
     fin([-0.35,-0.14,0.0,-0.22,if (angel) -0.96 else -0.5,0.0,0.16,-0.22,0.0],color),
     s.group([fin([0.0,0.0,0.0,-0.45,if (gold) 0.52 else 0.34,0.08,-0.37,-0.34,-0.08],color)],
         {id:o.id ++ "-tail",position:[-0.58,0.0,0.0]})]
}

// two triangles per curved blade keep plants light enough to sway as retained groups.
fn blade(angle,height,width,color,bend) {
    let x = cos(angle),z = sin(angle)
    let positions = [0.0,-height/2,0.0,x*width,height*0.14,z*width,x*bend,height/2,z*bend,
        0.0,-height/2,0.0,x*bend,height/2,z*bend,0-x*width,height*0.14,0-z*width];
    fin(positions,color,{rotation:[0.0,angle*0.3,0.0]})
}
fn plant(o) array => if (o.kind == "grass") [for (i in 0 to 8)
    blade(i*2.4,2.7-0.16*(i%3),0.12,if (i%2 == 0) "#79bf77" else "#359476",0.25+0.1*(i%4))]
    else [for (i in 0 to 7,let angle = i*2.4,let height = if (o.kind == "fern") 2.2 else 1.6)
        s.group([
            ellipsoid([0.0,0.0,0.0],[0.025,height/2,0.025],"#43855b"),
            for (j in 0 to (if (o.kind == "fern") 3 else 1),let y = 0-height*0.34+j*height*0.23)
                for (side in [-1.0,1.0]) ellipsoid([side*(0.2+0.025*j),y,0.0],
                    [if (o.kind == "fern") 0.36 else 0.45,0.12,0.17],
                    if (o.kind == "lotus") (if (i%2 == 0) "#bc7772" else "#d69a81") else (if (i%2 == 0) "#4dab79" else "#2f896f"),
                    {rotation:[0.0,0.0,side*0.45]})
        ],{position:[cos(angle)*0.24,-0.08*(i%3),sin(angle)*0.24],rotation:[0.1*sin(angle),angle,0.18*cos(angle)]})]

fn decor(o) array => if (o.kind == "wood") [
    ellipsoid([0.0,-0.25,0.0],[1.5,0.24,0.35],"#805d40",{rotation:[0.0,0.0,0.2]}),
    ellipsoid([0.1,0.16,0.0],[0.95,0.14,0.18],"#a17b58",{rotation:[0.0,0.5,0.75]}),
    ellipsoid([-0.7,0.05,0.15],[0.5,0.12,0.17],"#9b7550",{rotation:[0.0,-0.6,-0.7]})]
    else [<mesh geometry:if (o.kind == "slate") "stone" else "smooth",scale:if (o.kind == "slate") [1.0,0.8,0.85] else [0.85,0.45,0.7],
        material(model.species(o.kind).color)>,
        if (o.kind == "slate") ellipsoid([0.15,0.3,0.25],[0.7,0.12,0.45],"#94aaa3",{rotation:[0.1,0.0,-0.2]})]

pub fn object_tree(o,seconds=0.0,still=false) => (let entry = model.species(o.kind),let pose = model.pose(o,seconds,still),
    s.group([*(if (entry.category == "fish") fish(o) else if (entry.category == "plant") plant(o) else decor(o))],
        {id:o.id,'data-kind':o.kind,position:pose.position,rotation:pose.rotation,scale:[o.size,o.size,o.size]}))

fn tank() => [
    box([0.0,-0.25,0.0],[12.4,0.5,6.4],"#173b42"),
    box([0.0,0.015,0.0],[12.0,0.22,6.0],"#c9c3a0"),
    box([0.0,3.5,-3.04],[12.0,7.0,0.06],"#21616a",0.25),
    for (x in [-6.04,6.04]) box([x,3.5,0.0],[0.045,7.0,6.0],"#77bac0",0.065),
    box([0.0,6.94,0.0],[12.0,0.025,6.0],"#8fd8d0",0.10),
    for (y in [0.0,7.0]) *[
        for (z in [-3.03,3.03]) box([0.0,y,z],[12.15,0.065,0.065],"#91bcba",0.65),
        for (x in [-6.03,6.03]) box([x,y,0.0],[0.065,0.065,6.1],"#91bcba",0.65)],
    for (x in [-6.03,6.03]) for (z in [-3.03,3.03]) box([x,3.5,z],[0.035,7.0,0.035],"#83b6b5",0.35),
    // sand grains share one mesh and one material through instances.
    <mesh geometry:"stone",material:"sand-grain",instances:[for (i in 0 to 119)
        s.transform([-5.7+(i*73%127)/127.0*11.4,0.15,-2.7+(i*47%131)/131.0*5.4],
            [i*0.3,i*1.3,0.0],[0.035+0.015*(i%3),0.025,0.035])^]>,
    for (i in 0 to 14) ellipsoid([-5.4+(i*17%31)/31.0*10.8,0.13,-2.5+(i*7%19)/19.0*5.0],
        [0.5,0.006,0.15],"#dfd8ae",{rotation:[0.0,i*1.4,0.0]})
]
pub fn viewport(st) => s.scene([
    s.camera({id:"tank-camera",position:st.camera.position,target:st.camera.target,fov:st.camera.fov}),
    s.light('ambient',{color:"#cce9e0",intensity:0.63}),
    s.light('directional',{position:[-4.0,12.0,8.0],color:"#fff4d2",intensity:0.85}),
    s.light('directional',{position:[8.0,5.0,-6.0],color:"#86cde5",intensity:0.35}),
    s.resources([geometry.sphere("smooth",10,16),geometry.sphere("stone",5,7),
        s.material('lambert',{id:"sand-grain",color:"#a59b7e"})]),
    *tank(),
    s.group([for (o in st.doc) object_tree(o,st.time,o.id == st.selected)],{id:"tank-objects"}),
    s.group([for (axis in 0 to 2) for (a in [-1.0,1.0]) for (b in [-1.0,1.0]) box(
        if (axis == 0) [0.0,a,b] else if (axis == 1) [a,0.0,b] else [a,b,0.0],
        [for (i in 0 to 2) if (i == axis) 2.0 else 0.012],"#a0f6d5",0.65)],{id:"selection-box",visible:false})
],{id:"tank-scene",camera:"tank-camera",width:900,height:610,background:"#0c2c35",
    tabindex:"0",role:"img",'aria-label':"Aquarium. Click an object to select it, drag to move, drag empty water to orbit.",
    style:"display:block;width:100%;height:610px;touch-action:none"})
