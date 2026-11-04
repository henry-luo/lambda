import history: lambda.editor.core.history

let first = history.snapshot_record(history.snapshot_new(),"a",2)
let second = history.snapshot_record(first,"b",2)
let third = history.snapshot_record(second,"c",2)
let undo = history.snapshot_undo(third,"d")
let redo = history.snapshot_redo(undo.history,undo.value)
let changed = history.snapshot_record(undo.history,undo.value,2)
// Exercise the two legacy stack layouts with more than one undone entry.
let head_a = history.snapshot_record(history.snapshot_new(),"a",2,true)
let head_b = history.snapshot_record(head_a,"b",2,true)
let head_c = history.snapshot_record(head_b,"c",2,true)
let head_undo = history.snapshot_undo(head_c,"d",true,true)
let head_twice = history.snapshot_undo(head_undo.history,head_undo.value,true,true)
let head_redo = history.snapshot_redo(head_twice.history,head_twice.value,true,true)
let tail_undo = history.snapshot_undo(third,"d",false,false)
let tail_twice = history.snapshot_undo(tail_undo.history,tail_undo.value,false,false)
let tail_redo = history.snapshot_redo(tail_twice.history,tail_twice.value,false,false);
[
    third.past == ["b","c"], undo.value == "c", redo.value == "d",
    redo.history.past == ["b","c"], len(changed.future) == 0,
    history.snapshot_undo(history.snapshot_new(),"a") == null,
    history.snapshot_redo(history.snapshot_new(),"a") == null,
    head_c.past == ["c","b"], head_twice.value == "b", head_twice.history.future == ["c","d"],
    head_redo.value == "c" and head_redo.history.past == ["b"],
    tail_twice.value == "b" and tail_twice.history.future == ["d","c"],
    tail_redo.value == "c" and tail_redo.history.past == ["b"]
]
