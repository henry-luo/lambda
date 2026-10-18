import model: ~~.mod_model
import codec: lambda.editor.draw.codec
import core: lambda.editor.draw
let start = model.starter()
let st = model.select_ids(model.initial(),["e1"])
let edited = model.edit_properties(st,{waypoints:[{x:280,y:450},{x:320,y:450}]})^;
model.restore_archive(parse(format(codec.encode(edited.doc)^,'json'),'json')^)^ == edited.doc
model.restore_archive(parse(format(model.archive(start),'json'),'json')^)^ == start
model.restore_archive(codec.encode(core.new_document({width:2000,height:760}))^) ^ { true } ~ { false }
let invalid = model.with_attr(model.objects(start)[0],"unknown",true);
model.restore_archive({version:4,drawing:model.drawing([invalid])}) ^ { true } ~ { false }
