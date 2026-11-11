import w: lambda.sync.workspace
import c: lambda.sync.core

let seed = w.transact(w.start("lifecycle")^,"seed",[
    {kind:'create',type:'dir',parent:w.root,name:"folder"},
    {kind:'create',type:'file',parent:["seed",1],name:"a",content:w.content("base")^.reference}
])^.endpoint
let deleted = w.transact(seed,"delete",[{kind:'delete',entry:["seed",1]}])^.endpoint
let moved = w.transact(seed,"move",[{kind:'move',entry:["seed",2],parent:w.root}])^.endpoint
let escaped = c.merge(deleted.replica,moved.replica)^
let edited = w.transact(seed,"edit",[{kind:'publish',entry:["seed",2],content:w.content("offline")^.reference}])^.endpoint
let hidden = c.merge(deleted.replica,edited.replica)^
let created = w.transact(seed,"create",[{kind:'create',type:'file',parent:["seed",1],name:"new-offline",content:w.content("new")^.reference}])^.endpoint
let hidden_creation = c.merge(deleted.replica,created.replica)^
let restored = w.transact(deleted,"restore",[{kind:'restore',entry:["seed",1]}])^.endpoint
let other = w.transact(seed,"other",[{kind:'delete',entry:["seed",1]}])^.endpoint
let still_deleted = c.merge(restored.replica,other.replica)^
let left = w.transact(seed,"left",[{kind:'publish',entry:["seed",2],content:w.content("left")^.reference}])^.endpoint
let right = w.transact(seed,"right",[{kind:'publish',entry:["seed",2],content:w.content("right")^.reference}])^.endpoint
let conflict = c.merge(left.replica,right.replica)^
let resolved = w.transact({*:left,replica:conflict},"resolve",[
    {kind:'publish',entry:["seed",2],content:w.content("merged")^.reference}])^.endpoint;
all([
    w.entry(escaped,["seed",2])^.visible,
    any([for (notice in w.read(escaped)^.conflicts) notice.kind == 'delete-move']),
    not w.entry(hidden,["seed",2])^.visible,
    any([for (notice in w.read(hidden)^.conflicts) notice.kind == 'delete-edit']),
    not w.entry(hidden_creation,["create",1])^.visible,
    any([for (notice in w.read(hidden_creation)^.conflicts) notice.kind == 'delete-create']),
    w.entry(restored.replica,["seed",2])^.visible,
    not w.entry(still_deleted,["seed",2])^.visible,
    len(w.entry(conflict,["seed",2])^.heads)^ == 2,
    len(w.entry(resolved.replica,["seed",2])^.heads)^ == 1,
    len(w.entry(resolved.replica,["seed",2])^.heads[0].parents)^ == 2,
    c.validate(c.compact(still_deleted))^
])
