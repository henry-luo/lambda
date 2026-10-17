import draw: lambda.editor.draw
import model: lambda.editor.draw.model
import interaction: lambda.editor.draw.interaction
let a = model.make_shape("a","rect",80,100,"A")
let b = model.make_shape("b","rect",400,100,"B")
let base = {*:model.initial(model.drawing([a,b])),tool:"connect",zoom:1.0,grid:false}
let begin = draw.update(base,{kind:'begin',x:160,y:140,point:{x:160,y:140},pointer:1});
begin.drag.mode == "connect" and begin.source == "a"
begin.doc == base.doc and len(begin.hist.undo) == 0
let preview = draw.update(begin,{kind:'preview',x:480,y:140,point:{x:480,y:140}})
let edge = model.lookup(preview.doc,preview.selected).shape;
edge.tag == 'connector' and model.attr(edge,"from-shape") == "a" and model.attr(edge,"to-shape") == "b"
preview.hover == "b" and preview.drag.drawn
len(preview.hist.undo) == 0
len(draw.validate(preview.doc)) == 0
let committed = draw.update(preview,{kind:'commit',x:480,y:140,point:{x:480,y:140}});
committed.drag == null and committed.tool == "select" and len(committed.hist.undo) == 1
model.step_history(committed).doc == base.doc
let canceled = interaction.cancel(preview);
canceled.doc == base.doc and canceled.selected == base.selected and canceled.selection == base.selection and len(canceled.hist.undo) == 0
let free_start = draw.update(base,{kind:'begin',x:100,y:400,point:{x:100,y:400},pointer:2})
let free = draw.update(free_start,{kind:'commit',x:300,y:450,point:{x:300,y:450}})
let free_edge = model.lookup(free.doc,free.selected).shape;
model.attr(free_edge,"from-shape") == "" and model.attr(free_edge,"to-shape") == ""
model.endpoints(free.doc,free_edge).from == {x:100,y:400} and model.endpoints(free.doc,free_edge).to == {x:300,y:450}
len(free.hist.undo) == 1 and len(draw.validate(free.doc)) == 0
let attached = draw.update(free_start,{kind:'commit',x:400,y:140,point:{x:400,y:140}});
model.attr(model.lookup(attached.doc,attached.selected).shape,"to-shape") == "b"
let dangling = draw.update(begin,{kind:'commit',x:300,y:400,point:{x:300,y:400}});
model.attr(model.lookup(dangling.doc,dangling.selected).shape,"to-shape") == "" and len(dangling.hist.undo) == 1
let first = draw.update(begin,{kind:'commit',x:161,y:141,point:{x:161,y:141}});
first.source == "a" and first.doc == base.doc and first.drag == null
let second = draw.update(first,{kind:'begin',x:480,y:140,point:{x:480,y:140},pointer:1})
let clicked = draw.update(second,{kind:'commit',x:480,y:140,point:{x:480,y:140}});
model.attr(model.lookup(clicked.doc,clicked.selected).shape,"to-shape") == "b" and len(clicked.hist.undo) == 1
let rejected = draw.update(begin,{kind:'commit',x:180,y:150,point:{x:180,y:150}});
rejected.doc == base.doc and len(rejected.hist.undo) == 0 and rejected.drag == null
let zero = draw.update(free_start,{kind:'preview',x:300,y:450,point:{x:300,y:450}})
let collapsed = draw.update(zero,{kind:'commit',x:100,y:400,point:{x:100,y:400}});
collapsed.doc == base.doc and len(collapsed.hist.undo) == 0
let capped = {*:base,doc:model.with_attr(base.doc,"max-objects",2)};
(draw.update(capped,{kind:'begin',x:100,y:400,point:{x:100,y:400},pointer:1})).drag == null
let outside = draw.update(base,{kind:'begin',x:-20,y:100,point:{x:-20,y:100},pointer:1});
outside.drag == null and outside.doc == base.doc
let seeded = {*:base,next_id:1,doc:model.drawing([a,b,model.edge("e1","a","b")])}
let unique = draw.update(draw.update(seeded,{kind:'begin',x:100,y:400,point:{x:100,y:400},pointer:1}),
    {kind:'commit',x:300,y:450,point:{x:300,y:450}});
unique.selected == "e2" and len(draw.validate(unique.doc)) == 0
draw.decode(parse(format(draw.encode(free.doc)^,'json'),'json')^)^ == free.doc
let scaled = draw.update({*:base,zoom:0.5},{kind:'begin',x:80,y:70,point:{x:160,y:140},pointer:1})
let scaled_done = draw.update(scaled,{kind:'commit',x:240,y:70,point:{x:480,y:140}});
model.attr(model.lookup(scaled_done.doc,scaled_done.selected).shape,"to-shape") == "b" and len(scaled_done.hist.undo) == 1
