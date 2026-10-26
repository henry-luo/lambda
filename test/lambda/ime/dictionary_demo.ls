import ime: lambda.ime.chinese
import p: lambda.ime.chinese.pack
import lines: lambda.io.line_index
import learned: lambda.ime.chinese.user
import model: ~~.~~.demo.ime.mod_dictionary

fn has(engine,raw,word) bool => word in [for (item in ime.candidates(engine,raw)^.items) item.text]

pn main() {
    let engine = ime.load("zh-Hans",{directory:"test/ime/fixture"})^
    let empty = model.empty(engine.profile)
    let add = model.change(empty,engine,"恩典"," EN  DIAN ",true)^
    let added = model.restore(engine,add)^
    let add_again = model.change(add,added,"恩典","en'dian",true)^
    let removed = model.change(add,added,"北京","bei jing",false)^
    let without = model.restore(engine,removed)^
    let deleted_added = model.restore(engine,model.change(add,added,"恩典","en dian",false)^)^
    let first = model.browse(engine,"",null,5)^
    let second = model.browse(engine,"",first.next,5)^
    let prefix = model.browse(engine,"bei")^
    let full = model.browse(engine,"bei jing")^
    let restored = model.edited(without,["北京","bei'jing",true])^
    let session = {*:ime.session(engine)^,user:learned.learn(learned.empty(engine.profile),
        [{text:"北京",reading:"bei'jing"}])^}
    let clean = model.forget(session,"北京","bei'jing")^
    let single = {*:engine,lexicon:lines.from_bytes(binary("1:en\t恩 0\n"))^}
    let zero = model.edited(single,["恩","en",false])^
    let revived = model.edited(zero,["恩","en",true])^
    let readded = model.restore(engine,model.change(removed,engine,"北京","bei jing",true)^)^
    let row = p.lower_bound(readded.lexicon,"2:bei'jing")^
    let word = model.browse(engine,"北京")^
    let fragment = model.browse(engine,"京")^
    let word_first = model.browse(engine,"北京",null,1)^
    let word_second = model.browse(engine,"北京",word_first.next,1)^
    let polyphonic = model.browse(engine,"行")^
    let traditional = ime.load("zh-Hant",{directory:"test/ime/fixture_hant"})^
    let repeated = model.edited(engine,["人人","ren'ren",true])^
    let supplementary = model.edited(engine,["𠀀","he",true])^
    let checks = [
        model.reading(" Bei  Jing ") == "bei'jing",
        add.entries == [["恩典","en'dian",true]],
        has(added,"endian","恩典"),has(added,"ed","恩典"),
        len(add_again.entries) == 1,
        not has(without,"beijing","北京"),not has(without,"bj","北京"),
        has(without,"beijing","背景"),has(without,"endian","恩典"),
        not has(deleted_added,"endian","恩典"),not has(deleted_added,"ed","恩典"),
        len(first.items) == 5 and len(second.items) == 5,
        first.items[4] != second.items[0],
        all([for (item in prefix.items) starts_with(item.reading,"bei")]),
        "北京" in [for (item in full.items) item.word],
        len(model.browse(engine,"zzzz")^.items) == 0,
        has(restored,"beijing","北京"),
        len(clean.user.entries) == 0,
        len(zero.lexicon.offsets) == 0 and zero.lexicon.offsets is u32[],
        has(revived,"en","恩"),
        engine.lexicon.bytes == input("test/ime/fixture/lexicon.txt","binary")^,
        (model.change(empty,engine,"English","en",true) ^ { ^.code }) == 7830,
        (model.change(empty,engine,"恩典","en",true) ^ { ^.code }) == 7830,
        (model.change(empty,engine,"恩典","en invalid",true) ^ { ^.code }) == 7830,
        (model.validate({*:empty,profile:"zh-Hant"},engine) ^ { ^.code }) == 7830,
        (model.validate({*:add,entries:[*add.entries,*add.entries]},engine) ^ { ^.code }) == 7830,
        (model.validate({*:empty,entries:[["恩","en","yes"]]},engine) ^ { ^.code }) == 7830,
        (model.browse(engine,"北 jing") ^ { ^.code }) == 7830,
        p.posting(readded.lexicon,row,p.posting_start(readded.lexicon,row),false)^.weight == 4800,
        has(readded,"endian","恩典"),
        [for (item in word.items) item.word] == ["北京","北京大学"],
        [for (item in fragment.items) item.word] == ["京","北京","北京大学"],
        word_first.items[0].word == "北京" and word_second.items[0].word == "北京大学",
        word_first.next.lines == [19,28] and word_second.next == null,
        [for (item in polyphonic.items where item.word == "行") item.reading] == ["hang","xing"],
        model.browse(engine,"  北京  ")^ == word,
        len(model.browse(engine,"不存在")^.items) == 0,
        model.browse(zero,"恩")^.next == null,
        len(model.browse(added,"恩典")^.items) == 1,
        len(model.browse(deleted_added,"恩典")^.items) == 0,
        [for (item in model.browse(without,"北京")^.items) item.word] == ["北京大学"],
        model.browse(traditional,"中國")^.items[0].word == "中國",
        "行业" in [for (item in polyphonic.items) item.word],
        len(model.browse(repeated,"人")^.items) == 1,
        model.browse(supplementary,"𠀀")^.items[0].word == "𠀀"
    ];
    checks
}
