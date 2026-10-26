import ime: lambda.ime.chinese
import f: .mod_fixture

pn main() {
    let engine = ime.load("zh-Hans",{directory:"test/ime/fixture_fuzzy"})^
    let fuzzy = ime.candidates(engine,"sifei",{fuzzy:"all"})^
    let exact = ime.candidates(engine,"shifei")^
    let initials = ime.candidates(engine,"sif",{fuzzy:["s=sh"]})^
    let limited = ime.candidates(engine,"sifei",{fuzzy:"all",max_candidates:1})^
    let no_floor = ime.candidates(engine,"sifei",{fuzzy:"all",coverage_floor:0})^
    let separated = ime.candidates(engine,"si'fei",{fuzzy:"all"})^;
    [
        fuzzy.items[0].text == "是非" and fuzzy.items[0].weight == 1,
        f.item(fuzzy,"是非").consumed == 5 and f.item(fuzzy,"是非").match == "fuzzy",
        index_of(f.texts(fuzzy),"是非") < index_of(f.texts(fuzzy),"是否"),
        index_of(f.texts(fuzzy),"是非") < index_of(f.texts(fuzzy),"时候"),
        f.item(fuzzy,"是否").consumed == 3 and f.item(fuzzy,"是否").match == "abbreviation",
        exact.items[0].text == "是非" and exact.items[0].match == "exact",
        initials.items[0].text == "是否" and initials.items[0].consumed == 3,
        len(limited.items) == 1 and limited.items[0].text == "是非" and limited.truncated,
        no_floor.items[0].text == "是非",
        separated.items[0].text == "是非" and separated.items[0].consumed == 6,
        not ("是非" in f.texts(ime.candidates(engine,"sifei")^)),
        len(unique(f.texts(fuzzy))) == len(fuzzy.items)
    ]
}
