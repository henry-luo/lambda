import model: ~~.mod_model
import shapes: ~~.mod_shapes
import geom: lambda.editor.mod_geom
let st = model.initial()
len(model.kinds) == 12
all([for (kind in model.kinds) model.valid_node(model.make_shape("x",kind,0,0,"Label"),["x"])])
all([for (kind in model.kinds) len(shapes.outline(kind,{x:0,y:0,width:160,height:80}))>=3])
not geom.point_in_polygon({x:1,y:1},shapes.outline("triangle",{x:0,y:0,width:160,height:80}))
shapes.anchor("diamond",{x:0,y:0,width:160,height:80},{x:1,y:0}) == {x:160,y:40};
(shapes.anchor("parallelogram",{x:0,y:0,width:160,height:80},{x:1,y:0}).x < 160)
model.valid_color("#ABCdef") and not model.valid_color("red") and not model.valid_color("#gggggg")
let styled = model.edit_properties({*:st,selected:"n2"},{variant:"cylinder",pattern:"crosshatch",'pattern-color':"#123456",
    fill:"#fedcba",stroke:"#654321",'stroke-style':"dashed",'stroke-width':4.0,'font-size':20.0,'font-weight':"bold",'text-align':"left",opacity:0.5})^;
len(styled.hist.undo) == 1
model.attr(model.lookup(styled.doc,"n2").shape,"variant") == "cylinder"
model.attr(model.lookup(styled.doc,"n2").shape,"stroke-width") == 4
model.step_history(styled).doc == st.doc
model.restore(parse(format(model.archive(styled.doc),'json'),'json')^)^ == styled.doc
let clip = model.copy_style(styled)
let pasted = model.paste_style({*:clip,selected:"n4"})^;
model.attr(model.lookup(pasted.doc,"n4").shape,"pattern") == "crosshatch"
model.attr(model.lookup(pasted.doc,"n4").shape,"variant") == "rect"
model.attr(model.lookup(pasted.doc,"n4").shape,"fill") == "#fedcba"
let cross = model.paste_style({*:clip,selected:"e1"})^;
model.attr(model.lookup(cross.doc,"e1").shape,"stroke") == "#654321"
model.attr(model.lookup(cross.doc,"e1").shape,"end-arrow") == "triangle"
let linked = model.edit_properties({*:st,selected:"e1"},{'start-arrow':"diamond",'end-arrow':"open",routing:'curved',
    'from-port':"north",'to-port':"south",'stroke-style':"dotted",'stroke-width':3.0,'label-position':0.75})^;
model.endpoints(linked.doc,model.lookup(linked.doc,"e1").shape).from == {x:180,y:300}
model.endpoints(linked.doc,model.lookup(linked.doc,"e1").shape).to == {x:430,y:380}
let reversed = model.reverse_edge(linked)^;
model.attr(model.lookup(reversed.doc,"e1").shape,"from-shape") == "n2"
model.attr(model.lookup(reversed.doc,"e1").shape,"start-arrow") == "diamond"
model.attr(model.lookup(reversed.doc,"e1").shape,"from-port") == "south"
let bent = model.edit_edge_document(st.doc,"e1","bend",{x:300,y:200})
model.edge_handles(bent,model.lookup(bent,"e1").shape).bend == {x:300,y:200}
model.hit(bent,{x:300,y:200}) == "e1"
let reattached = model.edit_edge_document(st.doc,"e1","to",{x:0,y:0},"n4");
model.attr(model.lookup(reattached,"e1").shape,"to-shape") == "n4"
model.edit_edge_document(st.doc,"e1","to",{x:0,y:0},"n1") == st.doc
model.edit_properties({*:st,selected:"e1"},{'from-shape':"n2"}) ^ { true } ~ { false }
model.edit_properties({*:st,selected:"n2"},{'stroke-width':-1}) ^ { true } ~ { false }
model.edit_properties({*:st,selected:"n2"},{pattern:"unsupported"}) ^ { true } ~ { false }
let old_node = model.with_attrs(model.make_shape("legacy","rect",0,0,"Old diagram"),{})
let legacy = model.drawing([{*:old_node,attrs:[for (a in old_node.attrs where contains(['id','kind','x','y','width','height','fill','stroke','stroke-width','variant','label'],a.name)) a]}])
let restored = model.restore(parse(format({version:1,drawing:legacy},'json'),'json')^)^;
model.attr(model.lookup(restored,"legacy").shape,"pattern") == "solid"
model.attr(model.lookup(restored,"legacy").shape,"font-size") == 14
let sample = model.showcase();
model.restore(parse(format(model.archive(sample),'json'),'json')^)^ == sample
model.archive(sample).version == 3
let curved = model.edit_properties({*:st,doc:bent,selected:"e1"},{routing:'curved'})^;
model.hit(curved.doc,{x:297,y:337},2) == "e1"
