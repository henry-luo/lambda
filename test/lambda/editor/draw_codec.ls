import draw: lambda.editor.draw
import model: lambda.editor.draw.model
import step: lambda.editor.mod_step
let a = draw.vertex("a","rect",{x:80.0,y:100.0,width:120.0,height:80.0},{label:"A",data:{title:"Metadata",enabled:true}})^
let st = draw.exec(draw.open_session(draw.new_document())^,{kind:'insert',objects:[a]})^;
draw.decode(draw.encode(st.doc)^)^ == st.doc
draw.decode(parse(format(draw.encode(st.doc)^,'json'),'json')^)^ == st.doc
draw.encode(st.doc,{version:7}) ^ { true } ~ { false }
let plain = draw.vertex("a","rect",{x:80.0,y:100.0,width:120.0,height:80.0},{label:"A"})^
let original = draw.exec(draw.open_session(draw.new_document())^,{kind:'insert',objects:[plain]})^
let legacy = draw.encode(original.doc,{version:3})^;
legacy.version == 3
model.attr(draw.lookup(draw.decode(legacy)^,"a").shape,"rotation") == 0
let unknown = model.with_attr(a,"variant","unknown")
let invalid = model.drawing([unknown]);
draw.open_session(invalid) ^ { true } ~ { false }
draw.decode({version:4,drawing:{tag:"drawing",attrs:[],content:[]}}) ^ { true } ~ { false }
draw.decode({version:4,drawing:{tag:"drawing",attrs:[{name:"width",value:1},{name:"width",value:2}],content:[]}}) ^ { true } ~ { false }
draw.open_session(draw.new_document({'max-objects':0})) ^ { true } ~ { false }
draw.open_session(original.doc,{zoom:inf}) ^ { true } ~ { false }
draw.edge("bad",{kind:'port',vertex:"a",port:"north"},{kind:'point',x:0,y:0}) ^ { true } ~ { false }
let invalid_stroke = model.with_attr(a,"stroke-width",-2)
let invalid_doc = model.drawing([invalid_stroke]);
draw.open_session(invalid_doc) ^ { true } ~ { false }

let legacy_session = draw.open_session(draw.decode(legacy)^)^
let rotated = draw.exec(legacy_session,{kind:'set',id:"a",properties:{rotate:45.0}})^;
model.attr(draw.lookup(rotated.doc,"a").shape,"rotation") == 45
len(draw.validate(rotated.doc)) == 0
let limited = draw.exec(draw.open_session(draw.new_document({'max-objects':1}))^,{kind:'insert',objects:[plain]})^;
draw.exec(limited,{kind:'duplicate',ids:["a"]}) ^ { true } ~ { false }
