import cage: ~~.~~.demo.hamster.mod_cage
import model: ~~.~~.demo.hamster.mod_model
import sim: ~~.~~.demo.hamster.mod_sim

fn separated(st) => all([for (i,a in st.pets) for (j,b in st.pets where j>i,
    let p = sim.position(st.modules,a),let q = sim.position(st.modules,b))
    ((p[0]-q[0])/1.5)**2+((p[1]-q[1])/0.9)**2+((p[2]-q[2])/1.5)**2 >= 0.99999])
fn trace(st,count) => separated(st) and (count == 0 or trace(sim.advance(st,0.1),count-1))

let modules = [cage.room("room-1","room",-1,0),cage.room("room-2","kitchen",0,0),cage.room("room-3","room",1,0)]
let residents = [{*:sim.new_pet(1,"room-1"),food:0.1},{*:sim.new_pet(2,"room-3"),food:0.1}]
let converging = {*:model.initial(),modules:modules,pets:residents}
let one_room = {*:model.initial(),modules:[modules[0]],pets:[residents[0]],selected:"room-1",next_pet:2}
let adopted = model.adopt(one_room)
let duplicate = {*:residents[1],node:"room-1"}
let legacy = {*:model.archive(converging),residents:[residents[0],duplicate]}
let restored = model.restore(converging,legacy)^
let crowded = model.restore(converging,{*:legacy,modules:[modules[0]]}) ^ { true } ~ { false };
let fed = sim.advance(converging,20.0)
let opposing = {*:converging,pets:[
    {*:residents[0],next:"room-2",route:["room-2","room-3"],activity:"walking"},
    {*:residents[1],next:"room-2",route:["room-2","room-1"],activity:"walking"}]}
let full = model.adopt(model.adopt(model.adopt(model.initial())))
let exercised = sim.advance(full,45.0)
let levels = {*:converging,modules:[cage.room("room-1","room",0,0),cage.room("room-2","room",0,0,1)],
    pets:[sim.new_pet(1,"room-1"),sim.new_pet(2,"room-2")]}
let ramp_traffic = {*:converging,modules:[cage.room("room-1","ramp",0,0),cage.room("room-2","room",1,0,1),cage.room("room-3","room",0,0,1)],
    pets:[{*:sim.new_pet(1,"room-1"),next:"room-2",route:["room-2"],activity:"walking"},
        {*:sim.new_pet(2,"room-3"),remaining:20.0}]}
let stopped = sim.advance(ramp_traffic,1.0)
let resumed = sim.advance({*:stopped,pets:[stopped.pets[0]]},0.2);
{
    converging:trace(converging,80),
    adoption_capacity:len(adopted.pets) == 1 and separated(adopted),
    legacy_separation:len(restored.pets) == 2 and separated(restored),
    crowded_save_rejected:crowded,
    opposing:trace(opposing,100),
    both_fed:all([for (h in fed.pets) h.food > 0.5 and h.walked>0]),
    starter_traffic:trace(model.initial(),300),
    six_residents:len(full.pets) == 6 and trace(full,450),
    traffic_resumes:all([for (h in exercised.pets) h.walked>0]),
    independent_floors:trace(levels,30) and len(sim.arrange(levels.modules,levels.pets)) == 2,
    body_clearance:trace(ramp_traffic,10) and stopped.pets[0].activity == "waiting" and stopped.pets[0].walked < 1.25,
    resumes_after_yield:resumed.pets[0].progress > stopped.pets[0].progress and resumed.pets[0].activity == "walking"
}
