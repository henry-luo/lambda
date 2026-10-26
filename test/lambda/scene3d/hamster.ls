import cage: ~~.~~.demo.hamster.mod_cage
import model: ~~.~~.demo.hamster.mod_model
import sim: ~~.~~.demo.hamster.mod_sim
import scene: ~~.~~.demo.hamster.mod_scene
import s: lambda.scene3d
import camera: lambda.scene3d.camera

fn alone(kind,changes={},serial=1) => (let modules = [cage.room("room-1",kind,0,0)],
    {*:model.initial(),modules:modules,pets:[{*:sim.new_pet(serial,"room-1"),food:0.9,water:0.9,energy:0.9,comfort:0.9,*:changes}],spins:{}})

let initial = model.initial()
let built = model.build({*:initial,tool:"room"},-2,0)
let rotated = model.rotate_selected(built)
let moved = model.build({*:rotated,moving:true},-2,-1)
let collision = model.build({*:moved,moving:true},0,0)
let removed = model.remove(moved)
let undo = model.undo(removed)
let redo = model.undo(undo,true)
let adopted = model.adopt(built)
let blocked = model.remove(adopted)
let walking = sim.advance(initial,1.0)
let later = sim.advance(initial,20.0)
let eating = sim.advance(alone("kitchen",{food:0.1}),6.0)
let drinking = sim.advance(alone("kitchen",{water:0.1}),5.0)
let sleeping = sim.advance(alone("nest",{energy:0.1}),10.0)
let exercising = sim.advance(alone("wheel",{comfort:0.2}),6.0)
let bathing = sim.advance(alone("sand",{comfort:0.2},2),6.0)
let empty = sim.advance({*:alone("kitchen",{food:0.1}),food:0.0},6.0)
let refilled = sim.advance(model.care(empty,"feed"),6.0)
let isolated = {*:alone("nest",{food:0.1,water:0.1,energy:0.1}),modules:[cage.room("room-1","nest",-2,0),cage.room("room-2","kitchen",2,0)]}
let safe = sim.advance(isolated,10.0)
let archive = model.archive(later)
let restored = model.restore(initial,parse(format(archive,'json'),'json')^)^
let invalid = model.restore(initial,{*:archive,version:2}) ^ { true } ~ { false }
let overlap = model.restore(initial,{*:archive,modules:[initial.modules[0],{*:initial.modules[0],id:"room-99"}]}) ^ { true } ~ { false }
let bad_pet = model.restore(initial,{*:archive,residents:[{*:archive.residents[0],node:"missing"}]}) ^ { true } ~ { false }
let renamed = model.rename(initial,"Hazel")
let treated = model.care(initial,"treat")
let cam = camera.prepare({position:[0.0,12.0,0.0],target:[0.0,0.0,0.0],up:[0.0,0.0,-1.0]},500,500)
let point = cage.grid_point(cam,250,250,0)
let solo = alone("room")
let cam_view = camera.prepare({position:[3.0,3.0,4.0],target:[0.0,0.3,0.0]},640,440)
let projected = camera.project(cam_view,sim.position(solo.modules,solo.pets[0]));
let on_ramp = {*:initial.pets[0],node:"room-2",next:"room-7",progress:0.25}
let ramp_start = sim.position(initial.modules,on_ramp)
let ramp_middle = sim.position(initial.modules,{*:on_ramp,progress:0.75})
let body_height = 0.3*sim.size(on_ramp)
let bend_center = cage.position(cage.room("bend","bend",0,0));
{
    starter_modules:len(initial.modules),
    starter_pets:len(initial.pets),
    connected:len(cage.reachable(initial.modules,"room-1")) == 11,
    upper_route:cage.route(initial.modules,"room-1","room-11") == ["room-1","room-2","room-7","room-8","room-9","room-10","room-11"],
    reversed_route:len(cage.route(initial.modules,"room-11","room-1")) == 7,
    rotated_ports:cage.connected(cage.room("a","tunnel",0,0,0,1),cage.room("b","room",0,-1)) and
        not cage.connected(cage.room("a","tunnel",0,0,0,1),cage.room("b","room",1,0)),
    no_teleport:cage.route(isolated.modules,"room-1","room-2") == [],
    no_floating_ramp:not cage.can_place([],cage.room("r","ramp",0,0,1)),
    build:len(built.modules) == 12 and built.selected == "room-12",
    rotate:cage.lookup(rotated.modules,"room-12").turn == 1,
    move:cage.lookup(moved.modules,"room-12").z == -1,
    collision:collision.modules == moved.modules,
    remove:len(removed.modules) == 11,
    undo:undo.modules == moved.modules,
    redo:redo.modules == removed.modules,
    adopt:len(adopted.pets) == 4 and model.pet(adopted,"ham-4").node == "room-12",
    occupied_relocation:len(blocked.modules) == 11 and len(blocked.pets) == 4 and
        model.pet(blocked,"ham-4").node != "room-12" and model.undo(adopted).modules == adopted.modules,
    walking:walking.pets[0].next != "" and walking.pets[0].progress > 0,
    autonomous:all([for (h in later.pets) h.walked > 0]),
    eating:eating.pets[0].food > 0.5 and eating.food < 1.0,
    drinking:drinking.pets[0].water > 0.6 and drinking.water < 1.0,
    sleeping:sleeping.pets[0].energy > 0.8,
    exercise:exercising.pets[0].comfort > 0.5 and exercising.spins["room-1"] > 0,
    sand_bath:bathing.pets[0].comfort > 0.5,
    depleted:empty.pets[0].food < 0.1,
    refill:refilled.pets[0].food > 0.5,
    stranded:safe.pets[0].node == "room-1" and safe.pets[0].walked == 0 and safe.pets[0].energy > 0.8,
    growth:later.pets[0].age > initial.pets[0].age and sim.size(later.pets[0]) > sim.size(initial.pets[0]),
    rename:model.pet(renamed,"ham-1").name == "Hazel",
    treat:treated.pets[0].bond > initial.pets[0].bond,
    round_trip:restored is map and restored.modules == later.modules and len(restored.pets) == 3 and restored.food == later.food and restored.pets[0].age == later.pets[0].age,
    invalid_version:invalid,
    invalid_overlap:overlap,
    invalid_resident:bad_pet,
    grid_projection:point.x == 0 and point.z == 0,
    ramp_floor:abs(ramp_start[1]-body_height)<0.000001 and abs(ramp_middle[1]-body_height-0.625)<0.000001,
    bend_centerline:abs(sqrt((bend_center[0]-1.3)**2+(bend_center[2]+1.3)**2)-1.3)<0.000001,
    picking:model.pick_pet(solo,cam_view,projected.x,projected.y) == "ham-1",
    valid_scene:s.validate(scene.viewport(initial))^
}
