// Immutable game transitions keep the reactive view pure (S1.4, S12.1.3).
import progress: .mod_rounds
pub let rounds = 5
pub let subjects = [
    {id:"hen",text:"The hen",art:"hen",hint:"a feathered friend",color:"#fff0d2"},
    {id:"giraffe",text:"The giraffe",art:"giraffe",hint:"very tall indeed",color:"#fff3bf"},
    {id:"joker",text:"The joker",art:"joker",hint:"ready for a laugh",color:"#f4e4ff"},
    {id:"pirate",text:"A tiny pirate",art:"pirate",hint:"captain of the tub",color:"#dff1ff"},
    {id:"robot",text:"My robot",art:"robot",hint:"beep-boop buddy",color:"#e1f4e7"},
    {id:"grandma",text:"Grandma",art:"grandma",hint:"the best storyteller",color:"#ffe6e2",proper:true},
    {id:"dog",text:"The dog",art:"dog",hint:"a playful pup",color:"#fff0d2"},
    {id:"cat",text:"The cat",art:"cat",hint:"a curious companion",color:"#ffe6e2"},
    {id:"rabbit",text:"The rabbit",art:"rabbit",hint:"a bouncy little friend",color:"#f4e4ff"},
    {id:"teacher",text:"The teacher",art:"teacher",hint:"full of bright ideas",color:"#e1f4e7"},
    {id:"astronaut",text:"The astronaut",art:"astronaut",hint:"ready to explore",color:"#e8eaff"},
    {id:"dragon",text:"A friendly dragon",art:"dragon",hint:"a gentle giant",color:"#dff1ff"},
    {id:"chef",text:"The chef",art:"chef",hint:"a kitchen superstar",color:"#ffe6e2"},
    {id:"firefighter",text:"The firefighter",art:"firefighter",hint:"ready to help",color:"#fff0d2"},
    {id:"farmer",text:"The farmer",art:"farmer",hint:"up with the sun",color:"#fff2c6"},
    {id:"gardener",text:"The gardener",art:"gardener",hint:"green fingers",color:"#e1f4e7"},
    {id:"musician",text:"The musician",art:"musician",hint:"a wonderful rhythm",color:"#f4e4ff"},
    {id:"scientist",text:"The scientist",art:"scientist",hint:"full of questions",color:"#dff1ff"},
    {id:"bear",text:"The bear",art:"bear",hint:"a furry forest friend",color:"#fff0d2"},
    {id:"fox",text:"The fox",art:"fox",hint:"a curious explorer",color:"#ffe6e2"},
    {id:"penguin",text:"The penguin",art:"penguin",hint:"a little waddler",color:"#e8eaff"},
    {id:"monkey",text:"The monkey",art:"monkey",hint:"a cheeky climber",color:"#fff2c6"}
]
pub let predicates = [
    {id:"eggs",text:"lays eggs",verb:"lays",object:"eggs",art:"eggs",hint:"a fresh little nest",color:"#fff2c6",match:"hen"},
    {id:"leaves",text:"eats the tallest leaves",verb:"eats",object:"leaves",art:"leaves",hint:"stretch, stretch, stretch",color:"#e1f4e7",match:"giraffe"},
    {id:"cap",text:"wears a funny cap",verb:"wears",object:"cap",art:"cap",hint:"a very fancy look",color:"#f4e4ff",match:"joker"},
    {id:"bathtub",text:"sails around the bathtub",verb:"sails",object:"boat",art:"bathtub",hint:"all aboard the bubbles",color:"#dff1ff",match:"pirate"},
    {id:"moonwalk",text:"does a robot dance",verb:"does",object:"dance",art:"moonwalk",hint:"to a beep-boop beat",color:"#e8eaff",match:"robot"},
    {id:"pancakes",text:"bakes blueberry pancakes",verb:"bakes",object:"pancakes",art:"pancakes",hint:"flip, flip, hooray",color:"#ffe6e2",match:"grandma"},
    {id:"chases",text:"chases a red ball",verb:"chases",object:"ball",art:"ball",hint:"ready, set, fetch",color:"#ffe6e2",match:"dog"},
    {id:"finds",text:"finds a shiny shell",verb:"finds",object:"shell",art:"shell",hint:"a tiny treasure",color:"#dff1ff",match:"cat"},
    {id:"nibbles",text:"nibbles a crunchy carrot",verb:"nibbles",object:"carrot",art:"carrot",hint:"crunch, crunch",color:"#fff0d2",match:"rabbit"},
    {id:"reads",text:"reads a storybook",verb:"reads",object:"storybook",art:"book",hint:"once upon a time",color:"#fff2c6",match:"teacher"},
    {id:"carries",text:"carries a little flag",verb:"carries",object:"flag",art:"flag",hint:"off on an adventure",color:"#e8eaff",match:"astronaut"},
    {id:"paints",text:"paints a rainbow",verb:"paints",object:"rainbow",art:"rainbow",hint:"a splash of color",color:"#e1f4e7",match:"dragon"},
    {id:"stirs",text:"stirs a pot of soup",verb:"stirs",object:"soup",art:"soup",hint:"round and round",color:"#ffe6e2",match:"chef"},
    {id:"holds",text:"holds a red hose",verb:"holds",object:"hose",art:"hose",hint:"a helping hand",color:"#fff0d2",match:"firefighter"},
    {id:"grows",text:"grows a giant pumpkin",verb:"grows",object:"pumpkin",art:"pumpkin",hint:"from a tiny seed",color:"#fff2c6",match:"farmer"},
    {id:"waters",text:"waters bright flowers",verb:"waters",object:"flowers",art:"flowers",hint:"a little drink",color:"#e1f4e7",match:"gardener"},
    {id:"plays",text:"plays a little drum",verb:"plays",object:"drum",art:"drum",hint:"tap, tap, tap",color:"#f4e4ff",match:"musician"},
    {id:"examines",text:"examines a shiny crystal",verb:"examines",object:"crystal",art:"crystal",hint:"look a little closer",color:"#dff1ff",match:"scientist"},
    {id:"picks",text:"picks sweet berries",verb:"picks",object:"berries",art:"berries",hint:"a tasty discovery",color:"#ffe6e2",match:"bear"},
    {id:"follows",text:"follows a colorful butterfly",verb:"follows",object:"butterfly",art:"butterfly",hint:"flutter, flutter",color:"#f4e4ff",match:"fox"},
    {id:"pushes",text:"pushes a wooden sled",verb:"pushes",object:"sled",art:"sled",hint:"slide through the snow",color:"#e8eaff",match:"penguin"},
    {id:"tosses",text:"tosses a ripe banana",verb:"tosses",object:"banana",art:"banana",hint:"up it goes",color:"#fff2c6",match:"monkey"}
]
pub let objects = [
    {id:"eggs",text:"eggs",art:"eggs",hint:"a little breakfast",color:"#fff2c6"},
    {id:"leaves",text:"the tallest leaves",art:"leaves",hint:"a leafy snack",color:"#e1f4e7"},
    {id:"cap",text:"a funny cap",art:"cap",hint:"something to wear",color:"#f4e4ff"},
    {id:"boat",text:"a tiny boat",art:"bathtub",hint:"a little adventure",color:"#dff1ff"},
    {id:"dance",text:"a robot dance",art:"moonwalk",hint:"some fancy footwork",color:"#e8eaff"},
    {id:"pancakes",text:"blueberry pancakes",art:"pancakes",hint:"a tasty stack",color:"#ffe6e2"},
    {id:"ball",text:"a red ball",art:"ball",hint:"bounce, bounce",color:"#ffe6e2"},
    {id:"shell",text:"a shiny shell",art:"shell",hint:"a seaside treasure",color:"#dff1ff"},
    {id:"carrot",text:"a crunchy carrot",art:"carrot",hint:"a tasty orange snack",color:"#fff0d2"},
    {id:"storybook",text:"a storybook",art:"book",hint:"pages of adventure",color:"#fff2c6"},
    {id:"flag",text:"a little flag",art:"flag",hint:"fluttering in the breeze",color:"#e8eaff"},
    {id:"rainbow",text:"a rainbow",art:"rainbow",hint:"all the happy colors",color:"#e1f4e7"},
    {id:"soup",text:"a pot of soup",art:"soup",hint:"warm and delicious",color:"#ffe6e2"},
    {id:"hose",text:"a red hose",art:"hose",hint:"a splash of water",color:"#fff0d2"},
    {id:"pumpkin",text:"a giant pumpkin",art:"pumpkin",hint:"a wonderful harvest",color:"#fff2c6"},
    {id:"flowers",text:"bright flowers",art:"flowers",hint:"petals of every color",color:"#e1f4e7"},
    {id:"drum",text:"a little drum",art:"drum",hint:"make some music",color:"#f4e4ff"},
    {id:"crystal",text:"a shiny crystal",art:"crystal",hint:"a sparkling treasure",color:"#dff1ff"},
    {id:"berries",text:"sweet berries",art:"berries",hint:"a juicy snack",color:"#ffe6e2"},
    {id:"butterfly",text:"a colorful butterfly",art:"butterfly",hint:"delicate little wings",color:"#f4e4ff"},
    {id:"sled",text:"a wooden sled",art:"sled",hint:"ready for a snow day",color:"#e8eaff"},
    {id:"banana",text:"a ripe banana",art:"banana",hint:"a golden treat",color:"#fff2c6"}
]
pub let places = [
    {id:"park",text:"in the park",art:"park",hint:"out among the trees",color:"#e1f4e7"},
    {id:"beach",text:"at the beach",art:"beach",hint:"beside the blue sea",color:"#fff2c6"},
    {id:"school",text:"at school",art:"school",hint:"a place to learn",color:"#dff1ff"},
    {id:"garden",text:"in the garden",art:"garden",hint:"where flowers grow",color:"#f4e4ff"},
    {id:"kitchen",text:"in the kitchen",art:"kitchen",hint:"something smells good",color:"#ffe6e2"},
    {id:"moon",text:"on the moon",art:"moon",hint:"a long way from home",color:"#e8eaff"},
    {id:"forest",text:"in the forest",art:"forest",hint:"among tall trees",color:"#e1f4e7"},
    {id:"farm",text:"on the farm",art:"farm",hint:"a busy country place",color:"#fff2c6"},
    {id:"library",text:"in the library",art:"library",hint:"a world of books",color:"#f4e4ff"},
    {id:"playground",text:"at the playground",art:"playground",hint:"a place to play",color:"#ffe6e2"},
    {id:"zoo",text:"at the zoo",art:"zoo",hint:"animals all around",color:"#fff0d2"},
    {id:"museum",text:"in the museum",art:"museum",hint:"a place to discover",color:"#dff1ff"},
    {id:"river",text:"beside the river",art:"river",hint:"listen to the water",color:"#dff1ff"},
    {id:"castle",text:"in the castle",art:"castle",hint:"a storybook adventure",color:"#e8eaff"},
    {id:"station",text:"at the station",art:"station",hint:"all aboard",color:"#ffe6e2"},
    {id:"treehouse",text:"in the treehouse",art:"treehouse",hint:"up among the branches",color:"#e1f4e7"}
]
pub let times = [
    {id:"morning",text:"in the morning",art:"morning",hint:"hello, new day",color:"#fff2c6"},
    {id:"afternoon",text:"in the afternoon",art:"afternoon",hint:"when the sun is high",color:"#fff0d2"},
    {id:"night",text:"at night",art:"night",hint:"under the stars",color:"#e8eaff"},
    {id:"monday",text:"on Monday",art:"monday",hint:"a brand-new week",color:"#dff1ff"},
    {id:"winter",text:"in winter",art:"winter",hint:"a chilly time of year",color:"#e1f4e7"},
    {id:"lunch",text:"after lunch",art:"lunch",hint:"with a happy tummy",color:"#ffe6e2"},
    {id:"today",text:"today",art:"calendar",hint:"right here, right now",color:"#dff1ff"},
    {id:"tomorrow",text:"tomorrow",art:"calendar",hint:"the very next day",color:"#e8eaff"},
    {id:"evening",text:"in the evening",art:"sunset",hint:"as the day winds down",color:"#ffe6e2"},
    {id:"saturday",text:"on Saturday",art:"calendar",hint:"a weekend adventure",color:"#fff2c6"},
    {id:"summer",text:"in summer",art:"afternoon",hint:"long, sunny days",color:"#fff2c6"},
    {id:"spring",text:"in spring",art:"flowers",hint:"when new flowers bloom",color:"#e1f4e7"},
    {id:"autumn",text:"in autumn",art:"autumn",hint:"when leaves turn gold",color:"#fff0d2"},
    {id:"birthday",text:"on a birthday",art:"birthday",hint:"a day to celebrate",color:"#f4e4ff"},
    {id:"sunset",text:"at sunset",art:"sunset",hint:"as the sun goes down",color:"#ffe6e2"},
    {id:"dinner",text:"before dinner",art:"lunch",hint:"nearly time to eat",color:"#fff0d2"}
]
pub let manners = [
    {id:"carefully",text:"carefully",art:"careful",hint:"with care and attention",color:"#ede6ff"},
    {id:"quickly",text:"quickly",art:"rabbit",hint:"at a speedy pace",color:"#ede6ff"},
    {id:"slowly",text:"slowly",art:"turtle",hint:"taking a little time",color:"#ede6ff"},
    {id:"quietly",text:"quietly",art:"quiet",hint:"without making a noise",color:"#ede6ff"},
    {id:"happily",text:"happily",art:"happy",hint:"with a cheerful smile",color:"#ede6ff"}
]
pub let reasons = [
    {id:"help",text:"to help a friend",art:"help",hint:"a kind purpose",color:"#ffe7ef"},
    {id:"fun",text:"to have fun",art:"ball",hint:"a playful purpose",color:"#ffe7ef"},
    {id:"learn",text:"to learn something new",art:"book",hint:"a curious purpose",color:"#ffe7ef"},
    {id:"sunshine",text:"because it is a sunny day",art:"afternoon",hint:"give a reason",color:"#ffe7ef"},
    {id:"rain",text:"because the rain has stopped",art:"rain",hint:"explain what changed",color:"#ffe7ef"}
]
pub let parts = [
    {id:"subject",question:"Who?",name:"Subject",caption:"Pick a subject",keys:["a","s","d"]},
    {id:"predicate",question:"Doing what?",name:"Predicate",caption:"Pick an action",keys:["j","k","l"]},
    {id:"object",question:"To what?",name:"Object",caption:"Pick an object",keys:["z","x","c"]},
    {id:"where",question:"Where?",name:"Place",caption:"Pick a place",keys:["q","w","e"]},
    {id:"when",question:"When?",name:"Time",caption:"Pick a time",keys:["u","i","o"]},
    {id:"how",question:"How?",name:"Manner",caption:"Pick how",keys:["f","g","h"]},
    {id:"why",question:"Why?",name:"Reason",caption:"Pick a reason",keys:["v","b","n"]}
]
pub let patterns = [
    {id:"pair",title:"Start with two",parts:["subject","predicate"],example:"The hen lays eggs."},
    {id:"triple",title:"Add an object",parts:["subject","predicate","object"],example:"The joker wears a funny cap."},
    {id:"place",title:"Tell us where",parts:["subject","predicate","where"],example:"The cat finds a shiny shell at the beach."},
    {id:"time",title:"Tell us when",parts:["subject","predicate","when"],example:"Grandma bakes blueberry pancakes in the morning."},
    {id:"object-place",title:"Object and place",parts:["subject","predicate","object","where"],example:"The rabbit nibbles a crunchy carrot in the garden."},
    {id:"object-time",title:"Object and time",parts:["subject","predicate","object","when"],example:"The astronaut carries a little flag at night."},
    {id:"place-time",title:"Set the scene",parts:["subject","predicate","where","when"],example:"The dog chases a red ball in the park after lunch."},
    {id:"story",title:"The whole story",parts:["subject","predicate","object","where","when"],example:"A friendly dragon paints a rainbow at school on Monday."},
    {id:"how",title:"Tell us how",parts:["subject","predicate","how"],example:"The musician plays a little drum quietly.",advanced:true},
    {id:"object-how",title:"Action and manner",parts:["subject","predicate","object","how"],example:"The scientist examines a shiny crystal carefully.",advanced:true},
    {id:"why",title:"Give a reason",parts:["subject","predicate","why"],example:"The monkey tosses a ripe banana to have fun.",advanced:true},
    {id:"how-place",title:"How and where",parts:["subject","predicate","object","how","where"],example:"The gardener waters bright flowers slowly in the garden.",advanced:true},
    {id:"time-why",title:"When and why",parts:["subject","predicate","object","when","why"],example:"The chef stirs a pot of soup in the morning to help a friend.",advanced:true},
    {id:"details",title:"Add every detail",parts:["subject","predicate","object","how","where","when","why"],example:"The bear picks sweet berries carefully in the forest today to have fun.",advanced:true}
]
pub let connectors = [
    {id:"and",text:"and",separator:", and "},
    {id:"but",text:"but",separator:", but "},
    {id:"or",text:"or",separator:", or "},
    {id:"so",text:"so",separator:", so "},
    {id:"yet",text:"yet",separator:", yet "},
    {id:"because",text:"because",separator:" because "},
    {id:"although",text:"although",separator:" although "},
    {id:"while",text:"while",separator:" while "},
    {id:"when",text:"when",separator:" when "},
    {id:"if",text:"if",separator:" if "}
]
let catalogs = {subject:subjects,predicate:predicates,object:objects,where:places,when:times,how:manners,why:reasons}
pub let card_count = sum([for (part in parts) len(catalogs[part.id])])
let part_ids = [for (part in parts) part.id]
let empty_choices = reduce([{},*part_ids],(choices,kind)=>{*:choices,[kind]:""})
let round_fields = ["phase","pattern","hands","clauses","link","previous","seed",*part_ids]

fn lookup(cards,id) => [for (card in cards where card.id == id) card][0]
pub fn pattern(id) => lookup(patterns,id)
pub fn part(kind) => lookup(parts,kind)
pub fn kinds(id) => [for (kind in (pattern(id).parts or [])) kind]
pub fn has_object(id) => len([for (kind in kinds(id) where kind == "object") kind]) > 0
pub fn part_name(id,kind) => if (kind == "predicate" and has_object(id)) "Verb" else part(kind).name
fn display_cards(cards,id,kind) {
    // only a separate object slot turns a complete predicate into its verb.
    if (kind == "predicate" and has_object(id)) [for (card in cards) {*:card,text:card.verb}] else cards
}
pub fn catalog(id,kind) => display_cards(catalogs[kind] or [],id,kind)
pub fn deck(st,kind) => display_cards((if (st.decks == null) catalogs else st.decks)[kind] or [],st.pattern,kind)
pub fn card(st,kind) => lookup(deck(st,kind),st[kind])
pub fn ranked(cards,seed) => [for (entry in sort(
    [for (i,card in cards) {card:card,rank:math.random(seed+i)[0]}],(entry)=>entry.rank)) entry.card]
fn offer(cards,required) => if (contains([for (card in cards) card.id],required.id)) cards
    else [cards[0],required,cards[1]]
fn deal(st) {
    let hand_subjects = if (st.decks != null) deck(st,"subject") else slice(ranked(subjects,st.seed),0,3)
    let verbs = deck(st,"predicate")
    let natural = [for (card in verbs where card.match == hand_subjects[0].id) card][0]
    // all patterns offer a familiar subject/action/object match, plus silly mixes.
    let dealt = reduce([{hands:{},seed:st.seed},*kinds(st.pattern)],(acc,kind)=>{
        let cards = deck(st,kind)
        let hand = if (st.decks != null) cards else slice(ranked(cards,acc.seed),0,3)
        let offered = if (st.decks != null) hand else if (kind == "subject") hand_subjects
            else if (kind == "predicate") offer(hand,natural)
            else if (kind == "object") offer(hand,lookup(objects,natural.object)) else hand
        {hands:{*:acc.hands,[kind]:offered},seed:acc.seed+len(cards)}
    })
    clear_choices({*:st,hands:dealt.hands,seed:dealt.seed})
}
pub fn clear_choices(st) => {*:st,*:empty_choices,phase:"build"}
pub fn new_game(seed=42,round_id=1) => {*:empty_choices,round:round_id,seed:seed,book:[],pattern:"",hands:null,
    phase:"pattern",clauses:[],link:"",previous:null,visits:{}}
pub fn jump_round(st,target) => progress.jump(st,target,rounds,round_fields,new_game,"saved")
pub fn round_saved(st,id) => progress.finished(st,id)
pub fn choose_pattern(st,id) => if (st.phase != "pattern" or pattern(id) == null) st
    // reopening the same recipe preserves the draft and its hand.
    else if (st.pattern == id and st.hands != null) {*:st,phase:"build"}
    else deal({*:st,pattern:id})
pub fn show_patterns(st) => if (st.phase == "build") {*:st,phase:"pattern"} else st
pub fn choose(st,kind,id) {
    if (st.phase != "build" or not contains(kinds(st.pattern),kind) or lookup(st.hands[kind],id) == null) st else {*:st,[kind]:id}
}
pub fn current_clause(st) => reduce([{pattern:st.pattern,link:st.link,decks:st.decks},*part_ids],(clause,kind)=>{*:clause,[kind]:st[kind]})
pub fn all_clauses(st) => [*st.clauses,current_clause(st)]
pub fn complete(st) => pattern(st.pattern) != null and all([for (kind in kinds(st.pattern)) card(st,kind) != null])
pub fn part_text(clause,kind) {
    let selected = card(clause,kind)
    if (selected == null) "" else if (kind == "subject" and clause.link != "" and selected.proper != true) lower(selected.text) else selected.text
}
pub fn separator(id) => if (id == "") "" else lookup(connectors,id).separator
pub fn clause_text(clause) => join([for (kind in kinds(clause.pattern)) part_text(clause,kind)]," ")
pub fn sentence(st) => if (not complete(st)) "" else join([for (clause in all_clauses(st)) separator(clause.link) ++ clause_text(clause)],"") ++ "."
pub fn repeated(st) => sentence(st) != "" and contains([for (entry in st.book) entry.text],sentence(st))
pub fn ready(st) => st.phase == "build" and complete(st) and not repeated(st)
fn familiar(clause) => card(clause,"predicate").match == clause.subject and
    (not has_object(clause.pattern) or card(clause,"predicate").object == clause.object)
pub fn save_sentence(st) {
    if (not ready(st)) st else {
        let clauses = all_clauses(st)
        let entry = {text:sentence(st),art:card(clauses[0],"subject").art,clauses:clauses,natural:all([for (clause in clauses) familiar(clause)])}
        {*:st,phase:"saved",previous:null,book:if (st.unlimited == true) [*st.book,entry] else progress.record(st,entry)}
    }
}
pub fn extend(st,connector) => if (st.phase != "build" or not complete(st) or lookup(connectors,connector) == null) st
    // the immutable pre-join snapshot restores both the choices and their hand.
    else deal({*:st,clauses:[*st.clauses,current_clause(st)],link:connector,previous:st})
pub fn next(st) => if (st.phase != "saved") st else if (len(st.book) == rounds) {*:st,phase:"done"}
    else jump_round(st,progress.pending(st,rounds))
pub fn action(st,command,value="") => if (command == "reset") new_game(st.seed)
    else if (command == "primary") (if (st.phase == "saved") next(st) else save_sentence(st))
    else if (command == "pattern") choose_pattern(st,value)
    else if (command == "patterns") show_patterns(st)
    else if (command == "resume" and st.phase == "pattern" and st.pattern != "") {*:st,phase:"build"}
    else if (command == "round" and st.unlimited != true) progress.requested(st,value,rounds,round_fields,new_game,"saved")
    else if (command == "deal" and st.phase == "build" and st.unlimited != true) deal(st)
    else if (command == "extend") extend(st,value)
    // undo restores local choices while keeping work saved in other rounds.
    else if (command == "undo-join") (if (st.phase == "build" and st.previous != null)
        {*:st.previous,book:st.book,visits:st.visits,seed:max(st.seed,st.previous.seed)} else st)
    else choose(st,command,value)
pub fn shortcut(st,key) {
    let options = [for (kind in kinds(st.pattern),i,value in part(kind).keys where value == key) {kind:kind,id:st.hands[kind][i].id}]
    if (len(options) == 0) st else choose(st,options[0].kind,options[0].id)
}
pub fn saved_entry(st) => progress.entry(st,st.round)
pub fn feedback(st) => if (st.phase == "saved")
    (if (len(saved_entry(st).clauses) > 1) "A bigger story! Your conjunctions connect complete clauses."
        else if (saved_entry(st).natural) "A lovely match! Who and what fit together."
        else "A fun mix! Your subject and predicate make a sentence.")
    else if (repeated(st)) "That sentence is already in your book. Try some new cards!" else ""
