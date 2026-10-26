import ime: lambda.ime.chinese
import users: lambda.ime.chinese.user
import f: .mod_fixture
import k: .mod_session

pn main() {
    let engine = f.engine()^
    let initial = ime.session(engine)^
    let valid = {format:"lambda.ime.chinese.user",format_version:1,profile:"zh-Hans",next_seq:2,
        entries:[["北京","bei'jing",1,1]]}
    let malformed = [null,[],{},
        {*:valid,format:"other"},{*:valid,format_version:2},{*:valid,profile:"other"},
        {*:valid,entries:null},{*:valid,next_seq:-1},{*:valid,next_seq:1.5},
        {*:valid,next_seq:9007199254740992.0},
        {*:valid,entries:[["北京","bei'jing",1]]},
        {*:valid,entries:[["北京","bei'jing",0,1]]},
        {*:valid,entries:[["北京","bei'jing",1.5,1]]},
        {*:valid,entries:[["北京","bei'jing",1,-1]]},
        {*:valid,entries:[["北京","bei'jing",1,2]]},
        {*:valid,entries:[*valid.entries,*valid.entries]},
        {*:valid,entries:[["","",1,1]]},
        {*:valid,entries:[["hello","bei'jing",1,1]]},
        {*:valid,entries:[["😀","bei",1,1]]},
        {*:valid,entries:[["北京","bei",1,1]]},
        {*:valid,entries:[["北京","bei'notpinyin",1,1]]},
        {*:valid,entries:[["北京","Bei'jing",1,1]]},
        {*:valid,entries:[["北京","bei''jing",1,1]]},
        {*:valid,entries:[["北京","bei'jing'",1,1]]},
        {*:valid,entries:[["北北北北北北北北北北","bei'bei'bei'bei'bei'bei'bei'bei'bei'bei",1,1]]},
        // Han compatibility forms that NFC changes are not canonical user words.
        {*:valid,entries:[["\uF900","bei",1,1]]}]
    let rejected = [for (value in malformed) (ime.with_user(initial,value) ^ { ^.code }) == 7830]
    let astral = ime.with_user(initial,{*:valid,entries:[["\u{20000}","bei",1,1]]})^
    let ideographic_zero = ime.with_user(initial,{*:valid,entries:[["〇","bei",1,1]]})^
    let exhausted = ime.with_user(initial,{*:valid,next_seq:9007199254740991})^
    let count_max = ime.with_user(initial,{*:valid,entries:[["北京","bei'jing",9007199254740991,1]]})^
    let sequence = k.typed(engine,exhausted,"beijing")^
    let counter = k.typed(engine,count_max,"beijing")^;
    [all(rejected),len(rejected) == 26,
        astral.user.entries[0][0] == "\u{20000}" and len(astral.user.entries[0][0]) == 1,
        ideographic_zero.user.entries[0][0] == "〇",
        (k.press(engine,sequence," ") ^ { ^.code }) == 7830,
        (k.press(engine,counter," ") ^ { ^.code }) == 7830,
        exhausted.user.next_seq == 9007199254740991 and count_max.user.entries[0][2] == 9007199254740991,
        ime.with_user(initial,users.empty("zh-Hans"))^.user.entries == []]
}
