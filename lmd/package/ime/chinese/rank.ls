// IME7: stable pack order breaks ties after match quality and weight.
// frozen baseline: p10 expected-word weight; one leader preserves its initials floor.
pub let defaults = {coverage_floor:2703,group_leaders:1}

pub fn quality(value) int =>
    if (value == "exact") 0 else if (value == "fuzzy") 1
    else if (value == "incomplete") 2 else 3

fn ordered(items) array =>
    sort(items,(item)=>[quality(string(item.match)),-item.weight,item.line,item.posting])

pub fn ranked(items,weight_floor,leaders) array {
    let sorted = ordered(items)
    let strong = [for (i in 1 to 9 where any([for (item in sorted)
        item.coverage == 10 - i and (item.coverage == 1 or item.weight >= weight_floor)])) 10 - i]
    let placed = [for (item in sorted) {
        let lower = [for (coverage in strong where coverage < item.coverage) coverage];
        {*:item,bucket:if (item.weight >= weight_floor or item.coverage == 1 or len(lower) == 0)
            item.coverage else lower[0]}
    }]
    let groups = [for (i in 1 to 9) {
        let coverage = 10 - i
        let own = [for (item in placed where item.coverage == coverage and item.bucket == coverage) item]
        // skip empty coverage levels when placing rare longer words after the leaders.
        let deferred = sort([for (item in placed where item.coverage > coverage and item.bucket == coverage) item],
            (item)=>-item.coverage);
        [*take(own,leaders),*deferred,*drop(own,leaders)]
    }]
    let merged = [for (group in groups,item in group) item]
    // keep the best route for each text, including polyphonic and prefix routes.
    let positions = map([for (i,item in reverse(merged)) (item.text,len(merged) - i - 1)]);
    [for (i,item in merged where positions[item.text] == i) item]
}
