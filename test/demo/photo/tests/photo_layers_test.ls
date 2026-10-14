import layers: ~~.mod_layers
import model: ~~.mod_model
let initial = model.recipe()
let text = layers.create(initial,"text","A <story> & a memory")
let first = {*:initial,layers:[text]}
let heart = layers.create(first,"heart")
let both = {*:first,layers:[text,heart]}
layers.valid(text) and model.valid(both)
not model.valid({*:first,layers:[text,text]})
not model.valid({*:first,layers:[{*:text,color:"url(x)"}]})
not model.valid({*:first,layers:[{*:text,width:0}]})
not model.valid({*:first,layers:[{*:text,kind:"unknown"}]})
layers.hit(both,0.5,0.5,1.5) == heart.id
layers.hit(both,0.01,0.01,1.5) == null
layers.reorder(both,heart.id,-1).layers[0].id == heart.id
layers.reorder(both,text.id,-1) == both
len(layers.remove(both,text.id).layers) == 1
len(layers.duplicate(both,text.id).layers) == 3
layers.duplicate(both,text.id).layers[last].id == 3
layers.change(first,text.id,"opacity",2).layers[0].opacity == 1
layers.change(first,text.id,"color","#123abc").layers[0].color == "#123abc"
layers.change(first,text.id,"color","invalid") == first
layers.drag(text,"move",-5,5).x == 0 and layers.drag(text,"move",-5,5).y == 1-text.height
layers.drag(text,"resize",-5,5).width == 0.02 and layers.drag(text,"resize",-5,5).height == 1
let changed = layers.change(first,text.id,"text","New title")
first.layers[0].text == "A <story> & a memory" and changed.layers[0].text == "New title"
let history = model.commit_edit(model.history(),first)
model.current(model.step_forward(model.step_back(history))) == first
contains(format(layers.svg(first,1200,800),'xml'),"A &lt;story&gt; &amp; a memory");
(model.change(initial,"black",1).black < initial.white);
(model.change({*:initial,black:0.8},"white",0).white > 0.8);
model.upgrade({preset:"warm"}).layers == []
let rotated = {*:text,x:0.45,y:0.2,width:0.1,height:0.6,rotation:90}
layers.hit({*:initial,layers:[rotated]},0.65,0.5,1.5) == text.id
layers.hit({*:initial,layers:[rotated]},0.5,0.7,1.5) == null
layers.change(first,text.id,"x","") == first
layers.change(first,text.id,"width","-") == first
