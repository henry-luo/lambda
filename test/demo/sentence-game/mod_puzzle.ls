import model: .mod_model
import progress: .mod_rounds

// each lesson has accepted readings; duplicate word cards remain interchangeable.
pub let lessons = [
    {id:"fetch",topic:"A playful dog",art:"dog",clue:"Tell us what the dog chases.",answers:["The dog chases a ball."]},
    {id:"breakfast",topic:"In the kitchen",art:"pancakes",clue:"Tell us where Grandma bakes pancakes.",answers:["Grandma bakes pancakes in the kitchen."]},
    {id:"rabbit",topic:"A garden snack",art:"rabbit",clue:"Tell us what the rabbit eats and where.",answers:["The rabbit eats a carrot in the garden."]},
    {id:"reading",topic:"Story time",art:"book",clue:"Tell us where the teacher reads a book.",answers:["The teacher reads a book at school."]},
    {id:"treasure",topic:"A seaside discovery",art:"shell",clue:"Tell us what the cat finds at the beach.",answers:["The cat finds a shell at the beach."]},
    {id:"space",topic:"A moon adventure",art:"astronaut",clue:"Tell us what the astronaut carries on the moon.",answers:["The astronaut carries a flag on the moon."]},
    {id:"playtime",topic:"After lunch",art:"park",clue:"Tell us where and when the children play.",answers:["The children play in the park after lunch.","The children play after lunch in the park."]},
    {id:"sunshine",topic:"A bright start",art:"morning",clue:"Tell us when the sun shines.",answers:["The sun shines in the morning."]}
]
fn word(st,id) => [for (card in st.cards where card.id == id) card][0]
pub fn lesson(st) => lessons[st.round-1]
pub fn available(st) => [for (card in st.cards where not contains(st.order,card.id)) card]
fn deal(st) {
    let words = split(replace(lesson(st).answers[0],"."," .")," ")
    let cards = [for (i,text in words) {id:string(i),text:text}]
    let bank = model.scrambled(cards,st.seed)
    {*:st,cards:bank,order:[],phase:"build",notice:"",hint:false,seed:st.seed+len(cards)}
}
let round_fields = ["seed","cards","order","phase","notice","hint"]
pub fn new_game(seed=42,round_id=1) => deal({seed:seed,round:round_id,book:[],visits:{}})
pub fn jump_round(st,target) => progress.jump(st,target,len(lessons),round_fields,new_game,"solved")
pub fn placed(st) => [for (id in st.order) word(st,id)]
pub fn assembled(st) => join([for (i,card in placed(st)) (if (i == 0 or card.text == ".") "" else " ") ++ card.text],"")
pub fn full(st) => len(st.order) == len(st.cards)
fn accepted(st) => [for (answer in lesson(st).answers where lower(assembled(st)) == lower(answer)) answer][0]
fn put_word(st,id) => if (st.phase != "build" or word(st,id) == null or contains(st.order,id)) st
    else {*:st,order:[*st.order,id],notice:""}
fn remove_word(st,id) => if (st.phase != "build") st else {*:st,order:[for (value in st.order where value != id) value],notice:""}
fn move_word(st,id,step) {
    let from = [for (i,value in st.order where value == id) i][0]
    let to = if (from == null) -1 else from + step
    if (st.phase != "build" or from == null or to < 0 or to >= len(st.order)) st
    else {*:st,order:[for (i,value in st.order) if (i == from) st.order[to] else if (i == to) id else value],notice:""}
}
fn check(st) {
    let answer = accepted(st)
    if (st.phase != "build" or not full(st)) st
    else if (answer == null) {*:st,notice:"Not quite yet. Follow the clue, put who before the action, and finish with a full stop."}
    else {*:st,phase:"solved",notice:"You unscrambled it! Read your sentence aloud.",book:progress.record(st,{text:answer,art:lesson(st).art})}
}
fn next(st) => if (st.phase != "solved") st
    else if (len(st.book) == len(lessons)) {*:st,phase:"done"}
    else jump_round(st,progress.pending(st,len(lessons)))
pub fn action(st,command,value="") => if (command == "reset") new_game(st.seed)
    else if (command == "primary") (if (st.phase == "solved") next(st) else check(st))
    else if (command == "round") progress.requested(st,value,len(lessons),round_fields,new_game,"solved")
    else if (command == "word") put_word(st,value)
    else if (command == "remove-word") remove_word(st,value)
    else if (command == "word-left") move_word(st,value,-1)
    else if (command == "word-right") move_word(st,value,1)
    else if (command == "undo-word") remove_word(st,st.order[len(st.order)-1])
    else if (command == "clear-words" and st.phase == "build") {*:st,order:[],notice:""}
    else if (command == "hint" and st.phase == "build") {*:st,hint:not st.hint}
    else st
pub fn shortcut(st,key) {
    let index = model.bank_index(key)
    if (key == "backspace") action(st,"undo-word")
    else if (index != null) put_word(st,available(st)[index].id) else st
}
