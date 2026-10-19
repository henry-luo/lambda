import model: .mod_model

fn named(cards,id) => [for (card in cards where card.id == id) card][0]
fn cards(cards,ids) => [for (id in ids) named(cards,id)]
fn location(id,text,art) => {id:id,text:text,art:art,hint:"look in the picture",color:"#e0f4fc"}

// the pictured actors and props are the source of each reusable card bank.
pub let pictures = [
    {id:"park",title:"An afternoon in the park",art:"scene-park",cue:"It’s a sunny afternoon. Look at the people, the dog, and the things around them.",
        alt:"In a sunny park, Grandma reads a book on a bench under a tree. A dog chases a red ball. A teacher paints a rainbow on an easel.",
        decks:{subject:cards(model.subjects,["grandma","dog","teacher"]),predicate:cards(model.predicates,["reads","chases","paints"]),
            object:cards(model.objects,["storybook","ball","rainbow"]),
            where:[named(model.places,"park"),location("tree","under the tree","leaves"),location("bench","by the bench","park")],
            when:[named(model.times,"afternoon")],how:cards(model.manners,["carefully","happily"]),why:cards(model.reasons,["fun","sunshine"])}},
    {id:"kitchen",title:"A busy morning in the kitchen",art:"scene-kitchen",cue:"It’s breakfast time. Find the cook, the helper, and a very fancy cat.",
        alt:"In a bright kitchen, Grandma bakes blueberry pancakes at the cooker. A robot carries a carrot near the table. A cat wears a funny cap beside the window.",
        decks:{subject:cards(model.subjects,["grandma","robot","cat"]),predicate:[named(model.predicates,"pancakes"),
                {*:named(model.predicates,"carries"),text:"carries a crunchy carrot",object:"carrot"},named(model.predicates,"cap")],
            object:cards(model.objects,["pancakes","carrot","cap"]),
            where:[named(model.places,"kitchen"),location("window","by the window","morning"),location("table","at the table","lunch")],
            when:[named(model.times,"morning")],how:cards(model.manners,["carefully","slowly"]),why:cards(model.reasons,["help","learn"])}},
    {id:"beach",title:"An adventure at the beach",art:"scene-beach",cue:"The afternoon sun is shining. What is happening in the water and on the sand?",
        alt:"At the beach in the afternoon, a tiny pirate sails a boat in the water. A dog finds a shiny shell on the sand. A cat carries a little flag near the sea.",
        decks:{subject:cards(model.subjects,["pirate","dog","cat"]),predicate:[
                {*:named(model.predicates,"bathtub"),text:"sails a tiny boat"},named(model.predicates,"finds"),named(model.predicates,"carries")],
            object:cards(model.objects,["boat","shell","flag"]),
            where:[named(model.places,"beach"),location("sand","on the sand","beach"),location("sea","near the sea","beach")],
            when:[named(model.times,"afternoon")],how:cards(model.manners,["quickly","happily"]),why:cards(model.reasons,["fun","sunshine"])}}
]
pub fn picture(id) => named(pictures,id)
pub fn new_game(seed=42) => {*:model.new_game(seed),phase:"picture",scene:"",notice:"",unlimited:true}
fn choose_picture(st,id) => if (st.phase != "picture" or picture(id) == null) st
    else model.choose_pattern({*:new_game(st.seed),book:st.book,scene:id,decks:picture(id).decks,phase:"pattern"},"triple")
fn save_description(st) {
    let saved = model.save_sentence(st)
    if (saved == st) st
    // saving clears the sentence while keeping every card available again.
    else model.clear_choices({*:saved,clauses:[],link:"",previous:null,notice:"Added to your book! Use these cards again to describe something else."})
}
pub fn action(st,command,value="") => if (command == "reset") new_game(st.seed)
    else if (command == "picture") choose_picture(st,value)
    else if (command == "pictures") {*:st,phase:"picture"}
    else if (command == "recipe" and st.phase == "build" and model.pattern(value) != null) model.choose_pattern(model.show_patterns(st),value)
    else if (command == "primary") save_description(st)
    else if (command == "clear-description" and st.phase == "build") model.clear_choices({*:st,clauses:[],link:"",previous:null,notice:""})
    else if (st.phase == "build") model.action({*:st,notice:""},command,value) else st
pub fn shortcut(st,key) {
    let changed = if (st.phase == "build") model.shortcut(st,key) else st
    if (changed == st) st else {*:changed,notice:""}
}
