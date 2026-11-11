import c: lambda.sync.core
import t: lambda.sync.text
import m: lambda.sync.mark
import s: lambda.sync.session

let initial = t.empty("identity")
let a = t.insert(initial,"same-actor",0,"A")^
let b = t.insert(initial,"same-actor",0,"B")^
let epoch = {*:initial,epoch:1}
let original = a.log[0]
let pending_a = {*:original,context:[{actor:"missing",counter:1}],time:2}
let pending_b = {*:pending_a,value:c.scalar("other")}
let pending = c.receive(initial,[pending_a])^
let malformed = {*:original,context:[{actor:"bad",counter:-1}]}
let invalid_delete = {*:original,kind:'delete',value:[["absent",1]]}
let opened = s.start(t.from_text("import-id","seed","A")^)^
let imported = s.import_external(opened,"import","external",opened.base.id,"B","B")^;
[c.merge(a,b) ^ { ^.message == "sync: actor operation ID reused" },
 c.merge(c.compact(a),c.compact(b)) ^ { ^.message == "sync: actor operation ID reused" },
 c.receive(a,[b.log[0]]) ^ { ^.message == "sync: actor operation ID reused" },
 c.merge(initial,epoch) ^ { ^.message == "sync: incompatible document, profile, root or epoch" },
 c.merge(initial,t.empty("other")) ^ { ^.message == "sync: incompatible document, profile, root or epoch" },
 c.receive(pending,[pending_b]) ^ { ^.message == "sync: actor operation ID reused" },
 c.snapshot(pending) ^ { ^.message == "sync: cannot save with unresolved dependencies" },
 c.receive(initial,[malformed]) ^ { ^.message == "sync: invalid causal context entry" },
 c.receive(initial,[invalid_delete]) ^ { ^.message == "sync: deletion target is absent" },
 t.insert(initial,"",0,"X") ^ { ^.message == "sync: actor must be nonempty text" },
 s.import_external(imported,"import","external",opened.base.id,"C","C") ^ { ^.message == "sync: import ID reused with different input" },
 s.resolve(imported,initial) ^ { ^.message == "sync: resolution must preserve observed causal context" }]
