import cage: .mod_cage
import camera: lambda.scene3d.camera
import items: .mod_items

pub let coats = ["#c8945d","#d4c9b5","#8c8079","#ead6b1"]
pub let names = ["Pip","Mochi","Clover","Biscuit","Pebble","Maple"]
pub fn bounded(value) => min(1.0,max(0.0,value))
pub fn new_pet(serial,node) => {id:"ham-" ++ string(serial),name:names[(serial-1)%len(names)],coat:(serial-1)%len(coats),
    node:node,next:"",route:[],progress:0.0,intent:"explore",activity:"explore",remaining:0.0,
    food:0.8,water:0.85,energy:0.85,comfort:0.8,bond:0.15,age:0.5,walked:0.0}
pub fn size(h) => 0.76+0.24*min(1.0,h.age/5.0)
pub fn position(modules,h) array {
    let current = cage.lookup(modules,h.node)
    let next = if (h.next == "") current else cage.lookup(modules,h.next)
    let a = cage.position(current)
    let b = cage.position(next)
    let p = camera.add(a,camera.times(camera.subtract(b,a),h.progress))
    let ramp = if (current.kind == "ramp") current else if (next.kind == "ramp") next else null;
    // horizontal room floors meet the ramp only at its ends; linear endpoint Y would float or bury paws.
    [p[0],(if (ramp == null) p[1] else cage.ramp_height(ramp,p))+0.3*size(h),p[2]]
}
pub fn heading(modules,h) => if (h.next == "") 0.0 else (
    let a = cage.position(cage.lookup(modules,h.node)),let b = cage.position(cage.lookup(modules,h.next)),math.atan2(0-(b[2]-a[2]),b[0]-a[0]))
// adult clearance encloses the procedural body, ears, whiskers and walking bob, including future growth.
pub let body_half = [0.75,0.47,0.75]
fn center(modules,h) => camera.add(position(modules,h),[0.0,0.41-0.3*size(h),0.0])
fn relative(a,b) => [for (i in 0 to 2) (a[i]-b[i])/(2*body_half[i])]
pub fn clearance(modules,a,b) => (let delta = relative(center(modules,a),center(modules,b)),camera.dot(delta,delta))
fn held(pets) => [for (h in pets) *[h.node,*(if (h.next == "") [] else [h.next])]]
fn busy(st,h) => held([for (other in st.pets where other.id != h.id) other])
fn clear_of(modules,h,peers) => all([for (other in peers) clearance(modules,h,other) >= 1.0-0.000000001])
fn waiting(h) => {*:h,next:"",route:[],progress:0.0,activity:"waiting",remaining:0.0,intent:"explore"}

// the same allocation rule protects adoption, legacy saves and edits that invalidate an occupied edge.
pub fn arrange(modules,pets,index=0,placed=[],origins=null) {
    if (index >= len(pets)) placed else (
        let h = pets[index],
        if (cage.lookup(modules,h.node) == null and origins == null) null else (
            let occupied = held(placed),
            let present = cage.lookup(modules,h.node) != null,
            let keep = present and not contains(occupied,h.node) and (h.next == "" or not contains(occupied,h.next)) and clear_of(modules,h,placed),
            let peers = [*placed,*[for (other in slice(pets,index+1) where cage.lookup(modules,other.node) != null) other]],
            // deletion evacuates only within the old connected home; surviving residents keep their reservations.
            let homes = cage.reachable(if (present) modules else origins,h.node),
            let free = if (keep) [h] else [for (id in homes,let room = cage.lookup(modules,id),
                let candidate = waiting({*:h,node:id}) where room != null and room.kind != "ramp" and
                not contains(held(peers),id) and clear_of(modules,candidate,peers)) candidate],
            if (len(free) == 0) null else arrange(modules,pets,index+1,[*placed,free[0]],origins)))
}
pub fn reconcile(modules,pets,origins=null) => arrange(modules,[for (h in pets)
    // preserve an occupied edge, but replan its future route after a topology edit.
    if (h.next != "" and cage.connected(cage.lookup(modules,h.node),cage.lookup(modules,h.next))) {*:h,route:[h.next],intent:"explore"}
    else {*:h,next:"",route:[],progress:0.0,remaining:0.0,activity:"explore",intent:"explore"}],0,[],origins)
fn rooms(st,h,intent) => (let connected = cage.reachable(st.modules,h.node,busy(st,h)),
    [for (m in st.modules where contains(connected,m.id) and items.serves(m,intent)) m.id])
fn desires(st,h) => [
    if (h.water < 0.45 and st.water > 0.0) "drink",
    if (h.food < 0.55 and st.food > 0.0) "eat",
    if (h.energy < 0.4) "sleep",
    if (h.comfort < 0.72 and h.energy > 0.35) *[if (int(slice(h.id,4))%2 == 0) "bathe" else "exercise",
        if (int(slice(h.id,4))%2 == 0) "exercise" else "bathe","play"],
    "explore"]
fn duration(intent) => if (intent == "sleep") 8.0 else if (intent == "drink") 4.0 else if (intent == "eat") 5.0 else 6.0
fn begin_activity(h) => {*:h,activity:h.intent,remaining:duration(h.intent),next:"",route:[],progress:0.0}
fn choose(st,h) {
    let available = [for (want in desires(st,h),let choices = rooms(st,h,want) where len(choices)>0) {want:want,choices:choices}]
    let want = available[0].want
    let destinations = available[0].choices
    let away = [for (id in destinations where id != h.node) id]
    let choices = if (want == "explore" and len(away)>0) away else destinations
    let serial = int(slice(h.id,4))
    let index = (int(floor(st.elapsed/7.0))+serial)%max(1,len(choices))
    let target = choices[index]
    let route = cage.route(st.modules,h.node,target,busy(st,h))
    let intent = want
    let next = {*:h,intent:intent,route:slice(route,1),next:if (len(route)>1) route[1] else "",progress:0.0,activity:"walking"};
    if (len(route)<=1) (if (intent == "explore") waiting(next) else begin_activity(next)) else next
}
fn pet_step(st,h,dt) {
    let needs = {*:h,food:bounded(h.food-dt*0.004),water:bounded(h.water-dt*0.006),
        energy:bounded(h.energy-dt*(if (h.activity == "exercise") 0.025 else 0.0025)),
        comfort:bounded(h.comfort+dt*(if (st.clean > 0.6) -0.001 else -0.008)),age:h.age+dt/120.0}
    if (h.next != "") {
        let length = sqrt(camera.dot(camera.subtract(cage.position(cage.lookup(st.modules,h.next)),cage.position(cage.lookup(st.modules,h.node))),
            camera.subtract(cage.position(cage.lookup(st.modules,h.next)),cage.position(cage.lookup(st.modules,h.node)))))
        let progress = h.progress+dt*1.25/max(0.1,length)
        let moved = {*:needs,activity:"walking",walked:h.walked+min(dt*1.25,(1-h.progress)*length)};
        if (progress < 1.0) {*:moved,progress:progress} else (
            let remaining = slice(h.route,1),
            let arrived = {*:moved,node:h.next,route:remaining,next:if (len(remaining)>0) remaining[0] else "",progress:0.0},
            if (len(remaining)==0) begin_activity(arrived) else arrived)
    } else if (h.remaining > 0.0) {
        let current = cage.lookup(st.modules,h.node)
        let eating = h.activity == "eat" and items.serves(current,"eat") and st.food > 0
        let drinking = h.activity == "drink" and items.serves(current,"drink") and st.water > 0
        let sleeping = h.activity == "sleep" and items.serves(current,"sleep")
        let playing = contains(["exercise","bathe","play"],h.activity) and items.serves(current,h.activity);
        {*:needs,remaining:max(0.0,h.remaining-dt),food:bounded(needs.food+(if (eating) dt*0.1 else 0.0)),
            water:bounded(needs.water+(if (drinking) dt*0.17 else 0.0)),energy:bounded(needs.energy+(if (sleeping) dt*0.1 else 0.0)),
            comfort:bounded(needs.comfort+(if (playing) dt*0.075 else if (sleeping) dt*0.01 else 0.0))}
    } else choose(st,needs)
}
fn spin_wheels(st,pets,dt,index=0,angles={}) => if (index >= len(st.modules)) angles else (
    let m = st.modules[index],let active = len([for (h in pets where h.node == m.id and h.activity == "exercise" and h.remaining > 0) h]),
    spin_wheels(st,pets,dt,index+1,if (not items.has(m,"wheel")) angles else
        {*:angles,[m.id]:(if (st.spins[m.id] == null) 0.0 else st.spins[m.id])+active*dt*5.0}))
fn swept_clear(modules,from,to,peer_from,peer_to) {
    let a = relative(center(modules,from),center(modules,peer_from))
    let b = relative(center(modules,to),center(modules,peer_to))
    let motion = camera.subtract(b,a)
    let squared = camera.dot(motion,motion)
    let t = if (squared <= 0.000000000001) 0.0 else bounded(0-camera.dot(a,motion)/squared)
    let closest = camera.add(a,camera.times(motion,t));
    camera.dot(closest,closest) >= 1.0-0.000000001
}
fn blocked_move(previous,proposed) => {*:proposed,node:previous.node,next:previous.next,route:previous.route,
    progress:previous.progress,intent:previous.intent,walked:previous.walked,activity:"waiting",remaining:0.0}
fn advance_pets(st,dt,order,index=0,accepted=[]) {
    if (index >= len(order)) accepted else (
        let h = order[index],let peers = [*accepted,*slice(order,index+1)],
        let proposed = pet_step({*:st,pets:[h,*peers]},h,dt),
        // a route planned earlier may now lead into another resident's reserved room.
        let candidate = if (proposed.next != "" and contains(held(peers),proposed.next))
            (if (h.progress>0 and proposed.node == h.node) blocked_move(h,proposed) else waiting(proposed)) else proposed,
        let clear = all([for (other in peers,let before = [for (old in st.pets where old.id == other.id) old][0])
            swept_clear(st.modules,h,candidate,before,other)]),
        let next = if (clear) candidate else blocked_move(h,candidate),
        advance_pets(st,dt,order,index+1,[*accepted,next]))
}
pub fn step(st,dt) {
    // rotate priority so competing residents all get turns at a shared doorway or resource.
    let first = int(floor(st.elapsed*5))%max(1,len(st.pets))
    let order = if (len(st.pets) == 0) [] else [for (i in 0 to (len(st.pets)-1)) st.pets[(first+i)%len(st.pets)]]
    let accepted = advance_pets(st,dt,order)
    let pets = [for (h in st.pets) [for (next in accepted where next.id == h.id) next][0]]
    let eaters = len([for (h in pets where h.activity == "eat" and h.remaining > 0 and items.serves(cage.lookup(st.modules,h.node),"eat")) h])
    let drinkers = len([for (h in pets where h.activity == "drink" and h.remaining > 0 and items.serves(cage.lookup(st.modules,h.node),"drink")) h]);
    {*:st,pets:pets,elapsed:st.elapsed+dt,food:bounded(st.food-eaters*dt*0.015),water:bounded(st.water-drinkers*dt*0.02),
        clean:bounded(st.clean-len(pets)*dt*0.00045),spins:spin_wheels(st,pets,dt)}
}
// bounded substeps keep paths and care rates stable for fast-forward and ordinary frames.
pub fn advance(st,seconds) => if (seconds <= 0.000001) st else (
    let dt = min(0.2,seconds),advance(step(st,dt),seconds-dt))
