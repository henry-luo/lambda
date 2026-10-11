import model: ~~.mod_model
let initial = model.history()
let warm = {*:model.recipe(),preset:"warm"}
let edited = model.commit_edit(initial,warm)
let back = model.step_back(edited)
model.valid(model.recipe())
not model.valid({*:model.recipe(),gamma:0})
not model.valid({*:model.recipe(),output_width:20})
model.current(model.draft(initial,warm)) == warm
model.discard(model.draft(initial,warm)) == initial
edited.cursor == 1 and len(edited.entries) == 2
model.current(back).preset == "original"
model.current(model.step_forward(back)) == warm
model.commit_edit(edited,warm) == edited
len(model.commit_edit(back,{*:model.recipe(),brightness:0.2}).entries) == 2
model.change(warm,"exposure",30).exposure == 3
model.orient({*:warm,crop:{left:0.1,top:0.1,right:0.9,bottom:0.9}},"rotate").crop == model.recipe().crop
model.orient(warm,"rotate").turns == 3
