// S7.7.2v2/S11.4.1v5: final expression contracts prove admission, not storage recipes.
type State = {text:string, at:int, mode:"new" | "mid", optional?:bool}
type Step = {state:State?, done:bool}
fn initial(s:string) State => {text:s,at:0,mode:"new"}
fn advance(s:State) State {
    var result = s;
    result.at = s.at + 1;
    return result
}
fn wrapped(s:State) Step => {state:advance(s),done:false}
fn checked(s:State) Step => wrapped(s)
fn changed(s:State) State => {*:s,mode:"mid",at:s.at+1}
fn selected(s:State, finished:bool) Step =>
    if (finished) {state:null,done:true} else {state:changed(s),done:false}
fn selected_checked(s:State, finished:bool) Step => selected(s,finished)
fn local(s:State) State {
    let updated: State = {*:s,at:s.at+2};
    return updated
}
fn local_checked(s:State) State => local(s)
type Value = {value:int}
fn dynamic(source:map) Value => {*:source}
fn overwritten(source:map) Value => {*:source,value:4}
fn optional_override(source:{value?:int}) Value => {value:"bad",*:source}
fn computed(key:string, value:any) Value => {[key]:value}
type AnyField = {value:any}
fn any_field() AnyField => {value:{items:[1]}}
fn checked_any_field() AnyField => any_field()
fn error_field() AnyField => {value:error("contained")}
fn checked_error_field() AnyField => error_field()
fn original(s:State) State => s;
let s=initial("x");
[checked(s).state.at == 1, checked(s).state.text == "x", original(s).at == 0,
 changed(s).mode == "mid", local_checked(s).at == 2,
 selected_checked(s,false).state.at == 1, selected_checked(s,true).state is null];
[(dynamic({value:"bad"}) ^ { "rejected" }) == "rejected",
 (dynamic({value:3}) ^ { {value:0} }).value == 3,
 overwritten({value:"bad"}).value == 4,
 (optional_override({}) ^ { "rejected" }) == "rejected",
 (optional_override({value:5}) ^ { {value:0} }).value == 5,
 (computed("value","bad") ^ { "rejected" }) == "rejected",
 (computed("value",6) ^ { {value:0} }).value == 6,
 (computed("other",6) ^ { "rejected" }) == "rejected"];
[checked_any_field().value.items == [1],
 (checked_error_field().value ^ { ^.message }) == "contained"]
