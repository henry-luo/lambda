import s: lambda.ime.chinese.syllables
import l: lambda.ime.chinese.lattice
import f: .mod_fixture

fn rejected(value) => (s.settings(value) ^ { ^.code }) == 7830

pn main() {
    let table = f.engine()^.syllables
    let all_pairs = s.settings("all")^
    let combinations = s.expand(table,"zan",s.settings(["z=zh","an=ang"])^)
    let cases = [
        ["z=zh","zi","zhi"], ["c=ch","ci","chi"], ["s=sh","si","shi"],
        ["n=l","nan","lan"], ["f=h","fu","hu"], ["r=l","ru","lu"],
        ["an=ang","fan","fang"], ["en=eng","ren","reng"], ["in=ing","lin","ling"],
        ["ian=iang","xian","xiang"], ["uan=uang","guan","guang"]
    ];
    [
        len(s.fuzzy_pairs) == 11 and len(all_pairs) == 11,
        all([for (word in table.words) word in s.expand(table,word,[])]),
        all([for (word in table.words) {
            let graph = l.build(table,word,[])^;
            any([for (edge in graph.edges[0]) edge.kind == "full" and edge.value == word and edge.end == len(word)])
        }]),
        all([for (case in cases) case[2] in s.expand(table,case[1],s.settings([case[0]])^) and
            case[1] in s.expand(table,case[2],s.settings([case[0]])^)]),
        all([for (word in table.words,value in s.expand(table,word,all_pairs)) table.full[value] == true]),
        sort(combinations) == ["zan","zang","zhan","zhang"],
        not ("xiang" in s.expand(table,"xian",s.settings(["an=ang"])^)),
        not ("guang" in s.expand(table,"guan",s.settings(["an=ang"])^)),
        s.expand(table,"fo",s.settings(["f=h"])^) == ["fo"],
        s.settings(["zh=z"])^ == s.settings(["z=zh"])^,
        s.settings(["z=zh","zh=z"])^ == s.settings(["z=zh"])^,
        sort(s.initials("n",s.settings(["n=l","r=l"])^)) == ["l","n","r"],
        s.settings("none")^ == [],
        rejected("unknown"), rejected(["z=s"]), rejected({fuzzy:"all"}),
        len(l.paths(l.build(table,"fangan",[])^,256).paths) > 1,
        l.paths(l.build(table,"aaaaaaaaaaaaaaaaaaaaaaaaaaaaaaaa",all_pairs)^,4).truncated,
        (l.build(table,"A",[]) ^ { ^.code }) == 7830,
        (l.build(table,"xi''an",[]) ^ { ^.code }) == 7830,
        (l.build(table,"'xian",[]) ^ { ^.code }) == 7830,
        (l.build(table,"a1",[]) ^ { ^.code }) == 7830,
        (l.build(table,join([for (i in 1 to 33) "a"],""),[]) ^ { ^.code }) == 7830
    ]
}
