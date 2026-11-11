import z: lambda.sync.workspace_zip
import w: lambda.sync.workspace
import p: lambda.sync.protocol
import c: lambda.sync.core
import codec: lambda.sync.codec

fn expected(path) any^ => if (exists(path)) hash(input(path,'binary')^,'sha256')^ else null

pn main() {
    let path = "temp/sync-workspace-roundtrip.zip"
    let original = z.from_archive("roundtrip","seed",input("test/input/zip/sample.docx")^)^
    let json = [for (member in original.base.members where member.path == "data.json") member][0]
    let bytes = w.content("{\"answer\":99}","application/json")^
    let changed = z.update(original,w.transact(original.endpoint,"alice",[
        {kind:'move',entry:json.id,name:"answer.json"},
        {kind:'publish',entry:json.id,content:bytes.reference},
        {kind:'create',type:'dir',parent:w.root,name:"new-empty-dir"},
        {kind:'create',type:'file',parent:w.root,name:"zero",content:w.content(null)^.reference}
    ])^.endpoint,[bytes.object,w.content(null)^.object])^
    let saved = z.save(changed,path,expected(path)^)^
    let reread = z.read_file("copy","copy-seed",path)^
    let capture = z.capture(input(path)^)^
    let answer = [for (member in content(input(path)^)^ where member.name == "answer.json") member][0]
    let before = input(path,'binary')^
    output("external",path,'text')^;
    let blocked = z.save(saved,path) ^ { ^.message == "sync ZIP: target changed; capture external changes before saving" }
    output(before,path,'binary')^;
    let prefix = join([for (i in 1 to 3000) "n"],"")^
    let bulk_tree = <fs kind:'dir',*[for (i in 1 to 20) <fs name:prefix ++ string(i),kind:'file',"b">]>
    output(bulk_tree,"temp/sync-workspace-bulk.zip",{format:'zip',deterministic:true})^
    let bulk = z.from_archive("bulk","bulk",input("temp/sync-workspace-bulk.zip")^)^
    var remote = w.start("bulk")^
    var packets = 0
    var bounded = true
    while (not c.dominates(remote.replica.clock,bulk.endpoint.replica.clock)^) {
        let packet = p.next(bulk.endpoint,w.have(remote)^)^
        bounded = bounded and len(binary(codec.encode(packet)^))^ <= 65536
        remote = p.accept(remote,codec.encode(packet)^,{actors:["bulk"],checkpoints:[]})^
        packets = packets+1
    }
    ;
    [not z.dirty(saved)^,not z.dirty(reread)^,input(answer,'json')^.answer == 99,
     capture.id == saved.base.capture,len(capture.members) == 12,blocked,
     any([for (member in capture.members) member.path == "new-empty-dir" and member.type == 'dir']),
     any([for (member in capture.members) member.path == "zero" and member.content.size == 0]),
     all([for (old in original.base.members where old.type == 'file' and old.id != json.id)
         any([for (member in capture.members) member.path == old.path and member.content == old.content])]),
     packets > 1,bounded,len(w.read(remote.replica)^.entries) == 20]
}
