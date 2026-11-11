import c: lambda.sync.core
import t: lambda.sync.text

let base = t.from_text("text","seed","ab")^
let a = t.insert(base,"alice",1,"XY")^
let b = t.insert(base,"bob",1,"12")^
let merged = c.merge(a,b)^
let removed = t.remove(merged,"alice",1,4)^
let checkpoint = c.compact(removed)
let late = t.insert(a,"old-peer",2,"!")^
let continued = c.merge(checkpoint,late)^
let replayed = c.receive(t.empty("text"),reverse(merged.log))^
let unicode = t.from_text("unicode","seed","Aé😀")^;
[t.read(merged)^ == "a12XYb",
 c.merge(a,b)^ == c.merge(b,a)^,
 t.read(replayed)^ == t.read(merged)^,
 len(replayed.pending) == 0,
 t.read(checkpoint)^ == "ab",
 all([for (atom in checkpoint.atoms where contains(checkpoint.dead,atom.id)^) atom.value == null]),
 t.read(continued)^ == "a!b",
 t.read(c.merge(removed,late)^)^ == t.read(continued)^,
 c.packet(checkpoint,base)^.kind == 'checkpoint',
 t.read(t.replace(unicode,"import","AéZ😀")^)^ == "AéZ😀",
 t.remove(base,"alice",5,1) ^ { ^.message == "sync: sequence range out of bounds" }]
