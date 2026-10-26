import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine)^
    let b = k.press(engine,initial,"b")^
    let word = k.typed(engine,initial,"beijing")^
    let committed = k.press(engine,word," ")^
    let canceled = k.press(engine,word,"Escape")^
    let raw = k.press(engine,word,"Enter")^
    let empty = k.press(engine,b.session,"Backspace")^
    let digit = k.press(engine,word,"2")^
    let punct = k.press(engine,word,",")^
    let unknown = k.typed(engine,initial,"qq")^
    let no_candidate = k.press(engine,unknown," ")^;
    [
        initial.mode == "chinese" and initial.raw == "" and initial.items == [],
        ime.settings(initial)^ == {fuzzy:[],page_size:7},
        k.types(b.actions) == ["compose","candidates"],
        b.actions[0].text == "b" and b.actions[0].caret == 1,
        word.items[0].text == "北京",
        k.types(committed.actions) == ["commit","candidates"],
        committed.actions[0].text == "北京",
        committed.session.raw == "" and committed.session.chosen == [],
        committed.actions[1].items == [] and committed.actions[1].pages == 0,
        committed.session.user.entries == [["北京","bei'jing",1,1]],
        k.types(canceled.actions) == ["cancel","candidates"],
        canceled.session.user.entries == [],
        raw.actions[0].text == "beijing" and raw.session.user.entries == [],
        empty.actions[0].type == "cancel" and empty.session.raw == "",
        digit.actions[0].text == "背景",
        punct.actions[0].text == "beijing，",
        unknown.items == [] and no_candidate.session.raw == "qq",
        no_candidate.actions[0].type == "candidates",
        k.types(k.press(engine,initial,"9")^.actions) == ["pass"],
        k.types(k.press(engine,initial," ")^.actions) == ["pass"],
        k.types(k.press(engine,initial,"Backspace")^.actions) == ["pass"],
        k.types(k.press(engine,word,"b","keyup")^.actions) == ["pass"],
        initial.raw == "" and initial.user.entries == [] and word.raw == "beijing"
    ]
}
