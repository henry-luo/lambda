import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine,{fuzzy:["z=zh"],page_size:2})^
    let learned = k.choose(engine,k.typed(engine,initial,"beijing")^,"背景")^
    let root = "temp/ime_chinese/user-" ++ string(sys.proc.self.pid#)
    io.mkdir(root)^
    let path = root ++ "/user.json"
    ime.save_user(learned.session.user,path)^
    let saved = input(path,"json")^
    let loaded = ime.load_user(path)^
    let restored = ime.with_user(ime.session(engine,ime.settings(initial)^)^,loaded)^
    let composing = k.typed(engine,restored,"beijing")^
    let before = input(path,"binary")^
    var invalid_save = null
    ime.save_user({*:loaded,entries:[["bad","bei",1,1]]},path) ^ { invalid_save = ^ }
    let intact = input(path,"binary")^ == before
    var missing = null
    ime.load_user(root ++ "/absent.json") ^ { missing = ^ }
    output("{",root ++ "/broken.json")^
    var broken = null
    ime.load_user(root ++ "/broken.json") ^ { broken = ^ }
    output({*:saved,format_version:2},root ++ "/invalid.json",{format:"json"})^
    var invalid_load = null
    ime.load_user(root ++ "/invalid.json") ^ { invalid_load = ^ }
    // preferences are persisted independently of the user dictionary (IME13).
    output(ime.settings(initial)^,root ++ "/settings.json",{format:"json"})^
    let preferences = input(root ++ "/settings.json","json")^
    let configured = ime.session(engine,preferences)^
    let checks = [
        saved == {format:"lambda.ime.chinese.user",format_version:1,profile:"zh-Hans",next_seq:2,
            entries:[["背景","bei'jing",1,1]]},
        saved.index == null and saved.settings == null,
        loaded.entries == learned.session.user.entries and loaded.next_seq == 2,
        composing.items[0].text == "背景",
        ime.settings(restored)^ == ime.settings(initial)^,
        invalid_save.code == 7830 and intact,
        missing is error and broken is error and invalid_load.code == 7830,
        ime.settings(configured)^ == ime.settings(initial)^,
        len(input(root)^) == 4]
    io.delete(root)^;
    [*checks,not exists(root)]
}
