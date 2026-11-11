import c: lambda.sync.core
import m: lambda.sync.mark
import t: lambda.sync.text

let original = {title:"Hello",count:1,items:[1,2],nested:{flag:true}}
let base = m.from_value("mark","seed",original)^
let title = m.child(base,"title")^
let a = t.insert(base,"alice",5,"!",title)^
let b = m.reconcile(base,"external",{title:"Hello",count:2,items:[1,2],nested:{flag:true}})^
let merged = c.merge(a,b)^
let x = m.set(base,"alice","count",4)^
let y = m.set(base,"bob","count",5)^
let conflicted = c.merge(x,y)^
let resolved = m.set(conflicted,"resolve","count",6)^
let arr = m.from_value("array","seed",[1,2,3])^
let array_a = m.splice(arr,"alice",1,0,[8])^
let array_b = m.splice(arr,"bob",1,0,[9])^
let elem = m.from_value("element","seed",<p id:"x", "Hello">)^
let element_new = m.reconcile(elem,"import",<p id:"y", "Hello!">)^
let removed = m.remove_field(base,"alice","count")^
let delete_edit = c.merge(removed,y)^;
let content_empty = m.from_value("content","seed",<p>)^
let content_added = m.splice(content_empty,"alice",0,0,["a",null,"", "b",b'\x01',b'\x02',[null,""]])^;
let binary_base = m.from_value("binary","seed",{payload:b'\x01'})^
let binary_a = m.set(binary_base,"alice","payload",b'\x02')^
let binary_b = m.set(binary_base,"bob","payload",b'\x03')^
let key_a = m.set(base,"alice","new-a",true)^
let key_b = m.set(base,"bob","new-b",false)^;
[m.value(base)^ == original,
 m.value(merged)^ == {title:"Hello!",count:2,items:[1,2],nested:{flag:true}},
 m.child(merged,"title")^ == title,
 len(m.conflicts(conflicted)^) == 1,
 m.value(conflicted) ^ { ^.message == "sync: unresolved field conflict" },
 m.value(resolved)^.count == 6,
 len(m.conflicts(c.merge(c.compact(resolved),x)^)^) == 0,
 m.value(c.merge(array_a,array_b)^)^ == [1,9,8,2,3],
 m.value(element_new)^ == {kind:'element',tag:'p',attributes:{id:"y"},children:["Hello!"]},
 len(m.conflicts(delete_edit)^) == 1,
 m.value(c.restore(c.snapshot(merged)^))^ == m.value(merged)^,
 m.value(content_added)^.children == ["ab",b'\x0102',[null,""]],
 len(m.conflicts(c.merge(binary_a,binary_b)^)^) == 1,
 m.value(binary_base)^.payload == b'\x01',
 [for (key at m.value(base)^) key] == ['title','count','items','nested'],
 m.value(c.merge(key_a,key_b)^)^ == m.value(c.merge(key_b,key_a)^)^]
