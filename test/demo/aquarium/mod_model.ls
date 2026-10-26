// S12.1.3: the tank and all editor operations are values, independent of the DOM.
import camera: lambda.scene3d.camera
import numeric: lambda.scene3d.transform
import history: lambda.editor.mod_history

pub let catalog = [
    {kind:"tetra",name:"Neon tetra",category:"fish",color:"#3dd9e8",hint:"A flash of blue and red",half:[1.0,0.45,1.0],pick:[0.85,0.33,0.3]},
    {kind:"goldfish",name:"Goldfish",category:"fish",color:"#ffb454",hint:"A little liquid sunshine",half:[1.1,0.65,1.1],pick:[0.9,0.5,0.4]},
    {kind:"angel",name:"Angelfish",category:"fish",color:"#e7d9a8",hint:"Long fins, slow afternoons",half:[0.9,1.0,0.9],pick:[0.65,0.9,0.3]},
    {kind:"grass",name:"Ribbon grass",category:"plant",color:"#68b87b",hint:"A swaying meadow",half:[0.7,1.35,0.7],pick:[0.65,1.35,0.65]},
    {kind:"fern",name:"Water fern",category:"plant",color:"#399f79",hint:"Soft, layered green",half:[0.9,1.1,0.9],pick:[0.8,1.1,0.8]},
    {kind:"lotus",name:"Red lotus",category:"plant",color:"#c87e75",hint:"A warm accent among greens",half:[0.85,0.8,0.85],pick:[0.8,0.8,0.8]},
    {kind:"slate",name:"River slate",category:"rock",color:"#779393",hint:"Cool, weathered stone",half:[1.1,0.8,1.1],pick:[1.0,0.8,0.8]},
    {kind:"pebble",name:"River pebble",category:"rock",color:"#c9b89d",hint:"Smooth and sand-worn",half:[0.85,0.45,0.85],pick:[0.85,0.45,0.7]},
    {kind:"wood",name:"Driftwood",category:"decor",color:"#a17b58",hint:"A branch with a story",half:[1.65,0.8,1.65],pick:[1.5,0.8,0.8]}
]
pub fn species(kind) => [for (entry in catalog where entry.kind == kind) entry][0]
pub fn clamp(value,low,high) => min(high,max(low,value))
pub fn lookup(doc,id) => [for (o in doc where o.id == id) o][0]
pub fn constrain(o) {
    let half = species(o.kind).half
    let size = clamp(o.size,0.35,1.8)
    let limits = [6.0-half[0]*size,7.0-half[1]*size,3.0-half[2]*size];
    {*:o,size:size,position:[clamp(o.position[0],0-limits[0],limits[0]),
        clamp(o.position[1],0.14+half[1]*size,limits[1]),clamp(o.position[2],0-limits[2],limits[2])],
        yaw:((o.yaw%360)+360)%360}
}
pub fn make_object(id,kind,position,size=1.0,yaw=0.0) => constrain({id:id,kind:kind,position:position,size:size,yaw:yaw})
pub fn starter() => [
    make_object("item-1","slate",[-2.6,0.0,-0.8],1.25,20.0),
    make_object("item-2","slate",[-1.1,0.0,-1.2],0.8,140.0),
    make_object("item-3","pebble",[2.9,0.0,1.5],0.9),
    make_object("item-4","wood",[1.6,0.0,-0.5],1.0,-25.0),
    make_object("item-5","grass",[-4.3,0.0,-1.6],1.1),
    make_object("item-6","grass",[-3.2,0.0,-1.9],0.85,55.0),
    make_object("item-7","fern",[3.9,0.0,-1.5],1.15,60.0),
    make_object("item-8","fern",[2.7,0.0,-1.9],0.8,120.0),
    make_object("item-9","lotus",[-0.4,0.0,1.2],0.8,10.0),
    make_object("item-10","tetra",[-1.8,3.5,0.5],0.8),
    make_object("item-11","tetra",[0.1,4.2,-0.4],0.72),
    make_object("item-12","tetra",[1.9,3.6,0.6],0.8),
    make_object("item-13","goldfish",[-2.5,5.1,-0.4],1.0),
    make_object("item-14","angel",[3.0,4.6,-0.8],0.9)
]
pub fn initial() => {doc:starter(),saved:starter(),selected:"",category:"fish",playing:true,time:0.0,
    token:0,last_ms:null,drag:null,editing:null,hist:history.history_new(),next_id:15,
    camera:camera.prepare({position:[11.0,8.0,18.0],target:[0.0,3.0,0.0],fov:40.0}),
    status:"Your own little underwater world. Add something to make it yours."}

// snapshot transactions reuse the editor history; aquarium edits have no text steps.
pub fn record(st,before,after) => if (before == after) {*:st,doc:after} else {*:st,doc:after,
    hist:history.history_push(st.hist,{doc_before:before,doc_after:after,steps:[],sel_before:null,sel_after:null,meta:[]})}
pub fn select(st,id) => {*:st,selected:if (lookup(st.doc,id) != null) id else ""}
fn free_id(doc,n) => if (lookup(doc,"item-" ++ string(n)) == null) n else free_id(doc,n+1)
pub fn add(st,kind) => if (species(kind) == null or len(st.doc) >= 60) {*:st,status:"The tank holds up to 60 objects."} else (
    let serial = free_id(st.doc,st.next_id),
    let id = "item-" ++ string(serial),
    let fish = species(kind).category == "fish",
    let added = make_object(id,kind,[0.0,if (fish) 3.5 else 0.0,1.0],if (kind == "tetra") 0.8 else 1.0),
    {*:record(st,st.doc,[*st.doc,added]),selected:id,next_id:serial+1,status:species(kind).name ++ " added. Drag it or use the inspector."})
pub fn remove(st) => {*:record(st,st.doc,[for (o in st.doc where o.id != st.selected) o]),selected:"",status:"Object removed. Undo brings it back."}
pub fn duplicate(st) => (let chosen = lookup(st.doc,st.selected),
    if (chosen == null or len(st.doc) >= 60) st else (
        let serial = free_id(st.doc,st.next_id),let id = "item-" ++ string(serial),
        let copied = constrain({*:chosen,id:id,position:camera.add(chosen.position,[0.4,0.0,0.4])}),
        {*:record(st,st.doc,[*st.doc,copied]),selected:id,next_id:serial+1,status:"Object duplicated."}))
pub fn preview(st,edited_object) => {*:st,doc:[for (o in st.doc) if (o.id == edited_object.id) constrain(edited_object) else o]}
pub fn property(st,key,value) => (let o = lookup(st.doc,st.selected),
    if (o == null or not numeric.numeric(value)) st else (
        let edited = if (key == "size" or key == "yaw") {*:o,[key]:value}
            else {*:o,position:[for (i in 0 to 2) if (key == ["x","y","z"][i]) value else o.position[i]]},
        preview(st,edited)))
pub fn finish(st) => if (st.editing == null) st else {*:record(st,st.editing,st.doc),editing:null}
pub fn undo(st,redo=false) => (let result = if (redo) history.history_redo(st.hist,st.doc) else history.history_undo(st.hist,st.doc),
    {*:st,doc:result.doc,hist:result.hist,selected:"",drag:null,editing:null,status:if (redo) "Edit restored." else "Edit undone."})

pub fn pose(o,seconds,still=false) {
    let entry = species(o.kind)
    let phase = seconds*0.48+float(slice(o.id,5))*1.7
    let fish = entry.category == "fish" and not still
    let p = if (fish) constrain({*:o,position:[o.position[0]+0.8*sin(phase),o.position[1]+0.12*sin(phase*1.3),o.position[2]+0.35*cos(phase)]}).position else o.position;
    {position:p,rotation:[0.0,o.yaw*math.pi/180.0+(if (fish) math.atan2(0.35*sin(phase),0.8*cos(phase)) else 0.0),
        if (entry.category == "plant" and not still) 0.025*sin(seconds*1.2+phase) else 0.0],tail:if (still) 0.0 else 0.45*sin(seconds*8.0+phase)}
}
fn object_hit(st,ray,o) {
    let pose = pose(o,st.time,o.id == st.selected)
    let radii = species(o.kind).pick
    let angle = 0-pose.rotation[1]
    let delta = camera.subtract(ray.origin,pose.position)
    let local = [cos(angle)*delta[0]+sin(angle)*delta[2],delta[1],0-sin(angle)*delta[0]+cos(angle)*delta[2]]
    let d = ray.direction
    let direction = [cos(angle)*d[0]+sin(angle)*d[2],d[1],0-sin(angle)*d[0]+cos(angle)*d[2]]
    let a = sum([for (i in 0 to 2) (direction[i]/(radii[i]*o.size))**2])
    let b = 2*sum([for (i in 0 to 2) direction[i]*local[i]/(radii[i]*o.size)**2])
    let c = sum([for (i in 0 to 2) (local[i]/(radii[i]*o.size))**2])-1
    let discriminant = b*b-4*a*c;
    if (discriminant < 0) null else (
        let near = (0-b-sqrt(discriminant))/(2*a),let far = (0-b+sqrt(discriminant))/(2*a),
        let distance = if (near >= 0) near else far,
        if (distance < 0) null else {id:o.id,distance:distance})
}
pub fn pick(st,cam,x,y) {
    let ray = camera.ray(cam,x,y)
    let hits = [for (o in st.doc,let hit = object_hit(st,ray,o) where hit != null) hit]
    let nearest = min([for (h in hits) h.distance]);
    [for (h in hits where h.distance == nearest) h.id][0]
}
pub fn drag_point(cam,x,y,anchor,ground=false) => if (not ground)
    camera.unproject(cam,x,y,camera.project(cam,anchor).depth)
    else (let ray = camera.ray(cam,x,y),
        if (abs(ray.direction[1]) < 0.00001) anchor else
        camera.add(ray.origin,camera.times(ray.direction,(anchor[1]-ray.origin[1])/ray.direction[1])))

pub fn archive(st) => {format:"lambda-aquarium",version:1,objects:st.doc}
pub fn restore(st,value) any^ {
    // consume validation as a binding: a bare successful guard would join null to the returned map.
    let checked_header = if (not (value is map) or value.format != "lambda-aquarium" or value.version != 1 or
        not (value.objects is array) or len(value.objects) > 60) raise error("Aquarium: invalid tank file (version 1, up to 60 objects).") else true
    let checked = [for (o in value.objects)
        if (not (o is map) or not (o.id is string) or not starts_with(o.id,"item-") or
            not (int(slice(o.id,5)) is int) or int(slice(o.id,5)) < 1 or len(o.id) > 12 or
            species(o.kind) == null or not numeric.vector(o.position,3) or not numeric.numeric(o.size) or not numeric.numeric(o.yaw) or
            len([for (other in value.objects where other.id == o.id) other]) != 1)
            raise error("Aquarium: invalid or duplicate object.") else constrain(o)];
    {*:record(st,st.doc,checked),selected:"",saved:checked,next_id:1,drag:null,editing:null,status:"Tank opened."}
}
