import w: lambda.sync.workspace

let original = w.content("original")^
let seed = w.transact(w.start("example-workspace")^,"seed",[
    {kind:'create',type:'file',parent:w.root,name:"notes.txt",content:original.reference}
])^.endpoint
let edited = w.content("saved changes")^
let changed = w.transact(seed,"alice",[
    {kind:'create',type:'dir',parent:w.root,name:"Archive",as:"archive"},
    {kind:'move',entry:["seed",1],parent:"archive",name:"meeting-notes.txt"},
    {kind:'publish',entry:["seed",1],content:edited.reference}
])^;
["Path: " ++ w.path(changed.endpoint.replica,["seed",1])^,
 "Stable file ID: seed/1",
 "Atomic operations: " ++ string(len(changed.undo.batch.operations))^]
