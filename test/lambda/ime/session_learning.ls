import ime: lambda.ime.chinese
import users: lambda.ime.chinese.user
import query: lambda.ime.chinese.query
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine)^
    let typed = k.typed(engine,initial,"beijing")^
    let background = k.press(engine,typed,"2")^
    let promoted = k.typed(engine,background.session,"beijing")^
    let again = k.press(engine,promoted," ")^
    let beijing = k.choose(engine,k.typed(engine,again.session,"beijing")^,"北京")^
    let recent = k.typed(engine,beijing.session,"beijing")^
    let explicit = {format:"lambda.ime.chinese.user",format_version:1,profile:"zh-Hans",next_seq:8,
        entries:[["北经","bei'jing",2,7],["北","bei",20,6]]}
    let supplied = ime.with_user(initial,explicit)^
    let custom = k.typed(engine,supplied,"beijing")^
    let abbr = k.typed(engine,supplied,"bj")^
    let prefix = k.typed(engine,supplied,"beij")^
    let selected = k.press(engine,custom," ")^
    let active_overlay = ime.with_user(typed,explicit)^
    let dirty_selection = k.press(engine,active_overlay," ")^
    let fuzzy_user = ime.with_user(ime.session(engine,{fuzzy:["z=zh"]})^,
        {*:explicit,entries:[["钟国","zhong'guo",1,7]]})^
    let fuzzy = k.typed(engine,fuzzy_user,"zongguo")^
    let tied = ime.with_user(initial,{*:explicit,entries:[["背景","bei'jing",4,7],["北京","bei'jing",2,7]]})^
    let counted = k.typed(engine,tied,"beijing")^
    let clearing = ime.with_user(supplied,users.empty("zh-Hans"))^
    let learned = users.data(selected.session.user)
    let limited = query.candidates(engine,"beijing",{max_postings:1},tied.user)^;
    [
        promoted.items[0].text == "背景",
        again.session.user.entries == [["背景","bei'jing",2,2]] and again.session.user.next_seq == 3,
        recent.items[0].text == "北京",
        custom.items[0].text == "北经" and not ("北经" in f.texts(ime.candidates(engine,"beijing")^)),
        abbr.items[0].text == "北经" and abbr.items[0].match == "abbreviation",
        prefix.items[0].text == "北经" and prefix.items[0].match == "incomplete",
        selected.actions[0].text == "北经",
        dirty_selection.actions[0].text == "北经" and not dirty_selection.session.dirty,
        fuzzy.items[0].text == "钟国" and fuzzy.items[0].match == "fuzzy",
        counted.items[0].text == "背景",
        len(unique(k.texts(promoted))) == len(promoted.items),
        ime.settings(clearing)^ == ime.settings(supplied)^ and clearing.user.entries == [],
        learned.format == explicit.format and learned.next_seq == 9,
        learned.entries == [["北","bei",20,6],["北经","bei'jing",3,8]],
        learned.index == null and learned.settings == null,
        ime.configure(engine,supplied,{page_size:2})^.session.user == supplied.user,
        initial.user.entries == [] and typed.user.entries == [] and supplied.user.next_seq == 8,
        limited.truncated and limited.items[0].text == "背景" and len(limited.items) <= 2
    ]
}
