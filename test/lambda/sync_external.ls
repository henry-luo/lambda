import c: lambda.sync.core
import t: lambda.sync.text
import m: lambda.sync.mark
import s: lambda.sync.session

let base = t.from_text("external-text","seed","Hello world")^
let opened = s.start(base)^
let editing = s.update(opened,t.insert(base,"alice",5,"!")^)^
let external = s.import_external(editing,"disk-1","import-1",opened.base.id,"Hello world?","Hello world?")^
let repeated = s.import_external(external,"disk-2","import-2",external.base.id,"Hello world?.","Hello world?.")^
let saved = s.save(repeated)^
let conflict_base = t.from_text("overlap","seed","abc")^
let conflict_open = s.start(conflict_base)^
let conflict_live = s.update(conflict_open,t.splice(conflict_base,"alice",1,1,"L")^)^
let conflicted = s.import_external(conflict_live,"conflict","external",conflict_open.base.id,"aEc","aEc")^
let unknown = s.import_external(editing,"unknown","unknown-actor",null,"other","other")^
let structured_base = m.from_value("external-mark","seed",{title:"Hello",count:1})^
let structured_open = s.start(structured_base)^
let title = m.child(structured_base,"title")^
let structured_live = s.update(structured_open,t.insert(structured_base,"alice",5,"!",title)^)^
let structured = s.import_external(structured_live,"mark-1","mark-import",structured_open.base.id,
    "{title:Hello,count:2}",{title:"Hello",count:2})^
let scalar_live = s.update(structured_open,m.set(structured_base,"alice","count",3)^)^
let scalar_conflict = s.import_external(scalar_live,"mark-overlap","mark-other",structured_open.base.id,
    "{title:Hello,count:4}",{title:"Hello",count:4})^
let resolved = s.resolve(scalar_conflict,m.set(scalar_conflict.live,"resolution","count",5)^)^
let bad = s.import_external(structured_live,"invalid","invalid-actor",structured_open.base.id,"{broken",error("invalid structured input"))^
let reused = s.import_external(editing,"reuse","alice",opened.base.id,"Hello?","Hello?")^;
[t.read(external.live)^ == "Hello! world?",
 external.disk == "Hello world?" and s.dirty(external)^,
 c.clock_get(external.imports[0].branch.clock,"alice") == 0,
 len(external.notices) == 1 and len(external.conflicts) == 0,
 s.import_external(external,"disk-1","import-1",opened.base.id,"Hello world?","Hello world?")^ == external,
 t.read(repeated.live)^ == "Hello! world?.",
 not s.dirty(saved)^ and saved.disk == "Hello! world?.",
 len(conflicted.conflicts) == 1 and conflicted.imports[0].raw == "aEc",
 t.read(conflicted.conflicts[0].live_before)^ == "aLc",
 s.save(conflicted) ^ { ^.message == "sync: resolve external conflicts before saving" },
 unknown.live == editing.live and len(unknown.conflicts) == 1,
 m.value(structured.live)^ == {title:"Hello!",count:2} and s.dirty(structured)^,
 len(m.conflicts(scalar_conflict.live)^) == 1 and len(scalar_conflict.conflicts) == 1,
 m.value(s.save(resolved)^.live)^.count == 5,
 bad.live == structured_live.live and bad.imports[0].raw == "{broken",
 reused.live == editing.live and len(reused.conflicts) == 1,
 s.save(s.deleted(editing)) ^ { ^.message == "sync: resolve external conflicts before saving" }]
