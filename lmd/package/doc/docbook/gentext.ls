// A finite data interpreter ports locale list templates without evaluating source strings (S1.8).
import c: .common
import locale: .locale

pub fn token(key) => c.get(locale.TOKENS,key,key)
fn repeated(pattern,items,cursor,limit) map^ {
    if (cursor >= limit) {nodes:[],next:cursor} else {
        let current = sequence(pattern,0,items,cursor)^;
        if (current.next <= cursor) raise c.fail("invalid-source","Locale repeat did not consume an item")
        else {
            let rest = repeated(pattern,items,current.next,limit)^;
            {nodes:[*current.nodes,*rest.nodes],next:rest.next}
        }
    }
}
fn sequence(pattern,index,items,cursor) map^ {
    if (index >= len(pattern)) {nodes:[],next:cursor} else {
        let instruction = pattern[index]
        let reserve = len([for (entry in drop(pattern,index+1) where entry.kind == "content") entry])
        let current = if (instruction.kind == "text") {nodes:[instruction.text],next:cursor}
            else if (instruction.kind == "token") {nodes:[token(instruction.key)],next:cursor}
            else if (instruction.kind == "content") {nodes:[items[cursor]],next:cursor+1}
            else repeated(instruction.nodes,items,cursor,len(items)-reserve)^
        let rest = sequence(pattern,index+1,items,current.next)^;
        {nodes:[*current.nodes,*rest.nodes],next:rest.next}
    }
}
pub fn list_items(key,items) array^ {
    let variants = locale.LISTS[key];
    if (len(items) == 0) [] else if (variants == null)
        raise c.fail("invalid-source","Unknown localization list " ++ key)
    else sequence(variants[min(len(items),len(variants))-1],0,items,0)^.nodes
}
