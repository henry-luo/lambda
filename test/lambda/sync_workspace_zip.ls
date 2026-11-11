import z: lambda.sync.workspace_zip
import w: lambda.sync.workspace
import codec: lambda.sync.codec

let loaded = z.from_archive("archive","seed",input("test/input/zip/sample.docx")^)^
let member_record = [for (member in loaded.base.members where member.path == "data.json") member][0]
let payload_data = w.content("{\"answer\":43}")^
let changed = z.update(loaded,w.transact(loaded.endpoint,"alice",[
    {kind:'move',entry:member_record.id,name:"answer.json"},
    {kind:'publish',entry:member_record.id,content:payload_data.reference}
])^.endpoint,[payload_data.object])^
let prepared = z.prepare_save(changed)^
let later_content = w.content("{\"answer\":44}")^
let later = z.update(changed,w.transact(changed.endpoint,"alice",[
    {kind:'publish',entry:member_record.id,content:later_content.reference}
])^.endpoint,[later_content.object])^
let committed = z.commit_save(later,prepared)^
let restored = z.load(z.seal(committed)^,"restart")^
let empty = z.from_archive("empty","seed",input("test/input/zip/empty.zip")^)^;
all([
    len(loaded.base.members) == 10,not z.dirty(loaded)^,
    z.dirty(changed)^,z.dirty(committed)^,
    committed.base.checkpoint.frontier == changed.endpoint.replica.clock,
    restored.session == committed,
    z.validate(restored.session)^,
    len(content(z.prepare_save(empty)^.tree)^) == 0,
    w.path(changed.endpoint.replica,member_record.id)^ == "answer.json",
    z.prepare_save({*:changed,objects:[]}) ^ { ^.message == "sync workspace: missing or corrupt content" },
    z.load(z.seal(committed)^,"alice") ^ { ^.message == "sync store: restoration requires a fresh writer actor" },
    z.capture(input("test/input/zip/sample.docx")^,1) ^ { ^.message == "sync ZIP: entry limit" },
    z.capture(input("test/input/zip/sample.docx")^,100,1) ^ { ^.message == "sync ZIP: content limit" },
    z.from_archive("corrupt","seed",input("test/input/zip/corrupt.zip")^) ^ { ^.message != "" },
    z.commit_save(changed,{*:prepared,base_id:codec.digest("other")^}) ^ {
        ^.message == "sync ZIP: disk base changed during save" }
])
