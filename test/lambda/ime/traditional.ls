import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = ime.load("zh-Hant",{directory:"test/ime/fixture_hant"})^
    let initial = ime.session(engine)^
    let typed = k.typed(engine,initial,"zhongguo")^
    let committed = k.press(engine,typed," ")^
    let hair = ime.candidates(engine,"toufa")^
    let fire = ime.candidates(engine,"fazhan")^
    let forms = ime.candidates(engine,"fa")^
    let abbreviation = ime.candidates(engine,"zhg")^
    let partial = ime.candidates(engine,"ganxib")^
    var mismatch = 0
    ime.load("zh-Hans",{directory:"test/ime/fixture_hant"}) ^ { mismatch = ^.code };
    [
        engine.profile == "zh-Hant",
        mismatch == 7830,
        initial.user.profile == "zh-Hant",
        typed.items[0].text == "中國",
        committed.actions[0].text == "中國",
        committed.session.user.entries == [["中國","zhong'guo",1,1]],
        hair.items[0].text == "頭髮" and hair.items[0].reading == "tou'fa",
        fire.items[0].text == "發展",
        "發" in f.texts(forms) and "髮" in f.texts(forms),
        "中國" in f.texts(abbreviation),
        "幹細胞" in f.texts(partial),
        (ime.with_user(initial,{*:initial.user,profile:"zh-Hans"}) ^ { ^.code }) == 7830
    ]
}
