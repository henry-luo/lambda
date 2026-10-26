import cage: .mod_cage
import sim: .mod_sim
import items: .mod_items
import camera: lambda.scene3d.camera
import numeric: lambda.scene3d.transform
import history: lambda.editor.mod_history

pub fn pet(st,id) => [for (h in st.pets where h.id == id) h][0]
pub fn initial() => {modules:cage.starter(),saved:cage.starter(),pets:[
    {*:sim.new_pet(1,"room-2"),food:0.32,comfort:0.55},
    {*:sim.new_pet(2,"room-9"),energy:0.25},
    {*:sim.new_pet(3,"room-6"),water:0.3}],
    selected:"",pet_selected:"ham-1",tool:"select",tab:"modules",item_tool:"",level:0,moving:false,turn:0,next_id:12,next_pet:4,
    hist:history.history_new(),elapsed:0.0,food:1.0,water:1.0,clean:1.0,spins:{},playing:true,speed:1,
    token:0,last_ms:null,drag:null,follow:false,camera:camera.prepare({position:[15.0,14.0,19.0],target:[0.0,1.4,-0.8],fov:40.0}),
    status:"A little neighborhood, a few tiny lives. Make yourself at home."}
pub fn camera_state(st) => if (not st.follow or pet(st,st.pet_selected) == null) st.camera else (
    let target = sim.position(st.modules,pet(st,st.pet_selected)),
    camera.prepare({*:st.camera,position:camera.add(target,camera.subtract(st.camera.position,st.camera.target)),target:target}))
pub fn missing_rooms(st) => (let h = pet(st,st.pet_selected),let reachable = if (h == null) [] else cage.reachable(st.modules,h.node),
    if (h == null) [] else [for (need in [{intent:"eat",label:"seed bowl (kitchen)"},{intent:"drink",label:"water bottle"},
        {intent:"sleep",label:"cozy hideout"},{intent:"exercise",label:"running wheel"}] where
        len([for (m in st.modules where items.serves(m,need.intent) and contains(reachable,m.id)) m]) == 0) need.label])
pub fn pick_pet(st,cam,x,y) {
    let hits = [for (h in st.pets,let p = sim.position(st.modules,h),let center = camera.project(cam,p),
        let edge = camera.project(cam,camera.add(p,camera.times(cam.right,0.6*sim.size(h)))),
        let radius = abs(edge.x-center.x) where center.visible and (x-center.x)**2+(y-center.y)**2 <= radius**2)
        {id:h.id,depth:center.depth}]
    let depth = min([for (hit in hits) hit.depth]);
    [for (hit in hits where hit.depth == depth) hit.id][0]
}
pub fn record(st,modules,options={},origins=null) => (let pets = sim.reconcile(modules,st.pets,origins),
    if (pets == null) {*:st,status:if (origins == null) "That edit leaves too little space between residents."
        else "A hamster is using this module and there is no free connected room. Add a room or rehome it first."} else
    {*:st,modules:modules,pets:pets,hist:history.history_snapshot(st.hist,st.modules,modules),*:options})
fn free_id(modules,serial) => if (cage.lookup(modules,"room-" ++ string(serial)) == null) serial else free_id(modules,serial+1)
pub fn build(st,x,z) {
    let existing = cage.at(st.modules,x,z,st.level)
    let chosen = cage.lookup(st.modules,st.selected)
    let serial = free_id(st.modules,st.next_id)
    let module = if (st.moving and chosen != null) {*:chosen,x:x,z:z,level:st.level}
        else cage.room("room-" ++ string(serial),st.tool,x,z,st.level,st.turn);
    if (st.tool == "delete") (if (existing == null) {*:st,status:"Click a module to delete it."} else remove({*:st,selected:existing.id}))
    else if (st.tab == "furnishings" and not st.moving) (if (existing == null) {*:st,status:"Choose an existing room for this furnishing."}
        else if (st.item_tool != "") furnish({*:st,selected:existing.id},st.item_tool)
        else {*:st,selected:existing.id,status:cage.entry(existing.kind).title ++ " furnishings selected."})
    else if (existing != null and not st.moving) {*:st,selected:existing.id,status:cage.entry(existing.kind).title ++ " selected."}
    else if (st.tool == "select" and not st.moving) {*:st,selected:""}
    else if (len(st.modules)>=40 and not st.moving) {*:st,status:"This neighborhood supports up to 40 modules."}
    else if (not cage.can_place(st.modules,module,if (st.moving) st.selected else ""))
        {*:st,status:"That space is unavailable. Ramps start on the ground floor."}
    else (
        let modules = if (st.moving) [for (m in st.modules) if (m.id == st.selected) module else m] else [*st.modules,module],
        record(st,modules,{selected:module.id,next_id:serial+1,moving:false,status:"Module placed. Matching doors connect automatically."}))
}
pub fn rotate_selected(st) => (let selected = cage.lookup(st.modules,st.selected),
    if (selected == null) {*:st,turn:(st.turn+1)%4} else
    record(st,[for (m in st.modules) if (m.id == selected.id) {*:m,turn:(m.turn+1)%4} else m],{status:"Module rotated."}))
pub fn remove(st) => if (cage.lookup(st.modules,st.selected) == null) {*:st,status:"Select a module to delete, or use the Delete tool."}
    else record(st,[for (m in st.modules where m.id != st.selected) m],
        {selected:"",moving:false,status:"Module deleted. Residents moved to free connected rooms if needed. Undo restores the module and its furnishings."},st.modules)
pub fn furnish(st,kind,remove=false) {
    let room = cage.lookup(st.modules,st.selected)
    let entry = items.entry(kind)
    let contents = items.contents(room);
    if (room == null or entry == null) {*:st,status:"Select a room and a furnishing first."}
    else if (not items.accepts(room)) {*:st,status:"Furnishings need an open room. Keep tunnels and ramps clear."}
    else if (not remove and items.has(room,kind)) {*:st,status:"This room already has a " ++ lower(entry.title) ++ "."}
    else if (not remove and len([for (other in contents where items.entry(other).slot == entry.slot) other])>0)
        {*:st,status:"That spot is occupied. Remove its current furnishing first."}
    else (
        let next = if (remove) [for (other in contents where other != kind) other] else [*contents,kind],
        record(st,[for (m in st.modules) if (m.id == room.id) {*:m,items:next} else m],
            {status:entry.title ++ (if (remove) " removed. Undo restores it." else " added. Residents can use it here.")}))
}
pub fn undo(st,redo=false) => (let result = if (redo) history.history_redo(st.hist,st.modules) else history.history_undo(st.hist,st.modules),
    let occupied = [for (h in st.pets where cage.lookup(result.doc,h.node) == null) h],
    if (len(occupied)>0) {*:st,status:"A resident is using a module this edit would remove. Try again once it leaves."}
    else (let pets = sim.reconcile(result.doc,st.pets),
        if (pets == null) {*:st,status:"That edit leaves too little space between residents."} else
        {*:st,modules:result.doc,pets:pets,hist:result.hist,selected:"",moving:false,status:"Cage edit restored."}))
pub fn adopt(st) => if (len(st.pets)>=6) {*:st,status:"There is room for six virtual residents."}
    else if (len(st.modules)==0) {*:st,status:"Build a room before adopting a hamster."}
    else (let home = if (cage.lookup(st.modules,st.selected) != null and cage.lookup(st.modules,st.selected).kind != "ramp") st.selected
        else [for (m in st.modules where m.kind != "ramp") m.id][0],
        if (home == null) {*:st,status:"Add a room for your new resident."} else (
            let added = sim.new_pet(st.next_pet,home),
            let pets = sim.arrange(st.modules,[*st.pets,added]),
            if (pets == null) {*:st,status:"No free room for a new resident. Wait for a clear room or add a connected module."}
            else {*:st,pets:pets,pet_selected:added.id,next_pet:st.next_pet+1,status:added.name ++ " has moved in."}))
pub fn care(st,action) => if (action == "feed") {*:st,food:1.0,status:"Seed bowls refilled. Hungry residents will find a reachable bowl."}
    else if (action == "water") {*:st,water:1.0,status:"Fresh water is ready in every bottle."}
    else if (action == "clean") {*:st,clean:1.0,status:"Fresh bedding, happy paws."}
    else if (action == "treat") {*:st,pets:[for (h in st.pets) if (h.id == st.pet_selected)
        {*:h,food:sim.bounded(h.food+0.12),comfort:sim.bounded(h.comfort+0.2),bond:sim.bounded(h.bond+0.12)} else h],status:"A little treat, a little trust."}
    else st
pub fn rename(st,value) => {*:st,pets:[for (h in st.pets) if (h.id == st.pet_selected) {*:h,name:slice(trim(value),0,24)} else h]}
pub fn archive(st) => {format:"lambda-hamster",version:1,modules:st.modules,food:st.food,water:st.water,clean:st.clean,elapsed:st.elapsed,
    residents:[for (h in st.pets) {id:h.id,name:h.name,coat:h.coat,node:h.node,food:h.food,water:h.water,energy:h.energy,
        comfort:h.comfort,bond:h.bond,age:h.age,walked:h.walked}]}
fn valid_id(id,prefix:string) => id is string and slice(id,0,len(prefix)) == prefix and len(id)<=14 and
    (int(slice(id,len(prefix))) is int) and int(slice(id,len(prefix)))>=1
pub fn restore(st,value) any^ {
    let header = if (not (value is map) or value.format != "lambda-hamster" or value.version != 1 or
        not (value.modules is array) or len(value.modules)>40 or not (value.residents is array) or len(value.residents)>6 or
        not all([for (key in ["food","water","clean","elapsed"]) numeric.numeric(value[key]) and value[key]>=0]) or
        any([for (key in ["food","water","clean"]) value[key]>1])) raise error("Hamster: invalid cage file.") else true
    let modules = [for (m in value.modules)
        if (not (m is map) or not valid_id(m.id,"room-") or not cage.can_place(value.modules,m,m.id) or not items.valid(m) or
            len([for (other in value.modules where other.id == m.id) other]) != 1) raise error("Hamster: invalid module or furnishings.") else {*:m,items:items.contents(m)}]
    let pets = [for (h in value.residents)
        if (not (h is map) or not valid_id(h.id,"ham-") or not (h.name is string) or len(h.name)>24 or
            not (h.coat is int) or h.coat<0 or h.coat>=len(sim.coats) or cage.lookup(modules,h.node) == null or
            len([for (other in value.residents where other.id == h.id) other]) != 1 or
            not all([for (key in ["food","water","energy","comfort","bond","age","walked"]) numeric.numeric(h[key]) and h[key]>=0]) or
            any([for (key in ["food","water","energy","comfort","bond"]) h[key]>1])) raise error("Hamster: invalid resident.")
        else {*:sim.new_pet(int(slice(h.id,4)),h.node),*:h}]
    let placed = sim.arrange(modules,pets)
    let space = if (placed == null) raise error("Hamster: not enough free rooms to separate the saved residents.") else true;
    {*:st,modules:modules,pets:placed,food:value.food,water:value.water,clean:value.clean,elapsed:value.elapsed,
        saved:modules,spins:{},hist:history.history_new(),selected:"",pet_selected:if (len(pets)>0) pets[0].id else "",
        moving:false,item_tool:"",tool:"select",next_id:1,next_pet:1+int(max([0,*[for (h in pets) int(slice(h.id,4))]])),status:"Cage, furnishings and residents restored. Activities will replan from their rooms."}
}
