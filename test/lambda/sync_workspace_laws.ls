import w: lambda.sync.workspace
import c: lambda.sync.core

let seed = w.transact(w.start("workspace-laws")^,"seed",[
    {kind:'create',type:'dir',parent:w.root,name:"A"},
    {kind:'create',type:'dir',parent:w.root,name:"B"},
    {kind:'create',type:'dir',parent:w.root,name:"C"}
])^.endpoint
let a = w.transact(seed,"a",[{kind:'move',entry:["seed",1],parent:["seed",2]}])^.endpoint
let b = w.transact(seed,"b",[{kind:'move',entry:["seed",2],parent:["seed",1]}])^.endpoint
let middle = w.transact(seed,"aa",[{kind:'move',entry:["seed",1],parent:["seed",3]}])^.endpoint
let ab = c.merge(a.replica,b.replica)^
let expected = c.compact(c.merge(ab,middle.replica)^)
let packets = [[for (op in a.replica.log where op.id[0] == "a") op],
    [for (op in middle.replica.log where op.id[0] == "aa") op],
    [for (op in b.replica.log where op.id[0] == "b") op]]
let schedules = [[0,1,2],[0,2,1],[1,0,2],[1,2,0],[2,0,1],[2,1,0]]
fn deliver(base, packets, order) any^ {
    var replica = base
    var i = 0
    while (i < len(order)) {
        replica = c.receive(replica,packets[order[i]])^
        replica = c.receive(replica,packets[order[i]])^
        i = i+1
    }
    return replica
}
let names = w.transact(seed,"names",[
    {kind:'create',type:'dir',parent:w.root,name:"same"},
    {kind:'create',type:'dir',parent:w.root,name:"same"},
    {kind:'create',type:'dir',parent:w.root,name:"normal~name"}
])^.endpoint
let rows = w.read(names.replica)^.entries
let generated = w.entry(names.replica,["names",1])^.materialized_name
let hostile = w.transact(names,"hostile",[{kind:'create',type:'dir',parent:w.root,name:generated}])^.endpoint
let visible = [for (entry in w.read(hostile.replica)^.entries where entry.visible) entry.materialized_name];
all([
    all([for (order in schedules) c.compact(deliver(seed.replica,packets,order)^) == expected]),
    c.compact(c.merge(a.replica,c.merge(b.replica,middle.replica)^)^) == expected,
    c.compact(c.merge(expected,expected)^) == expected,
    c.compact(c.merge(c.compact(ab),c.compact(middle.replica))^) == expected,
    w.path(expected,["seed",2])^ == "C/A/B",
    any([for (conflict in w.read(ab)^.conflicts) conflict.kind == 'cycle']),
    not any([for (conflict in w.read(expected)^.conflicts) conflict.kind == 'cycle']),
    w.entry(names.replica,["names",3])^.materialized_name == "normal~name",
    len(unique(visible)) == len(visible),
    w.entry(hostile.replica,["hostile",1])^.materialized_name == generated,
    c.validate(expected)^
])
