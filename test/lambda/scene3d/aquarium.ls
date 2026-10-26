import model: ~~.~~.demo.aquarium.mod_model
import scene: ~~.~~.demo.aquarium.mod_scene
import s: lambda.scene3d
import camera: lambda.scene3d.camera

let initial = model.initial()
let added = model.add(initial,"goldfish")
let moved = model.property(added,"x",2.5)
let scaled = model.property(moved,"size",1.5)
let turned = model.property(scaled,"yaw",-90.0)
let edited = model.record(added,added.doc,turned.doc)
let copied = model.duplicate(edited)
let removed = model.remove(copied)
let undone = model.undo(removed)
let redone = model.undo(undone,true)
let opened = model.restore(initial,parse(format(model.archive(edited),'json'),'json')^)^
let invalid = model.restore(initial,{format:"lambda-aquarium",version:2,objects:[]}) ^ { true } ~ { false }
let duplicate_ids = model.restore(initial,{format:"lambda-aquarium",version:1,objects:[initial.doc[0],initial.doc[0]]}) ^ { true } ~ { false }
let bad_transform = model.restore(initial,{format:"lambda-aquarium",version:1,objects:[{*:initial.doc[0],size:"big"}]}) ^ { true } ~ { false }
let constrained = model.make_object("item-99","goldfish",[100.0,-100.0,-100.0],20.0,450.0)
let fish = model.make_object("item-1","goldfish",[0.0,3.5,0.0])
let cam = camera.prepare({position:[0.0,3.5,22.0],target:[0.0,3.5,0.0],fov:40.0},900,610)
let picking = {*:initial,doc:[fish],selected:fish.id,time:0.0}
let starting = model.drag_point(cam,450.0,305.0,fish.position)
let dragged = model.drag_point(cam,510.0,275.0,fish.position)
let limited = {*:initial,doc:[for (i in 1 to 60) model.make_object("item-" ++ string(i),"tetra",[0.0,3.0,0.0])]}
{
    starter_objects:len(initial.doc),
    categories:len(model.catalog),
    valid_scene:s.validate(scene.viewport(initial))^,
    add:len(added.doc) == 15 and added.selected == "item-15",
    move:model.lookup(moved.doc,moved.selected).position[0] == 2.5,
    resize:model.lookup(scaled.doc,scaled.selected).size == 1.5,
    rotate:model.lookup(turned.doc,turned.selected).yaw == 270.0,
    duplicate:len(copied.doc) == 16 and copied.selected != edited.selected,
    remove:len(removed.doc) == 15 and model.lookup(removed.doc,copied.selected) == null,
    undo:undone.doc == copied.doc,
    redo:redone.doc == removed.doc,
    edit_history:model.undo(edited).doc == added.doc,
    round_trip:opened is map and opened.doc == edited.doc and opened.saved == edited.doc,
    loaded_ids:len(model.add(opened,"tetra").doc) == 16,
    invalid_version:invalid,
    duplicate_ids:duplicate_ids,
    invalid_transform:bad_transform,
    constraints:constrained.size == 1.8 and constrained.position[0] < 6.0 and constrained.position[1] > 0.0 and constrained.position[2] > -3.0 and constrained.yaw == 90.0,
    swim:model.pose(fish,0.0).position != model.pose(fish,1.0).position,
    hold:model.pose(fish,1.0,true).position == fish.position,
    pick:model.pick(picking,cam,450.0,305.0) == fish.id,
    miss:model.pick(picking,cam,0.0,0.0) == null,
    drag:abs(starting[0]) < 0.000001 and dragged[0] > starting[0] and dragged[1] > starting[1] and abs(dragged[2]) < 0.000001,
    quota:len(model.add(limited,"tetra").doc) == 60,
    empty:model.remove({*:initial,doc:[],selected:""}).doc == []
}
