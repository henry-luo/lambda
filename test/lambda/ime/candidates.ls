import ime: lambda.ime.chinese
import f: .mod_fixture

pn main() {
    let engine = f.engine()^
    let full = ime.candidates(engine,"beijingdaxue")^
    let mixed = ime.candidates(engine,"beijdx")^
    let incomplete = ime.candidates(engine,"beij")^
    let ambiguous = ime.candidates(engine,"xian")^
    let separated = ime.candidates(engine,"xi'an")^
    let fangan = ime.candidates(engine,"fangan")^
    let abbr = ime.candidates(engine,"bj")^
    let fuzzy = ime.candidates(engine,"zongguo",{fuzzy:["z=zh"]})^;
    [
        full.items[0].text == "北京大学",
        f.item(full,"北京大学").consumed == 12,
        f.item(full,"北京大学").coverage == 4,
        f.item(full,"北京").consumed == 7,
        f.item(full,"北京").match == "exact",
        "北京大学" in f.texts(mixed),
        f.item(mixed,"北京大学").consumed == 6,
        f.item(mixed,"北京大学").match == "abbreviation",
        f.item(incomplete,"北京").match == "incomplete",
        "先" in f.texts(ambiguous) and "西安" in f.texts(ambiguous),
        "西安" in f.texts(separated) and not ("先" in f.texts(separated)),
        "方案" in f.texts(fangan) and "反感" in f.texts(fangan),
        "北京" in f.texts(abbr),
        f.item(abbr,"北京").match == "abbreviation",
        "中国" in f.texts(fuzzy),
        f.item(fuzzy,"中国").match == "fuzzy",
        not ("中国" in f.texts(ime.candidates(engine,"zongguo")^)),
        not ("中国" in f.texts(ime.candidates(engine,"zg")^)),
        "中国" in f.texts(ime.candidates(engine,"zg",{fuzzy:["z=zh"]})^),
        "中国" in f.texts(ime.candidates(engine,"zhg")^),
        "绿色" in f.texts(ime.candidates(engine,"lvse")^),
        "绿色" in f.texts(ime.candidates(engine,"lüse")^),
        "女儿" in f.texts(ime.candidates(engine,"nver")^),
        "北" in f.texts(ime.candidates(engine,"b")^),
        len(unique(f.texts(full))) == len(full.items),
        ime.candidates(engine,"")^.items == [],
        ime.candidates(engine,"qq")^.items == [],
        f.item(ime.candidates(engine,"bei'jing'")^,"北京").consumed == 9
    ]
}
