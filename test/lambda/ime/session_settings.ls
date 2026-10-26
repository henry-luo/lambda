import ime: lambda.ime.chinese
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine)^
    let typed = k.typed(engine,initial,"zongguo")^
    let configured = ime.configure(engine,typed,{fuzzy:["zh=z"],page_size:2})^
    let disabled = ime.configure(engine,configured.session,{fuzzy:"none"})^
    let chosen = k.choose(engine,k.typed(engine,initial,"beijingdaxue")^,"北京")^
    let keeping = ime.configure(engine,chosen.session,{fuzzy:"all",page_size:3})^
    let all_pairs = ime.settings(keeping.session)^
    let changed_idle = ime.configure(engine,initial,{page_size:4})^;
    [
        not ("中国" in k.texts(typed)),
        "中国" in k.texts(configured.session),
        configured.session.raw == typed.raw,
        k.types(configured.actions) == ["candidates"],
        ime.settings(configured.session)^ == {fuzzy:["z=zh"],page_size:2},
        not ("中国" in k.texts(disabled.session)) and disabled.session.raw == typed.raw,
        keeping.session.raw == "daxue" and keeping.session.chosen == chosen.session.chosen,
        len(all_pairs.fuzzy) == 11 and all_pairs.page_size == 3,
        keeping.session.user == chosen.session.user,
        changed_idle.session.raw == "" and changed_idle.actions[0].items == [],
        (ime.session(engine,{unknown:true}) ^ { ^.code }) == 7830,
        (ime.session(engine,{page_size:0}) ^ { ^.code }) == 7830,
        (ime.session(engine,{page_size:10}) ^ { ^.code }) == 7830,
        (ime.configure(engine,typed,{fuzzy:["unknown"]}) ^ { ^.code }) == 7830,
        (ime.configure(engine,typed,{max_paths:1}) ^ { ^.code }) == 7830,
        (ime.key(engine,initial,{type:"keydown",key:1}) ^ { ^.code }) == 7830,
        (ime.key({*:engine,profile:"other"},initial,{key:"a"}) ^ { ^.code }) == 7830,
        (ime.settings({}) ^ { ^.code }) == 7830,
        typed.raw == "zongguo" and ime.settings(initial)^ == {fuzzy:[],page_size:7}
    ]
}
