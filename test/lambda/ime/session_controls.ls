import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine,{page_size:2})^
    let word = k.typed(engine,initial,"zhang")^
    let opened = k.press(engine,word,"ArrowDown")^
    let down = k.press(engine,opened.session,"ArrowRight")^
    let across = k.press(engine,down.session,"ArrowRight")^
    let back = k.press(engine,across.session,"ArrowLeft")^
    let next_page = k.press(engine,word,"=")^
    let previous = k.press(engine,next_page.session,"-")^
    let out_of_page = k.press(engine,word,"9")^
    let selected = k.press(engine,down.session," ")^
    let control = k.press(engine,word,"a","keydown",{ctrlKey:true})^
    let alt = k.press(engine,word,"a","keydown",{altKey:true})^
    let meta = k.press(engine,word,".","keydown",{metaKey:true})^
    let english = k.tap(engine,initial)^
    let latin = k.press(engine,english.session,"a")^
    let chinese = k.tap(engine,latin.session)^
    let held = k.press(engine,initial,"Shift")^
    let uppercase = k.press(engine,held.session,"A","keydown",{shiftKey:true})^
    let released = k.press(engine,uppercase.session,"Shift","keyup")^
    let repeated_shift = k.press(engine,uppercase.session,"Shift")^
    let no_toggle = k.press(engine,repeated_shift.session,"Shift","keyup")^
    let modifier_shift = k.press(engine,held.session,"Shift","keyup",{ctrlKey:true})^
    let a = k.press(engine,initial,"\"")^
    let b = k.press(engine,a.session,"\"")^
    let c = k.press(engine,initial,"'")^
    let d = k.press(engine,c.session,"'")^
    let maximum = k.typed(engine,initial,join([for (i in 1 to 32) "a"],""))^
    let bounded = k.press(engine,maximum,"a")^;
    [
        word.page == 0 and word.highlight == 0,
        down.session.page == 0 and down.session.highlight == 1,
        across.session.page == 1 and across.session.highlight == 0,
        back.session.page == 0 and back.session.highlight == 1,
        len(across.actions) == 1 and across.actions[0].type == "candidates",
        next_page.session.page == 1 and previous.session.page == 0,
        out_of_page.session == word,
        selected.actions[0].text == word.items[1].text ++ slice(word.raw,word.items[1].consumed),
        control.session.raw == word.raw and control.session.items == word.items,
        k.types(control.actions) == ["pass"] and k.types(alt.actions) == ["pass"] and k.types(meta.actions) == ["pass"],
        english.session.mode == "english" and latin.session.raw == "",
        k.types(latin.actions) == ["pass"] and chinese.session.mode == "chinese",
        uppercase.session.raw == "" and released.session.mode == "chinese",
        no_toggle.session.mode == "chinese" and modifier_shift.session.mode == "chinese",
        k.press(engine,initial,"Shift","keyup")^.session.mode == "chinese",
        a.actions[0].text == "“" and b.actions[0].text == "”",
        c.actions[0].text == "‘" and d.actions[0].text == "’",
        k.press(engine,initial,"?")^.actions[0].text == "？",
        k.press(engine,initial,"\\")^.actions[0].text == "、",
        k.press(engine,initial,"(")^.actions[0].text == "（",
        bounded.session.raw == maximum.raw and len(bounded.session.raw) == 32,
        k.types(k.press(engine,initial,"a","keypress")^.actions) == ["pass"]
    ]
}
