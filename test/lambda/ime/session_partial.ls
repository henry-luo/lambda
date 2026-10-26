import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine)^
    let raw = k.typed(engine,initial,"beijingdaxue")^
    let chosen = k.choose(engine,raw,"北京")^
    let deleting = k.typed(engine,chosen.session,"")^
    let a = k.press(engine,deleting,"Backspace")^
    let b = k.press(engine,a.session,"Backspace")^
    let c = k.press(engine,b.session,"Backspace")^
    let d = k.press(engine,c.session,"Backspace")^
    let e = k.press(engine,d.session,"Backspace")^
    let undo = k.press(engine,e.session,"Backspace")^
    let finished = k.press(engine,chosen.session,"Enter")^
    let escaped = k.press(engine,chosen.session,"Escape")^
    let punct = k.press(engine,chosen.session,".")^
    let shifted = k.tap(engine,chosen.session)^
    let quote_raw = k.typed(engine,initial,"bei'jing'qq")^
    let quote_chosen = k.choose(engine,quote_raw,"北京")^
    let mixed = k.typed(engine,initial,"beijdx")^
    let mixed_choice = k.choose(engine,mixed,"北京大学")^
    // the second word comes from the overlay, exercising a two-choice commit.
    let supplied = ime.with_user(initial,{format:"lambda.ime.chinese.user",format_version:1,
        profile:"zh-Hans",next_seq:2,entries:[["大学","da'xue",1,1]]})^
    let first = k.choose(engine,k.typed(engine,supplied,"beijingdaxue")^,"北京")^
    let second = k.choose(engine,first.session,"大学")^;
    [
        chosen.session.raw == "daxue" and chosen.session.chosen[0].raw == "beijing",
        chosen.actions[0].text == "北京daxue" and chosen.actions[0].caret == 7,
        chosen.session.user.entries == [],
        e.session.raw == "" and len(e.session.chosen) == 1,
        e.actions[0].text == "北京" and e.actions[0].caret == 2,
        undo.session.raw == "beijing" and undo.session.chosen == [],
        undo.actions[0].text == "beijing" and undo.session.items[0].text == "北京",
        finished.actions[0].text == "北京daxue",
        finished.session.user.entries == [["北京","bei'jing",1,1]],
        escaped.actions[0].type == "cancel" and escaped.session.user.entries == [],
        punct.actions[0].text == "北京daxue。",
        shifted.actions[0].text == "北京daxue" and shifted.session.mode == "english",
        quote_chosen.session.raw == "qq" and quote_chosen.session.chosen[0].raw == "bei'jing'",
        mixed_choice.actions[0].text == "北京大学" and mixed_choice.session.raw == "",
        k.press(engine,k.typed(engine,initial,"b")^,"'")^.session.raw == "b'",
        k.press(engine,k.typed(engine,initial,"b'")^,"'")^.session.raw == "b'",
        first.session.user.entries == supplied.user.entries and second.actions[0].text == "北京大学",
        second.session.raw == "" and second.session.chosen == [] and
            second.session.user.entries == [["北京","bei'jing",1,2],["大学","da'xue",2,3]] and
            second.session.user.next_seq == 4
    ]
}
