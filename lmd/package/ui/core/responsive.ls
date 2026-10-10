import c: .component

// one ordered scale supplies package media rules and responsive prop validation.
pub let breakpoints = [{key:"xs",width:0},{key:"sm",width:576},{key:"md",width:768},
    {key:"lg",width:992},{key:"xl",width:1200},{key:"xxl",width:1600},{key:"xxxl",width:1920}]
pub fn validate(value, predicate, owner) bool^ {
    if (value is map) (
        let names = c.properties(value,[for (bp in breakpoints) bp.key],owner)^,
        if (len(value) == 0 or not all([for (key,part in value) predicate(part)]))
            raise c.fail(owner,"invalid responsive value") else true
    ) else if (not predicate(value)) raise c.fail(owner,"invalid value") else true
}
pub fn classes(prefix, value) => if (value is map)
    join([for (key,part in value) prefix ++ "-" ++ string(key)]," ") else ""
pub fn at(value, key, fallback) {
    let width = ([for (bp in breakpoints where bp.key == key) bp.width])[0]
    let active = if (value is map) [for (bp in breakpoints where bp.width <= width and c.has(value,bp.key)) value[bp.key]] else [value];
    if (len(active) == 0) fallback else active[len(active)-1]
}
pub fn variables(prefix, value, format_value) => if (value is map)
    join([for (key,part in value) "--" ++ prefix ++ "-" ++ string(key) ++ ":" ++ format_value(part) ++ ";"],"")
    else "--" ++ prefix ++ "-base:" ++ format_value(value) ++ ";"
pub fn rules(prefix, property) => join([for (bp in breakpoints) (
    let rule = "." ++ prefix ++ "-" ++ bp.key ++ "{" ++ property ++ ":var(--" ++ prefix ++ "-" ++ bp.key ++ ")}",
    if (bp.width == 0) rule else "@media(min-width:" ++ c.px(bp.width) ++ "){" ++ rule ++ "}")],"")
