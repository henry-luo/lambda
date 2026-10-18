// Immutable game transitions keep the reactive view pure (S1.4, S12.1.3).
pub let rounds = 5
pub let subjects = [
    {id:"hen",text:"The hen",art:"hen",hint:"a feathered friend",color:"#fff0d2"},
    {id:"giraffe",text:"The giraffe",art:"giraffe",hint:"very tall indeed",color:"#fff3bf"},
    {id:"joker",text:"The joker",art:"joker",hint:"ready for a laugh",color:"#f4e4ff"},
    {id:"pirate",text:"A tiny pirate",art:"pirate",hint:"captain of the tub",color:"#dff1ff"},
    {id:"robot",text:"My robot",art:"robot",hint:"beep-boop buddy",color:"#e1f4e7"},
    {id:"grandma",text:"Grandma",art:"grandma",hint:"the best storyteller",color:"#ffe6e2",proper:true}
]
pub let predicates = [
    {id:"eggs",text:"lays eggs",verb:"lays",object:"eggs",art:"eggs",hint:"a fresh little nest",color:"#fff2c6",match:"hen"},
    {id:"leaves",text:"eats the tallest leaves",verb:"eats",object:"leaves",art:"leaves",hint:"stretch, stretch, stretch",color:"#e1f4e7",match:"giraffe"},
    {id:"cap",text:"wears a funny cap",verb:"wears",object:"cap",art:"cap",hint:"a very fancy look",color:"#f4e4ff",match:"joker"},
    {id:"bathtub",text:"sails around the bathtub",verb:"sails",object:"boat",art:"bathtub",hint:"all aboard the bubbles",color:"#dff1ff",match:"pirate"},
    {id:"moonwalk",text:"does a robot dance",verb:"does",object:"dance",art:"moonwalk",hint:"to a beep-boop beat",color:"#e8eaff",match:"robot"},
    {id:"pancakes",text:"bakes blueberry pancakes",verb:"bakes",object:"pancakes",art:"pancakes",hint:"flip, flip, hooray",color:"#ffe6e2",match:"grandma"}
]
pub let objects = [
    {id:"eggs",text:"eggs",art:"eggs",hint:"a little breakfast",color:"#fff2c6"},
    {id:"leaves",text:"the tallest leaves",art:"leaves",hint:"a leafy snack",color:"#e1f4e7"},
    {id:"cap",text:"a funny cap",art:"cap",hint:"something to wear",color:"#f4e4ff"},
    {id:"boat",text:"a tiny boat",art:"bathtub",hint:"a little adventure",color:"#dff1ff"},
    {id:"dance",text:"a robot dance",art:"moonwalk",hint:"some fancy footwork",color:"#e8eaff"},
    {id:"pancakes",text:"blueberry pancakes",art:"pancakes",hint:"a tasty stack",color:"#ffe6e2"}
]
pub let connectors = [
    {id:"and",text:"and",separator:", and "},
    {id:"but",text:"but",separator:", but "},
    {id:"or",text:"or",separator:", or "},
    {id:"so",text:"so",separator:", so "},
    {id:"yet",text:"yet",separator:", yet "},
    {id:"because",text:"because",separator:" because "}
]
pub let shortcuts = {subject:["a","s","d"],predicate:["j","k","l"],object:["z","x","c"]}

fn lookup(cards,id) => [for (card in cards where card.id == id) card][0]
pub fn kinds(mode:int) => [for (i,kind in ["subject","predicate","object"] where i < mode) kind]
pub fn catalog(mode,kind) {
    let cards = {subject:subjects,predicate:predicates,object:objects}[kind] or []
    if (kind == "predicate" and mode == 3) [for (card in cards) {*:card,text:card.verb}] else cards
}
pub fn card(st,kind) => lookup(catalog(st.mode,kind),st[kind])
fn ranked(cards,seed) => [for (entry in sort(
    [for (i,card in cards) {card:card,rank:math.random(seed+i)[0]}],(entry)=>entry.rank)) entry.card]
fn offer(cards,required) => if (contains([for (card in cards) card.id],required.id)) cards
    else [cards[0],required,cards[1]]
fn deal(st) {
    let hand_subjects = slice(ranked(subjects,st.seed),0,3)
    let verbs = catalog(st.mode,"predicate")
    let natural = [for (card in verbs where card.match == hand_subjects[0].id) card][0]
    let hand_predicates = offer(slice(ranked(verbs,st.seed+len(subjects)),0,3),natural)
    // every mode offers a complete familiar match alongside imaginative mixes.
    let hand_objects = if (st.mode == 3) offer(slice(ranked(objects,st.seed+len(subjects)+len(predicates)),0,3),lookup(objects,natural.object)) else []
    {*:st,hands:{subject:hand_subjects,predicate:hand_predicates,object:hand_objects},subject:"",predicate:"",object:"",
        phase:"build",seed:st.seed+len(subjects)+len(predicates)+(if (st.mode == 3) len(objects) else 0)}
}
pub fn new_game(seed=42,mode=2) => deal({round:1,seed:seed,book:[],mode:if (mode == 3) 3 else 2,clauses:[],link:"",previous:null})
pub fn choose(st,kind,id) {
    if (st.phase != "build" or not contains(kinds(st.mode),kind) or lookup(st.hands[kind],id) == null) st else {*:st,[kind]:id}
}
pub fn current_clause(st) => {mode:st.mode,subject:st.subject,predicate:st.predicate,object:st.object,link:st.link}
pub fn all_clauses(st) => [*st.clauses,current_clause(st)]
pub fn complete(st) => all([for (kind in kinds(st.mode)) card(st,kind) != null])
pub fn part_text(clause,kind) {
    let selected = card(clause,kind)
    if (selected == null) "" else if (kind == "subject" and clause.link != "" and selected.proper != true) lower(selected.text) else selected.text
}
pub fn separator(id) => if (id == "") "" else lookup(connectors,id).separator
pub fn clause_text(clause) => join([for (kind in kinds(clause.mode)) part_text(clause,kind)]," ")
pub fn sentence(st) => if (not complete(st)) "" else join([for (clause in all_clauses(st)) separator(clause.link) ++ clause_text(clause)],"") ++ "."
pub fn repeated(st) => sentence(st) != "" and contains([for (entry in st.book) entry.text],sentence(st))
pub fn ready(st) => st.phase == "build" and complete(st) and not repeated(st)
fn familiar(clause) => card(clause,"predicate").match == clause.subject and
    (clause.mode == 2 or card(clause,"predicate").object == clause.object)
pub fn save_sentence(st) {
    let clauses = all_clauses(st)
    if (not ready(st)) st else {*:st,phase:"saved",previous:null,book:[*st.book,
        {text:sentence(st),art:card(clauses[0],"subject").art,clauses:clauses,natural:all([for (clause in clauses) familiar(clause)])}]}
}
pub fn extend(st,connector) => if (st.phase != "build" or not complete(st) or lookup(connectors,connector) == null) st
    // the immutable pre-join snapshot restores both the choices and their hand.
    else deal({*:st,clauses:[*st.clauses,current_clause(st)],link:connector,previous:st})
pub fn change_mode(st,mode) => if (st.phase != "build" or not contains([2,3],mode) or mode == st.mode) st
    else deal({*:st,mode:mode})
pub fn next(st) => if (st.phase != "saved") st else if (len(st.book) == rounds) {*:st,phase:"done"}
    else deal({*:st,round:st.round+1,clauses:[],link:"",previous:null})
pub fn action(st,command,value="") => if (command == "reset") new_game(st.seed,st.mode)
    else if (command == "primary") (if (st.phase == "saved") next(st) else save_sentence(st))
    else if (command == "mode" and contains(["2","3"],value)) change_mode(st,int(value))
    else if (command == "extend") extend(st,value)
    else if (command == "undo-join") (if (st.phase == "build" and st.previous != null) st.previous else st)
    else choose(st,command,value)
pub fn shortcut(st,key) {
    let options = [for (kind in kinds(st.mode),i,value in shortcuts[kind] where value == key) {kind:kind,id:st.hands[kind][i].id}]
    if (len(options) == 0) st else choose(st,options[0].kind,options[0].id)
}
pub fn feedback(st) => if (st.phase == "saved")
    (if (len(st.book[len(st.book)-1].clauses) > 1) "A bigger story! Your conjunctions connect complete clauses."
        else if (st.book[len(st.book)-1].natural) "A lovely match! Who and what fit together."
        else "A fun mix! Your subject and predicate make a sentence.")
    else if (repeated(st)) "That sentence is already in your book. Try a new pair!" else ""
