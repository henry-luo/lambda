import storage: ~~.mod_io
import opening: ~~.mod_open
import picker: ~~.mod_file_picker
import model: ~~.mod_model

let source = "test/demo/word/tests/import.md"
let original = storage.read_document(source)^
let wire = parse(format(storage.open_reply(source)^,'json')^,'json')^
let loaded = storage.restore_reply(wire)^
let editable = "test/demo/word/tests/fixtures/picker.word.json"
let snapshot = storage.restore_reply(storage.open_reply(editable)^)^
let dialog = {kind:"open",loading:true,seq:7}
let success = {exit_code:0,stdout:format(wire,'json')^};
let partial = storage.restore_reply(parse(format(storage.open_reply("test/input/docx/unsupported-numbering.docx")^,'json')^,'json')^)^
let japanese = storage.read_document("test/input/docx/japanese-numbering.docx")^
let embedded = storage.restore_reply(storage.open_reply("test/input/docx/diagram-composite.docx")^)^
let embedded_archive = storage.archive(embedded.editor.doc,"Embedded");
[
    loaded.editor.doc==original.editor.doc,
    loaded.session.path=="" and loaded.session.envelope.title==original.session.envelope.title,
    loaded.status==original.status,
    snapshot.session.path==editable and snapshot.session.disk_text==input(editable,'text')^,
    storage.restore_reply(null) ^ { true } ~ { false },
    storage.restore_reply({path:"",status:"",snapshot:{}}) ^ { true } ~ { false },
    opening.process_reply(success)^==wire,
    opening.process_reply({*:success,stdout:binary(success.stdout)})^==wire,
    opening.process_reply({*:success,timed_out:true}) ^ { contains(^.message,"timed out") } ~ { false },
    opening.process_reply({*:success,cancelled:true}) ^ { contains(^.message,"cancelled") } ~ { false },
    opening.process_reply({*:success,truncated:true}) ^ { contains(^.message,"too large") } ~ { false },
    opening.process_reply({*:success,exit_code:1}) ^ { true } ~ { false },
    opening.process_reply({*:success,stdout:"not JSON"}) ^ { true } ~ { false },
    opening.process_reply({*:success,stdout:format({error:"conversion failed"},'json')^}) ^ { ^.message=="conversion failed" } ~ { false },
    opening.accepts(dialog,{seq:7}) and not opening.accepts(dialog,{seq:6}),
    not opening.accepts({*:dialog,loading:false},{seq:7}) and not opening.accepts(null,{seq:7}),
    not picker.can_open({loading:true,path:"a.docx"}),
    contains(model.plain_text(partial.editor.doc),"Unsupported numbering sample") and len(partial.warnings)==1,
    contains(partial.warnings[0].message,"ordinalText"),
    contains(storage.html_document(japanese.editor.doc,"Japanese")^,"list-style-type:japanese-informal") and len(japanese.warnings)==0,
    len(loaded.warnings)==0,
    storage.restore_reply({*:wire,warnings:[{message:7}]}) ^ { true } ~ { false },
    storage.read_document("test/input/docx/malformed.docx") ^ { ^.code==7604 } ~ { false },
    model.plain_text(embedded.editor.doc)=="HeaderLeftRight" and embedded.warnings[0].code=="editable-content",
    storage.restore(embedded_archive)^.doc==embedded.editor.doc and embedded_archive.warnings==null,
    not contains(storage.html_document(embedded.editor.doc,"Embedded")^,"Imported with warnings"),
    opening.document_argument(["lambda","view","word.ls","--headless"],"/project","/home/me")^==null,
    opening.document_argument(["--doc","draft.word.json"],"/project","/home/me")^.path=="/project/draft.word.json",
    opening.document_argument(["--headless","--doc","../Draft document.docx","--event-file","trace.json"],"/project/work","/home/me")^.path=="/project/Draft document.docx",
    opening.document_argument(["--doc","/docs/paper.md"],"/project","/home/me")^.path=="/docs/paper.md",
    opening.document_argument(["--doc","~/Documents/draft.txt"],"/project","/home/me")^.path=="/home/me/Documents/draft.txt",
    opening.document_argument(["--doc=A document.word.json"],"/project","/home/me")^.path=="/project/A document.word.json",
    opening.document_argument(["--doc"],"/project","/home/me") ^ { contains(^.message,"requires a document path") } ~ { false },
    opening.document_argument(["--doc",""],"/project","/home/me") ^ { true } ~ { false },
    opening.document_argument(["--doc","--headless"],"/project","/home/me") ^ { true } ~ { false },
    opening.document_argument(["--doc","a.txt","--doc","b.txt"],"/project","/home/me") ^ { contains(^.message,"only once") } ~ { false },
    opening.document_argument(["--doc="],"/project","/home/me") ^ { true } ~ { false },
    opening.document_argument(["--doc"," Draft .txt "],"/project","/home/me")^.path=="/project/ Draft .txt ",
    opening.document_argument(["--document","a.txt"],"/project","/home/me")^==null,
    opening.document_argument(["--doc=a.txt","--doc","b.txt"],"/project","/home/me") ^ { true } ~ { false },
    opening.document_argument(["--doc=--draft.md"],"/project","/home/me")^.path=="/project/--draft.md"
]
