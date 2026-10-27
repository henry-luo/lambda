import cage: ~~.~~.demo.hamster.mod_cage
import model: ~~.~~.demo.hamster.mod_model
import sim: ~~.~~.demo.hamster.mod_sim
import items: ~~.~~.demo.hamster.mod_items

fn solo(changes={}) => {*:model.initial(),modules:[cage.room("room-1","room",0,0)],selected:"room-1",
    pets:[{*:sim.new_pet(1,"room-1"),food:0.9,water:0.9,energy:0.9,comfort:0.9,*:changes}]}
fn equipped(kind,changes={}) => model.furnish(solo(changes),kind)
fn invalid(st,contents) => model.restore(st,{*:model.archive(st),modules:[{*:st.modules[0],items:contents}]}) ^ { true } ~ { false }
fn separated(st) => all([for (i,a in st.pets) for (j,b in st.pets where j>i) sim.clearance(st.modules,a,b)>=1.0])

let initial = model.initial()
let bottle = equipped("bottle",{water:0.1})
let kitchen = model.furnish(bottle,"bowl")
let removed = model.furnish(kitchen,"bottle",true)
let toy = equipped("chew",{comfort:0.2})
let restored = model.restore(initial,parse(format(model.archive(removed),'json'),'json')^)^
let furnished = model.furnish(model.furnish(kitchen,"hideout"),"chew")
let full = model.furnish(furnished,"wheel")
let moved = model.build({*:furnished,moving:true},1,1)
let legacy = model.restore(initial,{*:model.archive(solo()),modules:[{id:"room-1",kind:"kitchen",x:0,z:0,level:0,turn:0}]})^
let modules = [cage.room("room-1","room",-1,0),cage.room("room-2","room",0,0),cage.room("room-3","room",1,0)]
let crowded = {*:initial,modules:modules,pets:[sim.new_pet(1,"room-1"),sim.new_pet(2,"room-2")],selected:"room-1"}
let evacuated = model.remove(crowded)
let travelling = model.remove({*:crowded,selected:"room-2",pets:[{*:sim.new_pet(1,"room-1"),next:"room-2",route:["room-2"],progress:0.8}]})
let isolated = {*:solo(),modules:[*solo().modules,cage.room("room-2","room",2,2)]}
let blocked = model.remove(isolated)
let deleted = model.build({*:furnished,tool:"delete",pets:[]},0,0)
let undo = model.undo(deleted)
let stripped = model.furnish({*:initial,selected:"room-3"},"bottle",true)
let without_water = {*:stripped,pets:[{*:sim.new_pet(1,"room-3"),water:0.1}]};
{
    starter_contents:items.count(initial.modules) == 6,
    add_to_plain_room:items.contents(bottle.modules[0]) == ["bottle"],
    drink:sim.advance(bottle,5.0).pets[0].water>0.6,
    eat:sim.advance(equipped("bowl",{food:0.1}),6.0).pets[0].food>0.5,
    sleep:sim.advance(equipped("hideout",{energy:0.1}),10.0).pets[0].energy>0.8,
    exercise:sim.advance(equipped("wheel",{comfort:0.2}),6.0).spins["room-1"]>0.0,
    sand:sim.advance(equipped("sand",{comfort:0.2}),6.0).pets[0].comfort>0.5,
    toy:sim.advance(toy,6.0).pets[0].comfort>0.5,
    bridge:sim.advance(equipped("bridge",{comfort:0.2}),6.0).pets[0].comfort>0.5,
    remove:items.contents(removed.modules[0]) == ["bowl"] and not items.serves(removed.modules[0],"drink"),
    no_phantom_water:sim.advance(without_water,5.0).pets[0].water<0.1,
    duplicate_refused:model.furnish(bottle,"bottle").modules == bottle.modules,
    slot_conflict:full.modules == furnished.modules and len(items.contents(full.modules[0])) == 4,
    passages_clear:model.furnish({*:solo(),modules:[cage.room("room-1","ramp",0,0)]},"bottle").modules[0].items == [],
    undo_item:model.undo(removed).modules == kitchen.modules,
    redo_item:model.undo(model.undo(removed),true).modules == removed.modules,
    move_with_contents:moved.modules[0].x == 1 and moved.modules[0].items == furnished.modules[0].items,
    dirty:furnished.modules != furnished.saved,
    round_trip:restored.modules == removed.modules and restored.saved == restored.modules,
    empty_persists:items.contents(model.restore(initial,model.archive(model.furnish(bottle,"bottle",true)))^.modules[0]) == [],
    legacy_contents:items.contents(legacy.modules[0]) == ["bowl","bottle"],
    unknown_rejected:invalid(bottle,["unknown"]),
    duplicate_rejected:invalid(bottle,["bottle","bottle"]),
    collision_rejected:invalid(bottle,["hideout","wheel"]),
    wrong_shape_rejected:invalid(bottle,"bottle"),
    evacuate_occupied:len(evacuated.modules) == 2 and len(evacuated.pets) == 2 and separated(evacuated) and
        all([for (h in evacuated.pets) cage.lookup(evacuated.modules,h.node) != null]),
    cancel_deleted_destination:travelling.pets[0].node == "room-1" and travelling.pets[0].next == "",
    no_disconnected_evacuation:blocked.modules == isolated.modules and blocked.pets == isolated.pets,
    delete_tool:len(deleted.modules) == 0,
    undo_delete:undo.modules == furnished.modules and items.contents(undo.modules[0]) == ["bottle","bowl","hideout","chew"]
}
