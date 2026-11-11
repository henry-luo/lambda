import c: lambda.sync.core
import t: lambda.sync.text
import m: lambda.sync.mark
import s: lambda.sync.session

let initial = s.start(t.from_text("saved","seed","ab")^)^
let save_a = initial.base
let step_b = s.save(s.update(initial,t.insert(initial.live,"alice",1,"B")^)^)^
let step_c = s.save(s.update(step_b,t.insert(step_b.live,"alice",2,"C")^)^)^
let step_d = s.save(s.update(step_c,t.insert(step_c.live,"alice",3,"D")^)^)^
let retained = s.retain(s.compact(step_d),[save_a.id,step_d.base.id])^
let roundtrip = parse(format(retained,'mark')^,'mark')^
let offline = t.insert(save_a.replica,"offline",1,"X")^
let checkpoint_message = c.packet(retained.live,offline)^
let resumed = c.receive_packet(offline,checkpoint_message)^
let reference = c.merge(step_d.live,offline)^
let nested_base = m.from_value("nested","seed",{nested:{note:"base"}})^
let nested_open = s.start(nested_base)^
let nested = m.child(nested_base,"nested")^
let note = m.child(nested_base,"note",nested)^
let nested_live = s.update(nested_open,t.replace(nested_base,"alice","live",note)^)^
let deleted_child = s.import_external(nested_live,"delete-child","external",nested_open.base.id,"{}",{})^
let ambiguous_base = s.start(m.from_value("ambiguous","seed",[{n:1},{n:2}])^)^
let ambiguous = s.import_external(ambiguous_base,"replace-node","external",ambiguous_base.base.id,
    "[{n:3},{n:2}]",[{n:3},{n:2}])^
let import_open = s.start(t.from_text("import-retention","seed","A")^)^
let import_b = s.import_external(import_open,"disk-b","branch-b",import_open.base.id,"B","B")^
let import_c = s.import_external(import_b,"disk-c","branch-c",import_b.base.id,"C","C")^
let import_retained = s.retain(s.compact(import_c),[import_open.base.id,import_c.base.id])^
let repeated_open = s.start(t.from_text("repeated","seed","aa")^)^
let repeated = s.import_external(repeated_open,"repeated","external",repeated_open.base.id,"a","a")^;
[len(step_d.versions) == 4 and len(retained.versions) == 2,
 t.read(c.restore(retained.versions[0]))^ == "ab",
 t.read(c.restore(retained.versions[1]))^ == "aBCDb",
 len(retained.live.log) == 0,
 roundtrip == retained,
 t.read(roundtrip.live)^ == "aBCDb",
 checkpoint_message.kind == 'checkpoint',
 c.compact(resumed) == c.compact(reference),
 t.read(resumed)^ == "aXBCDb",
 len(deleted_child.conflicts) == 1,
 m.value(deleted_child.conflicts[0].live_before)^ == {nested:{note:"live"}},
 ambiguous.live == ambiguous_base.live and ambiguous.imports[0].status == 'review',
 s.dirty(ambiguous)^,
 len(import_retained.versions) == 2 and len(import_retained.imports) == 2,
 import_retained.imports[0].status == 'compacted' and import_retained.imports[0].raw == null,
 s.import_external(import_retained,"disk-b","branch-b",import_open.base.id,"B","B")^ == import_retained,
 t.read(import_retained.live)^ == "C",
 all([for (atom in import_retained.live.atoms where contains(import_retained.live.dead,atom.id)^) atom.value == null]),
 repeated.live == repeated_open.live and repeated.imports[0].reason == "sync: ambiguous text correspondence"]
