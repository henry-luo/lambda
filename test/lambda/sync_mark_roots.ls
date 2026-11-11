import c: lambda.sync.core
import m: lambda.sync.mark
import t: lambda.sync.text
import s: lambda.sync.session
import x: lambda.sync.change
import k: lambda.sync.codec
import pos: lambda.sync.position
import presence: lambda.sync.presence

let base = m.from_value("root","seed",{a:1,b:2})^
let scalar = m.replace_root(base,"alice",42)^
let array_value = m.replace_root(base,"bob",[1,2])^
let conflict = c.merge(scalar,array_value)^
let resolution = m.resolve_root(conflict,"resolve",c.field(array_value,c.root,"value")[0].id)^
let opened = s.start(base)^
let reordered = s.update(opened,m.reorder(base,"alice",["b","a"])^)^
let imported = m.reconcile(base,"external",{b:2,a:1})^
let changed = x.transaction(base,"alice",[{kind:'root',value:{title:"new"}}])^
let reverted = x.undo(c.compact(changed.replica),"undo",changed.undo)^
let text = t.from_text("presence","seed","hi")^
let cursor = pos.at(text,1)^
let first = presence.update([],text,"peer",1,[cursor,cursor],1000,100)^
let retry = presence.update(first,text,"peer",1,[cursor,cursor],1000,900)^;
let held = s.import_external(opened,"namespaced-input","external",opened.base.id,"original raw",
    {value:symbol("href","https://example.test/ns")})^;
all([m.value(scalar)^ == 42,m.value(resolution)^ == [1,2],len(m.conflicts(conflict)^) == 1,
 m.value(conflict) ^ { ^.message == "sync: unresolved field conflict" },
 s.dirty(reordered)^,[for (key at m.value(imported)^) key] == ['b','a'],
 m.value(reverted)^ == {a:1,b:2},c.validate(reverted)^,
 retry == first,len(presence.live(retry,1100)) == 0,len(presence.live(retry,1099)) == 1,
 k.encode(symbol("href","https://example.test/ns")) ^ { ^.message == "sync codec: scalar is not losslessly portable" },
 m.set(base,"alice",symbol("href","https://example.test/ns"),"value") ^ { ^.message == "sync: map key requires a lossless name adapter" },
 held.live == opened.live and held.imports[0].status == 'review' and held.conflicts[0].raw == "original raw"])
