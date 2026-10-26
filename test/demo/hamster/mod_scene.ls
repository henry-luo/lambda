// D7.2.4v4 / S12.1.3: retained procedural meshes, with no model or texture downloads.
import s: lambda.scene3d
import g: ~~.scene3d._geometry
import cage: .mod_cage
import sim: .mod_sim
import items: .mod_items

fn material(color,opacity=1.0) => s.material('lambert',{color:color,opacity:opacity,side:'double'})
fn solid(shape,position,scale,color,options={},opacity=1.0) => <mesh geometry:shape,position:position,scale:scale,*:options,material(color,opacity)>
fn box(position,size,color,options={}) => solid("cube",position,size,color,options)
fn oval(position,scale,color,options={}) => solid("round",position,scale,color,options)
fn ring(position,scale,color,options={}) => solid("ring",position,scale,color,options)

fn torus() {
    let positions = [for (j in 0 to 32,let a = j*math.pi/16) for (i in 0 to 8,let b = i*math.pi/4)
        *[(1+0.065*cos(b))*cos(a),(1+0.065*cos(b))*sin(a),0.065*sin(b)]]
    let normals = [for (j in 0 to 32,let a = j*math.pi/16) for (i in 0 to 8,let b = i*math.pi/4)
        *[cos(b)*cos(a),cos(b)*sin(a),sin(b)]];
    s.geometry(positions,{id:"ring",normals:normals,indices:g.grid_indices(32,8)})
}
fn tube(curved=false) {
    let positions = [for (j in 0 to 12,let along = j/12.0,let angle = math.pi/2+along*math.pi/2)
        for (i in 0 to 12,let arc = i*math.pi/12)
        *(if (curved) [1.3+(1.3+0.65*cos(arc))*cos(angle),0.05+0.65*sin(arc),-1.3+(1.3+0.65*cos(arc))*sin(angle)]
            else [-1.3+along*2.6,0.05+0.65*sin(arc),0.65*cos(arc)])];
    s.geometry(positions,{id:if (curved) "bend-shell" else "tunnel-shell",indices:g.grid_indices(12,12)})
}
fn doorway() {
    let positions = [for (i in 0 to 15,let a = math.pi*i/16,let b = math.pi*(i+1)/16,
        let x = 0.5*cos(a),let y = 0.5+0.5*sin(a),let xx = 0.5*cos(b),let yy = 0.5+0.5*sin(b))
        *[x,y,0.0,xx,yy,0.0,x,1.2,0.0,xx,yy,0.0,xx,1.2,0.0,x,1.2,0.0]];
    s.geometry(positions,{id:"door-arch"})
}
fn wall(side,is_open,color,transparent) {
    let parts = if (is_open) [
        box([-0.88,0.6,0.0],[0.76,1.2,0.06],color),box([0.88,0.6,0.0],[0.76,1.2,0.06],color),
        solid("door-arch",[0.0,0.0,0.0],[1.0,1.0,1.0],color)]
        else [box([0.0,0.6,0.0],[2.5,1.2,0.045],color)]
    let position = [[1.28,0.0,0.0],[0.0,0.0,1.28],[-1.28,0.0,0.0],[0.0,0.0,-1.28]][side];
    s.group([*parts],{position:position,rotation:[0.0,if (side%2 == 0) math.pi/2 else 0.0,0.0],
        *:(if (transparent) {visible:false} else {})})
}
fn nest() => [
    box([0.0,0.45,-0.4],[1.5,0.9,0.06],"#ba8f68"),
    for (x in [-0.72,0.72]) box([x,0.45,0.15],[0.065,0.9,1.15],"#cba77e"),
    s.group([solid("door-arch",[0.0,0.0,0.0],[1.0,0.85,1.0],"#ceab83"),
        for (x in [-0.62,0.62]) box([x,0.47,0.0],[0.25,0.94,0.06],"#ceab83")],{position:[0.0,0.0,0.72]}),
    for (x in [-0.43,0.43]) box([x,1.02,0.13],[0.95,0.08,1.35],"#9f7856",{rotation:[0.0,0.0,if (x<0) 0.28 else -0.28]}),
    oval([0.0,0.035,0.2],[0.6,0.07,0.48],"#eee3bf")]
fn bowl(food) => [
    oval([0.87,0.12,0.82],[0.32,0.13,0.3],"#8eaf98"),
    ring([0.87,0.18,0.82],[0.3,0.28,0.3],"#b0cbb2",{rotation:[math.pi/2,0.0,0.0]}),
    for (i in 0 to 13) oval([0.87+0.22*cos(i*2.4),0.15+0.07*food,0.82+0.2*sin(i*2.4)],
        [0.065*food,0.035*food,0.05*food],if (i%2 == 0) "#d8b675" else "#a48a59")]
fn bottle(water) => [
    box([-0.95,0.7,-0.99],[0.14,1.4,0.12],"#afa38e"),
    oval([-0.95,0.88,-0.86],[0.22,0.6,0.22],"#c3ddd8"),
    oval([-0.95,0.4+water*0.4,-0.86],[0.2,0.45*max(0.01,water),0.2],"#76b4ba"),
    ring([-0.95,1.3,-0.86],[0.22,0.22,0.06],"#8ba7a0",{rotation:[math.pi/2,0.0,0.0]}),
    box([-0.95,0.25,-0.52],[0.055,0.055,0.6],"#8d9690",{rotation:[0.35,0.0,0.0]}),
    oval([-0.95,0.15,-0.26],[0.05,0.05,0.05],"#a7c6c2")]
fn wheel(id,angle) => [
    for (x in [-0.63,0.63]) box([x,0.05,0.0],[0.26,0.1,1.1],"#9caa99"),
    box([0.0,0.53,-0.42],[0.12,1.06,0.12],"#93a68f"),
    s.group([for (z in [-0.2,0.2]) ring([0.0,0.0,z],[0.88,0.88,0.88],"#8fafad"),
        for (i in 0 to 17,let a = i*math.pi/9)
            box([0.84*cos(a),0.84*sin(a),0.0],[0.06,0.07,0.42],"#c4d2be",{rotation:[0.0,0.0,a]}),
        for (a in [0.0,math.pi/3,2*math.pi/3]) box([0.0,0.0,-0.2],[1.7,0.035,0.035],"#96b5ad",{rotation:[0.0,0.0,a]})
    ],{id:id ++ "-spin",position:[0.0,0.98,-0.05],rotation:[0.0,0.0,angle]})]
fn ramp() => [
    box([0.0,1.2,0.0],[3.61,0.09,1.35],"#c4a57a",{rotation:[0.0,0.0,math.atan2(2.5,2.6)]}),
    for (i in 0 to 11,let x = -1.2+i*0.218) box([x,(x+1.3)*2.5/2.6+0.05,0.0],[0.045,0.05,1.25],"#a38862"),
    for (z in [-0.68,0.68]) box([0.0,1.45,z],[3.61,0.45,0.04],"#e1c7a2",{rotation:[0.0,0.0,math.atan2(2.5,2.6)]})]
fn furnishing(kind,m,st) {
    let children = if (kind == "hideout") nest() else if (kind == "bowl") bowl(st.food) else if (kind == "bottle") bottle(st.water)
    else if (kind == "wheel") wheel(m.id,if (st.spins[m.id] == null) 0.0 else st.spins[m.id])
    else if (kind == "sand") [oval([0.0,0.1,0.0],[0.95,0.12,0.9],"#b59773"),oval([0.0,0.16,0.0],[0.86,0.08,0.8],"#e5d2a3")]
    else if (kind == "bridge") [
        for (i in 0 to 8,let x = -0.9+i*0.225,let y = 0.12+0.48*(1.0-(x/0.9)**2))
            box([x,y,0.0],[0.2,0.1,0.85],if (i%2 == 0) "#c4a57a" else "#d5b990"),
        for (x in [-0.45,0.45]) box([x,0.22,0.0],[0.08,0.44,0.7],"#a58b69")]
    else [for (i in 0 to 2) oval([-0.9+i*0.13,0.09+(i%2)*0.07,0.84],[0.08,0.07,0.32],
        if (i%2 == 0) "#b29368" else "#d2b583",{rotation:[0.0,0.3+i*0.5,0.0]})];
    s.group([*children],{id:m.id ++ "-item-" ++ kind,'data-item':kind})
}
pub fn module_tree(modules,m,st) {
    let color = cage.entry(m.kind).color
    let special = contains(["tunnel","bend","ramp"],m.kind)
    let structure = if (m.kind == "ramp") ramp()
        else if (m.kind == "tunnel" or m.kind == "bend") [solid(if (m.kind == "bend") "bend-shell" else "tunnel-shell",[0.0,0.0,0.0],[1.0,1.0,1.0],"#aac7bd",{},0.38)]
        else [];
    s.group([
        box([0.0,-0.13,0.0],[2.53,0.22,2.53],color),
        box([0.0,0.0,0.0],[2.42,0.04,2.42],if (st.selected == m.id) "#f1d38d" else "#e5d1a7",{id:m.id ++ "-floor"}),
        if (not special) for (side in 0 to 3) wall(side,cage.open_side(modules,m,(side-m.turn+4)%4),color,side<2),
        // low front rails keep the cutaway readable without covering the residents.
        if (not special) for (z in [-1.25,1.25]) box([0.0,0.12,z],[2.5,0.12,0.035],"#bca27d"),
        *structure,
        for (kind in items.contents(m)) furnishing(kind,m,st),
        if (m.level>0) for (x in [-1.15,1.15]) for (z in [-1.15,1.15])
            box([x,-cage.floor_height/2,z],[0.1,cage.floor_height,0.1],"#baa58b")
    ],{id:m.id,'data-kind':m.kind,'data-items':format(items.contents(m),'json'),'data-cell':string(m.x) ++ "," ++ string(m.z) ++ "," ++ string(m.level),
        position:[m.x*cage.span,m.level*cage.floor_height,m.z*cage.span],rotation:[0.0,m.turn*math.pi/2,0.0]})
}
pub fn limb_position(i,phase=0.0,active=false) => [if (i<2) -0.32 else 0.25,
    -0.255+(if (active) 0.045*sin(phase+i*math.pi) else 0.0),if (i%2 == 0) -0.22 else 0.22]
fn limb(h,i) => s.group([
    oval([-0.035,0.12,0.0],[if (i<2) 0.14 else 0.085,0.16,0.085],sim.coats[h.coat]),
    oval([0.02,0.035,0.0],[0.065,0.08,0.065],"#e2baa7"),
    oval([0.055,0.0,0.0],[if (i<2) 0.14 else 0.105,0.045,0.085],"#dba79c"),
    for (toe in -1 to 1) *[
        oval([if (i<2) 0.16 else 0.13,0.005,toe*0.04],[0.045,0.025,0.023],"#e6b9ab"),
        oval([if (i<2) 0.193 else 0.163,0.006,toe*0.04],[0.02,0.012,0.013],"#f5e9d7")]
],{id:h.id ++ "-paw-" ++ string(i),'data-part':"leg",position:limb_position(i)})
pub fn hamster(h,modules) {
    let fur = sim.coats[h.coat];
    s.group([
        oval([-0.12,0.02,0.0],[0.48,0.32,0.3],fur,{'data-part':"torso"}),
        oval([0.04,-0.095,0.0],[0.36,0.22,0.295],"#f3e4c9",{'data-part':"belly"}),
        for (side in [-1,1]) *[
            oval([-0.31,-0.015,side*0.18],[0.25,0.27,0.17],fur,{'data-part':"haunch"}),
            oval([0.17,0.015,side*0.2],[0.17,0.22,0.11],fur),
            oval([-0.07,-0.09,side*0.285],[0.27,0.14,0.035],"#eddbbd")],
        if (h.coat == 2) oval([-0.16,0.33,0.0],[0.35,0.02,0.045],"#6f655d"),
        s.group([
            oval([0.35,0.14,0.0],[0.31,0.28,0.25],fur),
            for (side in [-1,1]) *[
                s.group([
                    oval([0.0,0.0,0.0],[0.095,0.13,0.07],fur),
                    oval([0.035,0.01,side*0.025],[0.055,0.085,0.045],"#d6aaa1"),
                    oval([0.04,-0.055,side*0.03],[0.045,0.04,0.04],"#ead2b4")
                ],{id:h.id ++ "-ear-" ++ string(side),'data-part':"ear",position:[0.3,0.39,side*0.2]}),
                oval([0.51,0.045,side*0.16],[0.18,0.16,0.15],"#f2dfc0",{'data-part':"cheek-pouch"}),
                oval([0.63,0.105,side*0.062],[0.09,0.075,0.08],"#f9ebd1")],
            s.group([for (z in [-0.225,0.225]) *[
                oval([0.51,0.225,z],[0.043,0.052,0.028],"#302b26"),
                oval([0.53,0.244,z*1.06],[0.013,0.016,0.009],"#fffdf5")]
            ],{id:h.id ++ "-eyes"}),
            s.group([for (z in [-0.23,0.23]) box([0.515,0.225,z],[0.055,0.012,0.025],"#6d5848")],
                {id:h.id ++ "-closed-eyes",visible:false}),
            oval([0.718,0.135,0.0],[0.032,0.025,0.04],"#cf9690",{'data-part':"nose"}),
            box([0.708,0.092,0.0],[0.012,0.045,0.012],"#9a7760"),
            for (side in [-1,1]) *[
                box([0.698,0.065,side*0.025],[0.014,0.012,0.05],"#9a7760",{rotation:[side*0.3,0.0,0.0]}),
                box([0.69,0.04,side*0.014],[0.023,0.04,0.024],"#fff8e8",{'data-part':"incisor"}),
                for (i in -1 to 1) box([0.59,0.12+i*0.035,side*0.27],[0.009,0.008,0.28],"#9d8970",
                    {rotation:[i*0.16,side*(0.1+i*0.15),0.0],'data-part':"whisker"})]
        ],{id:h.id ++ "-head"}),
        for (i in 0 to 3) limb(h,i),
        s.group([
            oval([-0.58,-0.065,0.0],[0.1,0.075,0.075],"#e0baa6"),
            oval([-0.665,-0.075,0.0],[0.04,0.05,0.05],"#d9ad9d"),
            for (x in [-0.61,-0.65]) ring([x,-0.07,0.0],[0.05,0.05,0.016],"#cf9f8e",{rotation:[0.0,math.pi/2,0.0]})
        ],{'data-part':"tail"})
    ],{id:h.id,position:sim.position(modules,h),scale:[sim.size(h),sim.size(h),sim.size(h)],'data-activity':h.activity,'data-coat':string(h.coat)})
}
pub fn viewport(st) => s.scene([
    s.camera({id:"cage-camera",position:st.camera.position,target:st.camera.target,fov:st.camera.fov}),
    s.light('ambient',{color:"#fff2dc",intensity:0.52}),
    s.light('directional',{position:[-8.0,15.0,8.0],color:"#fff8e2",intensity:0.72}),
    s.light('directional',{position:[8.0,8.0,-5.0],color:"#c2d8d6",intensity:0.2}),
    s.resources([g.sphere("round",10,16),s.box([1.0,1.0,1.0],{id:"cube"}),torus(),tube(false),tube(true),doorway()]),
    box([0.0,-0.4,0.0],[13.8,0.5,13.8],"#bba685"),
    box([0.0,-0.13,0.0],[13.5,0.05,13.5],"#d5c4a8"),
    for (x in [-6.8,6.8]) box([x,0.0,0.0],[0.12,0.5,13.7],"#b49a75"),
    for (z in [-6.8,6.8]) box([0.0,0.0,z],[13.7,0.5,0.12],"#b49a75"),
    for (i in -2 to 2) for (j in -2 to 2) box([i*cage.span,-0.08,j*cage.span],[2.48,0.02,2.48],"#ded2bc"),
    s.group([for (m in st.modules) module_tree(st.modules,m,st)],{id:"cage-modules"}),
    s.group([for (h in st.pets) hamster(h,st.modules)],{id:"cage-residents"})
],{id:"cage-scene",camera:"cage-camera",width:900,height:630,background:"#e8e2d5",tabindex:"0",role:"img",
    'aria-label':"Modular hamster neighborhood. Drag to orbit. Select a building tool and click a cell to build.",
    style:"display:block;width:100%;height:630px;touch-action:none"})
