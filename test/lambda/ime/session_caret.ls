import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

fn moved(engine,s,key,count) map^ => if (count == 0) s
    else moved(engine,k.press(engine,s,key)^.session,key,count - 1)^
fn positioned(engine,s,position) map^ =>
    moved(engine,moved(engine,s,"ArrowLeft",len(s.raw))^,"ArrowRight",position)^

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine)^
    let raw = k.typed(engine,initial,"bejing")^
    let left = positioned(engine,raw,2)^
    let inserted = k.press(engine,left,"i")^
    let erased = k.press(engine,inserted.session,"Backspace")^
    let forward = k.press(engine,left,"Delete")^
    let start = moved(engine,left,"ArrowLeft",8)^
    let end = moved(engine,start,"ArrowRight",12)^
    let down = k.press(engine,inserted.session,"ArrowDown")^
    let highlighted = k.press(engine,k.press(engine,down.session,"ArrowRight")^.session,"Escape")^
    let across = k.press(engine,highlighted.session,"ArrowRight")^
    let quote_raw = k.typed(engine,initial,"xian")^
    let quote_at = positioned(engine,quote_raw,2)^
    let quote = k.press(engine,quote_at,"'")^
    let before_quote = positioned(engine,quote.session,2)^
    let full = k.typed(engine,initial,"beijingdaxue")^
    let chosen = k.choose(engine,full,"北京")^
    let boundary = moved(engine,chosen.session,"ArrowLeft",5)^
    let at_boundary = k.press(engine,boundary,"ArrowLeft")^
    let emptied = moved(engine,boundary,"Delete",5)^
    let restored = k.press(engine,emptied,"Backspace")^
    let separated = k.typed(engine,initial,"b'eijing")^
    let separator_start = moved(engine,separated,"ArrowLeft",8)^
    let adjacent = positioned(engine,k.typed(engine,initial,"xi'a'n")^,4)^
    let middle = positioned(engine,full,3)^
    let selected = k.choose(engine,middle,"北京")^
    let configured = ime.configure(engine,left,{page_size:2})^
    let pending = ime.with_user(left,initial.user)^
    let committed = k.press(engine,inserted.session,"Enter")^;
    [
        initial.caret == 0 and raw.caret == 6,
        left.raw == "bejing" and left.caret == 2,
        inserted.session.raw == "beijing" and inserted.session.caret == 3,
        inserted.actions[0].text == "beijing" and inserted.actions[0].caret == 3,
        inserted.session.items[0].text == "北京",
        erased.session.raw == "bejing" and erased.session.caret == 2,
        forward.session.raw == "being" and forward.session.caret == 2,
        start.caret == 0 and end.caret == 6,
        k.press(engine,start,"Backspace")^.session == start,
        k.press(engine,end,"Delete")^.session == end,
        k.press(engine,start,"'")^.session == start,
        k.press(engine,start,"x")^.session.raw == "xbejing",
        k.press(engine,start,"Delete")^.session.raw == "ejing",
        down.session.expanded and across.session.highlight == 1,
        across.session.page == down.session.page and across.session.items == down.session.items,
        across.session.caret == 4 and across.actions[0].caret == 4,
        k.types(across.actions) == ["compose"],
        k.press(engine,across.session,"ArrowRight")^.session.caret == 5,
        quote.session.raw == "xi'an" and quote.session.caret == 3,
        k.press(engine,quote.session,"'")^.session == quote.session,
        k.press(engine,before_quote,"'")^.session == before_quote,
        chosen.session.raw == "daxue" and chosen.session.caret == 5,
        boundary.caret == 0 and at_boundary.actions[0].caret == 2,
        at_boundary.actions[0].text == "北京daxue",
        k.press(engine,boundary,"Backspace")^.session == boundary,
        restored.session.raw == "beijing" and restored.session.caret == 7,
        restored.session.chosen == [] and restored.actions[0].caret == 7,
        restored.session.user.entries == [],
        k.press(engine,separator_start,"Delete")^.session.raw == "eijing",
        k.press(engine,adjacent,"Backspace")^.session.raw == "xi'n",
        selected.session.raw == "daxue" and selected.session.caret == 0,
        selected.actions[0].text == "北京daxue" and selected.actions[0].caret == 2,
        configured.session.caret == 2,
        k.press(engine,pending,"ArrowRight")^.session.dirty,
        committed.actions[0].text == "beijing" and committed.session.caret == 0,
        k.types(k.press(engine,initial,"ArrowLeft")^.actions) == ["pass"]
    ]
}
