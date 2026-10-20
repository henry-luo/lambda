import editor: lambda.editor.raster
import workspace: lambda.editor.raster.workspace
fn check(label,value) => value
let a = {*:editor.open_session(editor.new_document({width:8,height:6})^)^,name:"A"}
let b = {*:editor.open_session(editor.new_document({width:4,height:4})^)^,name:"B"}
let first = workspace.add(workspace.create(),a)^
let edited = editor.exec(a,{id:"layer.new"})^
let both = workspace.add(workspace.capture(first,edited)^,b)^
check("two-documents",len(both.tabs) == 2 and both.active == "document-2")
let back = workspace.activate(both,"document-1",b)^
check("independent-history",len(back.session.hist.undo) == 1 and back.session.doc.width == 8 and workspace.lookup(back.workspace,"document-2").session.doc.width == 4)
let undo = editor.exec(back.session,{id:"history.undo"})^
check("independent-undo",undo.doc == a.doc and workspace.lookup(back.workspace,"document-2").session.doc == b.doc)
check("guard",(workspace.close(workspace.capture(back.workspace,back.session)^,"document-1") ^ { true }) is bool)
let saved = editor.mark_saved(back.session,back.session.doc)
let closed = workspace.close(workspace.capture(back.workspace,saved)^,"document-1")^
check("saved-close",len(closed.tabs) == 1 and closed.active == "document-2")
check("dirty-all",workspace.any_dirty(both,b)^)
check("rows",workspace.rows(both,b)^ == [{id:"document-1",name:"A",dirty:true,active:false},{id:"document-2",name:"B",dirty:false,active:true}])
check("discard",len(workspace.close(back.workspace,"document-1",true)^.tabs) == 1)
check("unknown",(workspace.activate(both,"missing",b) ^ { true }) is bool)
check("no-recursion",workspace.lookup(both,"document-1").session.doc == edited.doc)
