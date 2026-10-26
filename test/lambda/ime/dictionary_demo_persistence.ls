import ime: lambda.ime.chinese
import model: lambda.ime.chinese.dictionary

pn main() {
    let engine = ime.load("zh-Hans",{directory:"test/ime/fixture"})^
    let root = "temp/ime_chinese/dictionary-" ++ string(sys.proc.self.pid#)
    let path = root ++ "/custom.json"
    let original = input("test/ime/fixture/lexicon.txt","binary")^
    io.mkdir(root)^
    let added = model.change(model.empty(engine.profile),engine,"恩典","en dian",true)^
    let edits = model.change(added,engine,"北京","bei jing",false)^
    model.save_edits(edits,path,engine)^
    let checked = model.load_edits(path,engine)^
    let restored = model.restore(engine,checked)^
    let full = ime.candidates(restored,"beijing")^
    let short = ime.candidates(restored,"bj")^
    let custom = ime.candidates(restored,"endian")^
    let before = input(path,"binary")^
    var invalid = null
    model.validate({*:edits,format_version:2},engine) ^ { invalid = ^ }
    var wrong_profile = null
    model.validate({*:edits,profile:"zh-Hant"},engine) ^ { wrong_profile = ^ }
    var write_failure = null
    model.save_edits(edits,path ++ "/child.json",engine) ^ { write_failure = ^ }
    let checks = [checked == edits,
        not ("北京" in [for (item in full.items) item.text]),
        not ("北京" in [for (item in short.items) item.text]),
        custom.items[0].text == "恩典",
        invalid.code == 7830 and wrong_profile.code == 7830,
        write_failure is error,
        input(path,"binary")^ == before,
        input("test/ime/fixture/lexicon.txt","binary")^ == original];
    io.delete(root)^;
    [*checks,not exists(root)]
}
