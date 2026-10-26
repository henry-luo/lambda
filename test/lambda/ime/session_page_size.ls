import ime: lambda.ime.chinese
import k: .mod_session

pn main() {
    let engine = ime.load("zh-Hans",{directory:"test/ime/fixture_horizontal"})^
    let initial = ime.session(engine)^
    let typed = k.typed(engine,initial,"xi")^
    let frame = k.press(engine,typed,"ArrowUp")^
    let next = k.press(engine,typed,"PageDown")^
    let seventh = k.press(engine,typed,"7")^
    let eighth = k.press(engine,typed,"8")^
    let ninth = ime.configure(engine,typed,{page_size:9})^
    let reset = ime.configure(engine,ninth.session,{})^;
    [
        ime.settings(initial)^ == {fuzzy:[],page_size:7},
        len(typed.items) == 8,
        len(frame.actions[0].items) == 7 and frame.actions[0].pages == 2,
        next.session.page == 1 and len(next.actions[0].items) == 1,
        next.actions[0].items[0].text == "溪",
        seventh.actions[0].text == "席",
        eighth.session == typed,
        k.press(engine,next.session,"1")^.actions[0].text == "溪",
        len(ninth.actions[0].items) == 8 and ninth.actions[0].pages == 1,
        k.press(engine,ninth.session,"8")^.actions[0].text == "溪",
        reset.session.settings.page_size == 7 and len(reset.actions[0].items) == 7
    ]
}
