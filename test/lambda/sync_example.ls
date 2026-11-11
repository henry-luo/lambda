import c: lambda.sync.core
import t: lambda.sync.text
import s: lambda.sync.session

let original = s.start(t.from_text("example","seed","Hello world")^)^
let edited = s.update(original,t.insert(original.live,"alice",5,"!")^)^
let imported = s.import_external(edited,"disk-save","external",original.base.id,
    "Hello world?","Hello world?")^
let compacted = s.compact(imported)
let saved = s.save(compacted)^
let offline = t.insert(original.live,"offline",0,"Old peer: ")^
let recovered = c.receive_packet(offline,c.packet(saved.live,offline)^)^;
["Disk: " ++ imported.disk,
 "Editor: " ++ t.read(imported.live)^,
 "Unsaved: " ++ string(s.dirty(imported)^),
 imported.notices[0],
 "Saved: " ++ saved.disk,
 "Offline merged: " ++ t.read(recovered)^]
