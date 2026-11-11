import c: lambda.sync.core
import t: lambda.sync.text
import m: lambda.sync.mark
import x: lambda.sync.change
import pos: lambda.sync.position

let base = t.from_text("positions","seed","ab")^
let left = pos.at(base,1,'left')^
let right = pos.at(base,1,'right')^
let changed = x.transaction(base,"alice",[{kind:'text',start:1,count:0,value:"X"}])^
let concurrent = t.insert(base,"bob",2,"!")^
let merged = c.merge(changed.replica,concurrent)^
let undone = x.undo(c.compact(merged),"undo",changed.undo)^
let removed = t.remove(changed.replica,"alice",0,2)^
let tree = m.from_value("ordering","seed",<p a:1,b:2,"hi">)^
let a = m.tag(m.reorder(tree,"alice",["b","a"])^,"alice",'h1')^
let b = m.tag(tree,"bob",'h2')^
let conflict = c.merge(a,b)^
let resolved = m.tag(conflict,"resolve",'h3')^
let field = m.from_value("undo-field","seed",{a:1})^
let change_local = x.transaction(field,"alice",[{kind:'set',key:"a",value:2}])^
let other = m.set(change_local.replica,"bob","a",3)^;
all([pos.resolve(changed.replica,left)^ == 1,pos.resolve(changed.replica,right)^ == 2,
 pos.resolve(c.compact(removed),left)^ == 0,pos.resolve(c.compact(removed),right)^ == 0,
 t.read(undone)^ == "ab!",
 m.value(a)^.tag == 'h1',[for (key at m.value(a)^.attributes) key] == ['b','a'],
 len(m.conflicts(conflict)^) == 1,m.value(resolved)^.tag == 'h3',
 m.value(x.undo(change_local.replica,"undo",change_local.undo)^)^ == {a:1},
 x.undo(other,"undo",change_local.undo) ^ { ^.message == "sync change: undo field changed; explicit resolution required" },
 x.transaction(base,"alice",[{kind:'text',start:1,count:0,value:"X"},{kind:'text',start:99,count:0,value:"Y"}]) ^ { ^.message == "sync: sequence range out of bounds" },
 t.read(base)^ == "ab"])
