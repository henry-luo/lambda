// S12.1.3: topology, placement and routing are pure values shared by the builder and simulation.
import camera: lambda.scene3d.camera
import items: .mod_items

pub let span = 2.6
pub let floor_height = 2.5
pub let catalog = [
    {kind:"room",title:"Play room",tag:"ROOM",color:"#b9cbb4",ports:[0,1,2,3],note:"A little space to explore"},
    {kind:"nest",title:"Sleep nook",tag:"REST",color:"#cda38b",ports:[0,1,2,3],note:"A cozy cardboard hideaway"},
    {kind:"kitchen",title:"Kitchen",tag:"CARE",color:"#c6b66e",ports:[0,1,2,3],note:"Seeds and a fresh water bottle"},
    {kind:"wheel",title:"Wheel room",tag:"PLAY",color:"#a9bec5",ports:[0,1,2,3],note:"Somewhere to spend that energy"},
    {kind:"sand",title:"Sand spa",tag:"PLAY",color:"#d6c599",ports:[0,1,2,3],note:"A small sandy retreat"},
    {kind:"tunnel",title:"Straight tunnel",tag:"LINK",color:"#b6c5b5",ports:[0,2],note:"Link two rooms in a line"},
    {kind:"bend",title:"Corner tunnel",tag:"LINK",color:"#b6c5b5",ports:[0,3],note:"A turn in their daily adventure"},
    {kind:"ramp",title:"Ramp up",tag:"LEVEL",color:"#bb9e7e",ports:[0,2],note:"West at this floor, east one up"},
    {kind:"lookout",title:"Sky lounge",tag:"ROOM",color:"#c5b9c9",ports:[0,1,2,3],note:"A room with a different view"}
]
pub let directions = [[1,0],[0,1],[-1,0],[0,-1]]
pub fn entry(kind) => [for (e in catalog where e.kind == kind) e][0]
pub fn lookup(modules,id) => [for (m in modules where m.id == id) m][0]
pub fn at(modules,x,z,level) => [for (m in modules where m.x == x and m.z == z and m.level == level) m][0]
pub fn position(m) {
    // a bend's waypoint lies on its quarter-circle centerline, rather than the cell's empty corner.
    let d = if (m.kind == "bend") span/2*(1.0-1.0/sqrt(2.0)) else 0.0
    let offset = [[d,0-d],[0-d,0-d],[0-d,d],[d,d]][m.turn];
    [m.x*span+offset[0],m.level*floor_height+(if (m.kind == "ramp") floor_height/2 else 0.0),m.z*span+offset[1]]
}
pub fn ramp_height(m,p) {
    let x = p[0]-m.x*span
    let z = p[2]-m.z*span
    let along = [x,0-z,0-x,z][m.turn];
    floor_height*(m.level+min(1.0,max(0.0,(along+span/2)/span)))
}
pub fn room(id,kind,x,z,level=0,turn=0) => {id:id,kind:kind,x:x,z:z,level:level,turn:turn,items:items.defaults(kind)}
pub fn starter() => [
    room("room-1","nest",-1,0),room("room-2","room",0,0),room("room-3","kitchen",1,0),
    room("room-4","wheel",1,1),room("room-5","tunnel",0,1),room("room-6","bend",-1,1),
    room("room-7","ramp",0,-1,0,1),room("room-8","lookout",0,-2,1),
    room("room-9","sand",1,-2,1),room("room-10","room",1,-1,1),room("room-11","nest",2,-1,1)
]
pub fn port_level(m,side) {
    let local = (side+m.turn)%4;
    if (not contains(entry(m.kind).ports,local)) -9
    else m.level+(if (m.kind == "ramp" and local == 0) 1 else 0)
}
pub fn connected(a,b) => if (a == null or b == null or abs(a.x-b.x)+abs(a.z-b.z) != 1) false else (
    let side = if (b.x>a.x) 0 else if (b.z>a.z) 1 else if (b.x<a.x) 2 else 3,
    let level = port_level(a,side),level >= 0 and level == port_level(b,(side+2)%4))
pub fn neighbors(modules,id,blocked=[]) => (let source = lookup(modules,id),
    [for (m in modules where connected(source,m) and not contains(blocked,m.id)) m.id])
pub fn open_side(modules,m,side) => len([for (other in modules where
    other.x == m.x+directions[side][0] and other.z == m.z+directions[side][1] and connected(m,other)) other]) > 0

// breadth-first paths carry their predecessor chain; visited IDs bound traversal even in loops.
fn search(modules,queue,seen,goal,blocked,index=0) {
    if (index >= len(queue)) [] else (
        let path = queue[index],let id = path[last],
        if (id == goal) path else (
            let fresh = [for (next in neighbors(modules,id,blocked) where not contains(seen,next)) next],
            search(modules,[*queue,*[for (next in fresh) [*path,next]]],[*seen,*fresh],goal,blocked,index+1)))
}
pub fn route(modules,from,to,blocked=[]) => if (lookup(modules,from) == null or lookup(modules,to) == null) []
    else search(modules,[[from]],[from],to,blocked)
fn flood(modules,queue,blocked,index=0) => if (index >= len(queue)) queue else (
    let fresh = [for (next in neighbors(modules,queue[index],blocked) where not contains(queue,next)) next],
    flood(modules,[*queue,*fresh],blocked,index+1))
pub fn reachable(modules,id,blocked=[]) => if (lookup(modules,id) == null) [] else flood(modules,[id],blocked)
pub fn links(modules) => [for (i,a in modules) for (j,b in modules where j>i and connected(a,b)) [a.id,b.id]]
pub fn valid_cell(x,z,level) => x is int and z is int and level is int and abs(x)<=2 and abs(z)<=2 and level>=0 and level<=1
pub fn can_place(modules,m,ignore="") => entry(m.kind) != null and valid_cell(m.x,m.z,m.level) and
    m.turn is int and m.turn >= 0 and m.turn <= 3 and (m.kind != "ramp" or m.level == 0) and
    len([for (other in modules where other.id != ignore and other.x == m.x and other.z == m.z and other.level == m.level) other]) == 0
pub fn grid_point(cam,x,y,level) => (let ray = camera.ray(cam,x,y),
    if (abs(ray.direction[1]) < 0.00001) null else (
        let distance = (level*floor_height-ray.origin[1])/ray.direction[1],
        let p = camera.add(ray.origin,camera.times(ray.direction,distance)),
        if (distance < 0) null else {x:int(round(p[0]/span)),z:int(round(p[2]/span)),level:level}))
