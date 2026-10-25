import ime: lambda.ime.chinese

pub fn press(engine,s,key,kind = "keydown",mods = null) map^ =>
    ime.key(engine,s,{*:(if (mods == null) {} else mods),type:kind,key:key})^

pub fn typed(engine,s,text,at = 0) map^ =>
    if (at == len(text)) s else typed(engine,press(engine,s,text[at])^.session,text,at + 1)^

pub fn types(actions) array => [for (action in actions) action.type]
pub fn texts(s) array => [for (item in s.items) item.text]
pub fn index(s,text) => index_of(texts(s),text)
pub fn tap(engine,s) map^ => press(engine,press(engine,s,"Shift")^.session,"Shift","keyup")^
pub fn choose(engine,s,text) map^ {
    let configured = ime.configure(engine,s,{fuzzy:s.settings.fuzzy,page_size:9})^
    let target = index(configured.session,text)
    let paged = page(engine,configured.session,target div 9);
    press(engine,paged,string(target % 9 + 1))^
}
fn page(engine,s,count) => if (count == 0) s
    else page(engine,press(engine,s,"PageDown")^.session,count - 1)^
