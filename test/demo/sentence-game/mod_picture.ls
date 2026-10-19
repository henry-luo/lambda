import model: .mod_model

fn named(cards,id) => [for (card in cards where card.id == id) card][0]
fn cards(cards,ids) => [for (id in ids) named(cards,id)]
fn location(id,text,art) => {id:id,text:text,art:art,hint:"look in the picture",color:"#e0f4fc"}
fn scene_decks(subject_ids,action_ids,object_ids,locations,time_ids,manner_ids,reason_ids) => {
    subject:cards(model.subjects,subject_ids),predicate:cards(model.predicates,action_ids),object:cards(model.objects,object_ids),
    where:locations,when:cards(model.times,time_ids),how:cards(model.manners,manner_ids),why:cards(model.reasons,reason_ids)
}

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
            when:[named(model.times,"afternoon")],how:cards(model.manners,["quickly","happily"]),why:cards(model.reasons,["fun","sunshine"])}},
    {id:"harvest",title:"Harvest morning on the farm",art:"scene-harvest",advanced:true,
        cue:"It’s an autumn morning on the farm. Find six busy helpers and animals. What is each one doing, and where?",
        alt:"On a sunny autumn farm, a farmer tends a giant pumpkin in the vegetable patch. A gardener waters flowers. A chef stirs a pot of soup at a stall. A musician plays a little drum under a tree. A rabbit nibbles a carrot, and a hen sits beside eggs in a nest near the barn.",
        decks:scene_decks(["farmer","gardener","chef","musician","rabbit","hen"],
            ["grows","waters","stirs","plays","nibbles","eggs"],["pumpkin","flowers","soup","drum","carrot","eggs"],
            [named(model.places,"farm"),named(model.places,"garden"),location("barn","beside the barn","farm"),
                location("tree","under the tree","leaves"),location("vegetables","in the vegetable patch","pumpkin"),location("nest","near the nest","eggs")],
            ["morning","autumn"],["carefully","slowly","happily"],["help","fun","learn"])},
    {id:"woodland",title:"Stories in the woodland",art:"scene-woodland",advanced:true,
        cue:"Six friends are busy in the forest on a summer afternoon. Look for stories, colors, games and tiny treasures.",
        alt:"In a sunny summer woodland, Grandma reads a storybook on a bench under a tree. A teacher paints a rainbow at an easel. A bear picks red berries from a bush. A fox follows a butterfly along the path. A monkey tosses a banana near a tree. A dog chases a red ball in the clearing.",
        decks:scene_decks(["grandma","teacher","bear","fox","monkey","dog"],
            ["reads","paints","picks","follows","tosses","chases"],["storybook","rainbow","berries","butterfly","banana","ball"],
            [named(model.places,"forest"),location("tree","under the tree","leaves"),location("bench","by the bench","park"),
                location("bushes","beside the berry bushes","berries"),location("path","along the path","forest"),location("clearing","in the clearing","park")],
            ["afternoon","summer"],["carefully","quickly","happily"],["fun","sunshine","learn"])},
    {id:"discovery",title:"The school discovery fair",art:"scene-discovery",advanced:true,
        cue:"It’s a spring morning at the school fair. Six very different visitors have something to show. Tell us about them!",
        alt:"At a school discovery fair on a spring morning, a scientist examines a purple crystal at a table. An astronaut carries a red flag beside a moon display. A teal robot dances on a stage. A teacher reads a storybook under a tree. A friendly green dragon paints a rainbow on an easel. A firefighter holds a red hose beside a flower bed.",
        decks:scene_decks(["scientist","astronaut","robot","teacher","dragon","firefighter"],
            ["examines","carries","moonwalk","reads","paints","holds"],["crystal","flag","dance","storybook","rainbow","hose"],
            [named(model.places,"school"),location("stage","on the stage","drum"),location("display","beside the moon display","moon"),
                location("table","at the science table","crystal"),location("tree","under the tree","leaves"),location("flowers","beside the flower bed","flowers")],
            ["morning","spring"],["carefully","quietly","happily"],["learn","help","fun"])}
]
pub fn picture(id) => named(pictures,id)
pub fn card_count(scene) => sum([for (part in model.parts) len(scene.decks[part.id])])
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
