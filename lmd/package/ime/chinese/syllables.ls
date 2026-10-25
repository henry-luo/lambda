import c: .common

pub let fuzzy_pairs = [
    {name:"z=zh",kind:"initial",left:"z",right:"zh",example:"zong / zhong"},
    {name:"c=ch",kind:"initial",left:"c",right:"ch",example:"cang / chang"},
    {name:"s=sh",kind:"initial",left:"s",right:"sh",example:"si / shi"},
    {name:"n=l",kind:"initial",left:"n",right:"l",example:"nan / lan"},
    {name:"f=h",kind:"initial",left:"f",right:"h",example:"fu / hu"},
    {name:"r=l",kind:"initial",left:"r",right:"l",example:"ru / lu"},
    {name:"an=ang",kind:"final",left:"an",right:"ang",example:"fan / fang"},
    {name:"en=eng",kind:"final",left:"en",right:"eng",example:"ren / reng"},
    {name:"in=ing",kind:"final",left:"in",right:"ing",example:"lin / ling"},
    {name:"ian=iang",kind:"final",left:"ian",right:"iang",example:"xian / xiang"},
    {name:"uan=uang",kind:"final",left:"uan",right:"uang",example:"guan / guang"}
]

pub fn initial(value: string) string =>
    if (slice(value,0,2) in ["zh","ch","sh"]) slice(value,0,2)
    else if (value[0] in ["a","o","e"]) "" else value[0]

pub fn inventory(text: string) map {
    let words = split(trim(text),"\n")
    let prefixes = unique([for (word in words,i in 1 to len(word)) slice(word,0,i)]);
    {words:words,full:map([for (word in words) (word,true)]),
        prefixes:map([for (prefix in prefixes) (prefix,true)]),
        initials:unique([for (word in words) {
            let head = initial(word);
            if (head == "") word[0] else head
        }])}
}

pub fn settings(value = null) array^ {
    if (value == null or value == "none") []
    else if (value == "all") [for (pair in fuzzy_pairs) pair]
    else {
        let valid = c.require(value is array,"fuzzy must be none, all, or an array of pair names")^
        let selected = [for (name in value) {
            let matched = [for (pair in fuzzy_pairs where name == pair.name or
                name == pair.right ++ "=" ++ pair.left) pair]
            let known = c.require(len(matched) == 1,"unknown fuzzy pair: " ++ string(name) ++
                "; supported: " ++ join([for (pair in fuzzy_pairs) pair.name],", "))^;
            matched[0]
        }];
        unique(selected)
    }
}

// pair closure lets enabled n=l and r=l compose without depending on option order.
fn closure(values, pairs, kind) array {
    let expanded = unique([*values,*[for (pair in pairs,value in values where pair.kind == kind and
        (value == pair.left or value == pair.right))
        if (value == pair.left) pair.right else pair.left]]);
    if (len(expanded) == len(values)) expanded else closure(expanded,pairs,kind)
}

pub fn initials(value,pairs) array => closure([value],pairs,"initial")
pub fn expand(table,value,pairs) array {
    let head = initial(value)
    let tail = slice(value,len(head))
    let variants = [for (a in closure([head],pairs,"initial"),b in closure([tail],pairs,"final")
        where table.full[a ++ b] == true) a ++ b];
    unique(variants)
}
