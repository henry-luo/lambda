import w: lambda.sync.workspace
import c: lambda.sync.core
import p: lambda.sync.protocol
import tree: lambda.sync.workspace_tree

let seed = w.transact(w.start("moves")^,"seed",[
    {kind:'create',type:'dir',parent:w.root,name:"A"},
    {kind:'create',type:'dir',parent:w.root,name:"B"},
    {kind:'create',type:'file',parent:["seed",1],name:"child",content:w.content("x")^.reference}
])^.endpoint
let a = w.transact(seed,"a",[{kind:'move',entry:["seed",1],parent:["seed",2]}])^.endpoint
let b = w.transact(seed,"b",[{kind:'move',entry:["seed",2],parent:["seed",1]}])^.endpoint
let ab = c.merge(a.replica,b.replica)^
let ba = c.merge(b.replica,a.replica)^
let materialized = w.read(ab)^
let renamed = w.transact(seed,"name",[{kind:'move',entry:["seed",3],name:"renamed"}])^.endpoint
let changed = w.transact(seed,"edit",[{kind:'publish',entry:["seed",3],content:w.content("y")^.reference}])^.endpoint
let together = c.merge(renamed.replica,changed.replica)^
let late = c.receive(seed.replica,[*[for (op in b.replica.log where op.id[0] == "b") op],
    *[for (op in a.replica.log where op.id[0] == "a") op]])^;
all([
    c.compact(ab) == c.compact(ba),c.compact(late) == c.compact(ab),
    len([for (conflict in materialized.conflicts where conflict.kind == 'cycle') conflict]) == 1,
    w.path(ab,["seed",3])^ == "B/A/child",
    c.validate(c.compact(ab))^,
    c.compact(c.merge(c.compact(a.replica),c.compact(b.replica))^) == c.compact(ab),
    w.path(together,["seed",3])^ == "A/renamed",
    len(w.entry(together,["seed",3])^.heads)^ == 1,
    w.transact(a,"a",[{kind:'move',entry:["seed",2],parent:["seed",1]}]) ^ {
        ^.message == "sync workspace: local move would create a cycle" },
    not tree.ancestor(tree.placements(tree.events(ab)^)^.entries,w.root,["seed",1])^
])
