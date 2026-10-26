// IME7v2: complete readings precede partial routes, independently of frequency.
// the frozen weight floor still orders coverage within comparable match groups.
pub let defaults = {coverage_floor:2703,group_leaders:1}

pub fn quality(value) int =>
    if (value == "exact") 0 else if (value == "fuzzy") 1
    else if (value == "incomplete") 2 else 3

fn ordered(items) array =>
    sort(items,(item)=>[quality(string(item.match)),-item.weight,item.line,item.posting])

pub fn distinct(items) array {
    let positions = map([for (i,item in reverse(items)) (item.text,len(items) - i - 1)]);
    [for (i,item in items where positions[item.text] == i) item]
}

fn coverage_ranked(sorted,weight_floor,leaders) array {
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
    }];
    [for (group in groups,item in group) item]
}

fn completion(item,input_length) int {
    let match_quality = quality(string(item.match));
    if (input_length != null and item.consumed == input_length and match_quality < 2)
        match_quality else 2
}

pub fn ranked(items,weight_floor,leaders,input_length = null) array {
    let sorted = ordered(items)
    let groups = [for (tier in 0 to 2)
        coverage_ranked([for (item in sorted where completion(item,input_length) == tier) item],
            weight_floor,leaders)];
    // deduplicate after grouping so a partial route cannot hide a complete reading.
    distinct([for (group in groups,item in group) item])
}
