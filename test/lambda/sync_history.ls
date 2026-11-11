import c: lambda.sync.core
import t: lambda.sync.text
import s: lambda.sync.session

let opened = s.start(t.from_text("history","seed","ab")^)^
let a = s.update(opened,t.insert(opened.live,"alice",1,"A")^)^
let captured = s.prepare_save(a)^
let advanced = s.update(a,t.insert(a.live,"alice",2,"later")^)^
let committed = s.commit_save(advanced,captured)^
let b = s.save(s.update(opened,t.insert(opened.live,"bob",2,"B")^)^)^
let received = s.receive_version(s.update(committed,b.live)^,b.base)^
let saved = s.save(received)^
let external = s.import_external(a,"external","ext",opened.base.id,"ab!","ab!")^;
all([committed.disk == "aAb",t.read(committed.live)^ == "aAlaterb",s.dirty(committed)^,
 c.restore(committed.base)^.clock == captured.saved.frontier,
 len(s.heads(received)^) == 2,len(saved.base.parents) == 2,
 contains(saved.base.parents,captured.saved.id)^ and contains(saved.base.parents,b.base.id)^,
 len(s.heads(saved)^) == 1,not s.dirty(saved)^,
 s.commit_save(external,captured) ^ { ^.message == "sync: disk base changed during save" },
 s.validate_version(saved.base)^])
