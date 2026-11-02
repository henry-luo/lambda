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
    match (storage.restore_reply(null) ^ { ^ }) { case error: true default: false },
    match (storage.restore_reply({path:"",status:"",snapshot:{}}) ^ { ^ }) { case error: true default: false },
    opening.process_reply(success)^==wire,
    opening.process_reply({*:success,stdout:binary(success.stdout)})^==wire,
    { let __error_outcome_1 = opening.process_reply({*:success,timed_out:true}) ^ { ^ }; if (__error_outcome_1 is error) { contains(__error_outcome_1.message,"timed out") } else match __error_outcome_1 { case any { false } } },
    { let __error_outcome_2 = opening.process_reply({*:success,cancelled:true}) ^ { ^ }; if (__error_outcome_2 is error) { contains(__error_outcome_2.message,"cancelled") } else match __error_outcome_2 { case any { false } } },
    { let __error_outcome_3 = opening.process_reply({*:success,truncated:true}) ^ { ^ }; if (__error_outcome_3 is error) { contains(__error_outcome_3.message,"too large") } else match __error_outcome_3 { case any { false } } },
    match (opening.process_reply({*:success,exit_code:1}) ^ { ^ }) { case error: true default: false },
    match (opening.process_reply({*:success,stdout:"not JSON"}) ^ { ^ }) { case error: true default: false },
    { let __error_outcome_4 = opening.process_reply({*:success,stdout:format({error:"conversion failed"},'json')^}) ^ { ^ }; if (__error_outcome_4 is error) { __error_outcome_4.message=="conversion failed" } else match __error_outcome_4 { case any { false } } },
    opening.accepts(dialog,{seq:7}) and not opening.accepts(dialog,{seq:6}),
    not opening.accepts({*:dialog,loading:false},{seq:7}) and not opening.accepts(null,{seq:7}),
    not picker.can_open({loading:true,path:"a.docx"}),
    contains(model.plain_text(partial.editor.doc),"Unsupported numbering sample") and len(partial.warnings)==1,
    contains(partial.warnings[0].message,"ordinalText"),
    contains(storage.html_document(japanese.editor.doc,"Japanese")^,"list-style-type:japanese-informal") and len(japanese.warnings)==0,
    len(loaded.warnings)==0,
    match (storage.restore_reply({*:wire,warnings:[{message:7}]}) ^ { ^ }) { case error: true default: false },
    { let __error_outcome_5 = storage.read_document("test/input/docx/malformed.docx") ^ { ^ }; if (__error_outcome_5 is error) { __error_outcome_5.code==7604 } else match __error_outcome_5 { case any { false } } },
    model.plain_text(embedded.editor.doc)=="HeaderLeftRight" and embedded.warnings[0].code=="editable-content",
    storage.restore(embedded_archive)^.doc==embedded.editor.doc and embedded_archive.warnings==null,
    not contains(storage.html_document(embedded.editor.doc,"Embedded")^,"Imported with warnings"),
    opening.document_argument(["lambda","view","word.ls","--headless"],"/project","/home/me")^==null,
    opening.document_argument(["--doc","draft.word.json"],"/project","/home/me")^.path=="/project/draft.word.json",
    opening.document_argument(["--headless","--doc","../Draft document.docx","--event-file","trace.json"],"/project/work","/home/me")^.path=="/project/Draft document.docx",
    opening.document_argument(["--doc","/docs/paper.md"],"/project","/home/me")^.path=="/docs/paper.md",
    opening.document_argument(["--doc","~/Documents/draft.txt"],"/project","/home/me")^.path=="/home/me/Documents/draft.txt",
    opening.document_argument(["--doc=A document.word.json"],"/project","/home/me")^.path=="/project/A document.word.json",
    { let __error_outcome_6 = opening.document_argument(["--doc"],"/project","/home/me") ^ { ^ }; if (__error_outcome_6 is error) { contains(__error_outcome_6.message,"requires a document path") } else match __error_outcome_6 { case any { false } } },
    match (opening.document_argument(["--doc",""],"/project","/home/me") ^ { ^ }) { case error: true default: false },
    match (opening.document_argument(["--doc","--headless"],"/project","/home/me") ^ { ^ }) { case error: true default: false },
    { let __error_outcome_7 = opening.document_argument(["--doc","a.txt","--doc","b.txt"],"/project","/home/me") ^ { ^ }; if (__error_outcome_7 is error) { contains(__error_outcome_7.message,"only once") } else match __error_outcome_7 { case any { false } } },
    match (opening.document_argument(["--doc="],"/project","/home/me") ^ { ^ }) { case error: true default: false },
    opening.document_argument(["--doc"," Draft .txt "],"/project","/home/me")^.path=="/project/ Draft .txt ",
    opening.document_argument(["--document","a.txt"],"/project","/home/me")^==null,
    match (opening.document_argument(["--doc=a.txt","--doc","b.txt"],"/project","/home/me") ^ { ^ }) { case error: true default: false },
    opening.document_argument(["--doc=--draft.md"],"/project","/home/me")^.path=="/project/--draft.md"
]
