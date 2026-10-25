import ime: lambda.ime.chinese
import rank: lambda.ime.chinese.rank
import f: .mod_fixture

pn main() {
    let engine = f.engine()^
    let full = ime.candidates(engine,"beijingdaxue",{coverage_floor:2000,group_leaders:1})^
    let all_off = ime.candidates(engine,"zhang")^
    let all_on = ime.candidates(engine,"zhang",{fuzzy:"all"})^
    let path_bound = ime.candidates(engine,"beijingdaxue",{max_paths:1})^
    let key_bound = ime.candidates(engine,"b",{max_keys:1})^
    let post_bound = ime.candidates(engine,"bei",{max_postings:1})^
    let candidate_bound = ime.candidates(engine,"bei",{max_candidates:1})^
    let synthetic = [
        {text:"rare",coverage:4,match:"exact",weight:1,line:1,posting:0},
        {text:"common",coverage:3,match:"exact",weight:4000,line:2,posting:0},
        {text:"next",coverage:3,match:"exact",weight:3000,line:2,posting:1},
        {text:"fuzzy",coverage:3,match:"fuzzy",weight:5000,line:3,posting:0},
        {text:"same",coverage:2,match:"fuzzy",weight:5000,line:4,posting:0},
        {text:"same",coverage:2,match:"exact",weight:3000,line:5,posting:0}
    ]
    let ordered = rank.ranked(synthetic,2000,1)
    let ties = rank.ranked([
        {text:"second",coverage:2,match:"exact",weight:100,line:2,posting:0},
        {text:"first",coverage:2,match:"exact",weight:100,line:1,posting:0}
    ],0,1);
    [
        full.items[0].text == "北京大学",
        f.texts(full)[1] == "北京",
        index_of(f.texts(full),"北境大雪") > index_of(f.texts(full),"北京"),
        ordered[0].text == "common" and ordered[1].text == "rare" and ordered[2].text == "next",
        ordered[3].text == "fuzzy",
        len(ordered) == 5 and ordered[4].text == "same" and ordered[4].match == "exact",
        "张" in f.texts(all_off) and f.item(all_off,"赞").match == "abbreviation",
        "张" in f.texts(all_on) and "赞" in f.texts(all_on),
        all_on.items[0].text == "张" and all_on.items[0].match == "exact",
        path_bound.truncated,
        key_bound.truncated and key_bound.scanned.keys <= 1,
        post_bound.truncated and post_bound.scanned.postings <= 1,
        candidate_bound.truncated and len(candidate_bound.items) <= 1,
        ime.candidates(engine,"beijingdaxue",{fuzzy:"all",max_keys:4,max_postings:8})^.scanned.keys <= 4,
        not ime.candidates(engine,"qq")^.truncated,
        not ime.candidates(engine,"")^.truncated,
        ties[0].text == "first" and ties[1].text == "second"
    ]
}
