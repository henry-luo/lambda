import model: .mod_model
import puzzle: .mod_puzzle
import picture: .mod_picture

pub let levels = [
    {id:1,title:"Build a sentence",tag:"Choose & create",art:"book",copy:"Pick a pattern. Mix picture cards to make your own sentences.",detail:string(len(model.patterns)) ++ " patterns · " ++ string(model.card_count) ++ " cards"},
    {id:2,title:"Unscramble a sentence",tag:"Think & rearrange",art:"robot",copy:"The words are all mixed up! Put every card back in the right order.",detail:"8 word puzzles · clues & hints"},
    {id:3,title:"Describe a picture",tag:"Look & tell stories",art:"rainbow",copy:"Look closely at a picture. Use its cards again and again to tell what you see.",detail:"3 pictures · reusable cards"}
]
let engines = [
    {create:model.new_game,action:model.action,shortcut:model.shortcut},
    {create:puzzle.new_game,action:puzzle.action,shortcut:puzzle.shortcut},
    {create:picture.new_game,action:picture.action,shortcut:picture.shortcut}
]
fn engine(id) => engines[id-1]
pub fn new_game(seed=42) => {level:0,seed:seed,sessions:{},play:null}
fn begin_level(st,id) {
    let old = st.sessions[string(id)]
    let play = if (old != null) old else engine(id).create(st.seed)
    {*:st,level:id,play:play,sessions:{*:st.sessions,[string(id)]:play}}
}
fn keep(st,play) => {*:st,play:play,seed:play.seed,sessions:{*:st.sessions,[string(st.level)]:play}}
pub fn action(st,command,value="") => if (command == "levels") {*:st,level:0}
    else if (command == "level" and contains(["1","2","3"],value)) begin_level(st,int(value))
    else if (st.level == 0) st
    else keep(st,engine(st.level).action(st.play,command,value))
pub fn shortcut(st,key) => if (st.level == 0 or st.play.phase != "build") st
    else keep(st,engine(st.level).shortcut(st.play,key))
