import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = ime.load("zh-Hans",{directory:"test/ime/fixture_menu"})^
    let initial = ime.session(engine)^
    let raw = k.typed(engine,initial,"xi")^
    let opened = k.press(engine,raw,"ArrowDown")^
    let right = k.press(engine,opened.session,"ArrowRight")^
    let below = k.press(engine,right.session,"ArrowDown")^
    let above = k.press(engine,below.session,"ArrowUp")^
    let left = k.press(engine,above.session,"ArrowLeft")^
    let narrow = k.press(engine,opened.session,"ArrowDown","keydown",{candidate_columns:3})^
    let top = k.press(engine,opened.session,"ArrowUp")^
    let top_right = k.press(engine,right.session,"ArrowUp")^
    let narrow_up = k.press(engine,narrow.session,"ArrowUp","keydown",{candidate_columns:3})^
    let narrow_edge = k.press(engine,k.press(engine,right.session,"ArrowRight")^.session,
        "ArrowUp","keydown",{candidate_columns:3})^
    let reopened = k.press(engine,top_right.session,"ArrowDown")^
    let selected = k.press(engine,top_right.session," ")^
    let collapsed = k.press(engine,below.session,"Escape")^
    let committed = k.press(engine,below.session,"Enter")^
    let picked = k.press(engine,opened.session,"Candidate","keydown",{candidate_index:49})^
    let typed = k.press(engine,opened.session,"a")^
    let core = f.engine()^
    let words = k.typed(core,ime.session(core)^,"aihaohaoxuexi")^
    let first = k.press(core,words,"ArrowLeft")^
    let second = k.press(core,first.session,"ArrowLeft")^
    let letter = k.press(core,second.session,"ArrowRight")^
    let explicit = k.typed(core,ime.session(core)^,"xi'an")^
    let boundary = k.press(core,explicit,"ArrowLeft")^
    let partial = k.typed(core,ime.session(core)^,"beij")^;
    [
        raw.stops == [0,2] and not raw.expanded,
        k.press(engine,raw,"ArrowLeft")^.session.caret == 0,
        opened.session.expanded and len(opened.actions[0].items) == 64,
        opened.actions[0].expanded and opened.actions[0].columns == 7,
        opened.actions[0].highlight == 0 and opened.session.caret == 2,
        right.actions[0].highlight == 1 and right.session.caret == 2,
        below.actions[0].highlight == 8 and below.session.page == 1 and below.session.highlight == 1,
        above.session.expanded and above.actions[0].highlight == 1 and left.actions[0].highlight == 0,
        narrow.actions[0].highlight == 3 and narrow.session.caret == 2,
        not collapsed.session.expanded and len(collapsed.actions[0].items) == 7,
        collapsed.session.page == 1 and collapsed.session.highlight == 1,
        k.press(engine,collapsed.session,"ArrowLeft")^.session.caret == 0,
        committed.actions[0].text == "万" and not committed.session.expanded,
        picked.actions[0].text == "丰" and picked.session.caret == 0,
        not typed.session.expanded and typed.session.raw == "xia" and typed.session.caret == 3,
        k.press(engine,opened.session,"Candidate","keydown",{candidate_index:64})^.session == opened.session,
        k.press(engine,collapsed.session,"Escape")^.actions[0].type == "cancel",
        k.press(engine,k.typed(engine,initial,"qq")^,"ArrowDown")^.session.expanded == false,
        words.stops == [0,2,5,8,11,13],
        first.session.caret == 11 and second.session.caret == 8,
        letter.session.caret == 9 and k.press(core,letter.session,"ArrowLeft")^.session.caret == 8,
        explicit.stops == [0,3,5] and boundary.session.caret == 3,
        k.press(core,boundary.session,"ArrowLeft")^.session.caret == 0,
        k.press(core,partial,"ArrowLeft")^.session.caret == 3,
        not top.session.expanded and len(top.actions[0].items) == 7 and top.actions[0].highlight == 0,
        not top_right.session.expanded and top_right.session.page == 0 and top_right.session.highlight == 1,
        top_right.session.raw == "xi" and top_right.session.caret == 2 and k.types(top_right.actions) == ["candidates"],
        narrow_up.session.expanded and narrow_up.actions[0].highlight == 0,
        not narrow_edge.session.expanded and narrow_edge.actions[0].highlight == 2,
        reopened.session.expanded and reopened.actions[0].highlight == 1 and len(reopened.actions[0].items) == 64,
        selected.actions[0].text == "一" and k.press(engine,top_right.session,"ArrowUp")^.session == top_right.session
    ]
}
