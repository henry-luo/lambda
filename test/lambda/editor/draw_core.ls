import draw: lambda.editor.draw
import model: lambda.editor.draw.model
import doc: lambda.editor.mod_doc
import tx: lambda.editor.mod_transaction
import step: lambda.editor.mod_step
fn check(label,condition) => condition
let a = draw.vertex("a","rect",{x:80.0,y:120.0,width:160.0,height:80.0},{label:"Input"})^
let descriptor = {id:"service",outline:[[0.2,0],[1,0],[0.8,1],[0,1]],
    ports:[{id:"input",x:0.0,y:0.5,direction:'in'},{id:"output",x:1.0,y:0.5,direction:'out'}]}
let b = draw.vertex("b",descriptor,{x:360.0,y:120.0,width:160.0,height:100.0},{label:"Service"})^
let e = draw.edge("ab",{kind:'boundary',vertex:"a",side:'east'},{kind:'port',vertex:"b",port:"input"},{label:"flow"})^
let empty = draw.new_document({width:800.0,height:600.0})
let original = draw.open_session(empty)^
let st = draw.exec(original,{kind:'insert',objects:[a,b,e]})^
check("insert",len(draw.vertices(st.doc)) == 2 and len(draw.edges(st.doc)) == 1)
check("history",len(st.hist.undo) == 1 and (draw.exec(st,{kind:'undo'})^).doc == empty)
check("outgoing",model.id(draw.outgoing_edges(st.doc,"a")[0]) == "ab")
check("incoming",model.id(draw.incoming_edges(st.doc,"b")[0]) == "ab")
check("neighbors",draw.neighbors(st.doc,"a") == ["b"])
check("port",draw.resolve_endpoint(st.doc,e,"to") == {x:360,y:170})
check("custom-pick",draw.hit(st.doc,{x:365,y:125}) == "" and draw.hit(st.doc,{x:440,y:170}) == "b")
let turned = draw.exec(st,{kind:'set',id:"b",properties:{rotate:90.0,'flip-x':true}})^
check("transformed-port",draw.resolve_endpoint(turned.doc,e,"to") == {x:440,y:250})
check("custom-svg",contains(format(draw.to_svg(turned.doc),'xml'),"polygon"))
let free = draw.exec(st,{kind:'reconnect',id:"ab",side:"to",endpoint:{kind:'point',x:650.0,y:300.0}})^
check("detach",draw.incoming_edges(free.doc,"b") == [] and draw.resolve_endpoint(free.doc,draw.edges(free.doc)[0],"to") == {x:650,y:300})
check("invalid-port",draw.exec(st,{kind:'reconnect',id:"ab",side:"to",endpoint:{kind:'port',vertex:"b",port:"missing"}}) ^ { true } ~ { false })
check("port-direction",draw.exec(st,{kind:'reconnect',id:"ab",side:"to",endpoint:{kind:'port',vertex:"b",port:"output"}}) ^ { true } ~ { false })
let removed = draw.exec(st,{kind:'delete',ids:["b"]})^
check("delete-incident",len(draw.vertices(removed.doc))==1 and draw.edges(removed.doc)==[])
check("undo-topology",(draw.exec(removed,{kind:'undo'})^).doc == st.doc)
check("duplicate-id",draw.exec(st,{kind:'insert',objects:[a]}) ^ { true } ~ { false })
check("missing-id",draw.exec(st,{kind:'set',id:"missing",properties:{label:"lost"}}) ^ { true } ~ { false })
let saved = parse(format(draw.encode(st.doc)^,'json'),'json')^
check("archive-v4",saved.version == 4)
check("archive-roundtrip",draw.decode(saved)^ == st.doc)
check("page-roundtrip",model.page(draw.decode(saved)^) == {width:800,height:600})
check("legacy-refuses-loss",draw.encode(st.doc,{version:3}) ^ { true } ~ { false })
check("invalid-size",draw.open_session(draw.new_document({width:-1.0})) ^ { true } ~ { false })
let root = doc.node('doc',[doc.node('paragraph',[doc.text("before")]),st.doc])
let change = draw.transaction(root,{drawing_path:[1]},{kind:'set',id:"a",properties:{label:"Edited"}})^
check("embedded-edit",doc.node_at(change.doc_after,[0]) == root.content[0] and model.attr(draw.lookup(change.doc_after.content[1],"a").shape,"label") == "Edited")
check("embedded-inverse",tx.tx_invert(change).doc_after == root)
let same = draw.transaction(st.doc,null,{kind:'set',id:"a",properties:{label:"Input"}})^
check("no-op",same == null)

check("free-neighbors",draw.neighbors(free.doc,"a") == [])
let cleared = draw.exec(st,{kind:'set',id:"a",properties:{data:{value:1}}})^
let removed_data = draw.exec(cleared,{kind:'set',id:"a",properties:{data:null}})^;
check("metadata-null",model.attr(draw.lookup(removed_data.doc,"a").shape,"data") == null)
