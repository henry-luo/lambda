import c: .common
import p: .pack
import s: .syllables
import rank: .rank

pub fn options(value) map^ {
    let opts = c.options(value,["fuzzy","max_keys","max_paths","max_candidates","max_postings",
        "coverage_floor","group_leaders"])^;
    {fuzzy:s.settings(opts.fuzzy)^,
        max_keys:c.bounded(c.get(opts,"max_keys",256),1,32768,"max_keys")^,
        max_paths:c.bounded(c.get(opts,"max_paths",512),1,4096,"max_paths")^,
        max_candidates:c.bounded(c.get(opts,"max_candidates",128),1,4096,"max_candidates")^,
        max_postings:c.bounded(c.get(opts,"max_postings",4096),1,65536,"max_postings")^,
        coverage_floor:c.bounded(c.get(opts,"coverage_floor",rank.defaults.coverage_floor),0,65535,"coverage_floor")^,
        group_leaders:c.bounded(c.get(opts,"group_leaders",rank.defaults.group_leaders),1,128,"group_leaders")^}
}

pub fn query(path) map {
    let tokens = path.tokens
    let initials = any([for (token in tokens) token.kind == "initial"])
    let incomplete = any([for (token in tokens) token.kind == "prefix"])
    let abbreviation = initials and len(tokens) >= 2
    let parts = [for (token in tokens) if (abbreviation) token.value[0] else token.value];
    {key:string(len(tokens)) ++ ":" ++ join(parts,"'"),tokens:tokens,
        abbreviation:abbreviation,prefix:not abbreviation and (initials or incomplete),
        consumed:path.end,coverage:len(tokens),
        match:if (initials) "abbreviation" else if (incomplete) "incomplete"
            else if (any([for (token in tokens) token.fuzzy])) "fuzzy" else "exact"}
}

fn fits(posting,query) bool {
    if (not query.abbreviation) true
    else {
        let readings = split(posting.reading,"'");
        len(readings) == len(query.tokens) and all([for (i,token in query.tokens) {
            let reading = readings[i]
            let head = s.initial(string(reading));
            if (token.kind == "full") reading == token.value
            else if (token.kind == "prefix") starts_with(reading,token.value)
            else token.value == (if (head == "") reading[0] else head)
        }])
    }
}

// learned entries share the pack's token filter, including orthographic initials.
pub fn matches(reading,query) bool {
    let key = string(len(split(reading,"'"))) ++ ":" ++ reading;
    if (query.abbreviation) fits({reading:reading},query)
    else if (query.prefix) starts_with(key,query.key) else key == query.key
}

fn collect(table,line,at,query,budget,limit,ordinal = 0,items = []) map^ {
    if (at >= p.end(table,line)) {items:items,postings:ordinal,truncated:false}
    else if (ordinal >= budget or len(items) >= limit) {items:items,postings:ordinal,truncated:true}
    else {
        let post = p.posting(table,line,at,query.abbreviation)^
        let found = if (fits(post,query)) [*items,{text:post.text,reading:post.reading,weight:post.weight,
            consumed:query.consumed,coverage:query.coverage,match:query.match,line:line,posting:ordinal}] else items;
        collect(table,line,post.next,query,budget,limit,ordinal + 1,found)^
    }
}

fn scan(table,line,query,progress,opts) map^ {
    let matches = if (query.prefix) p.prefix(table,line,query.key)^ else p.equal(table,line,query.key)^;
    if (not matches) progress
    else if (progress.keys >= opts.max_keys or progress.postings >= opts.max_postings)
        {*:progress,truncated:true}
    else {
        let batch = collect(table,line,p.posting_start(table,line),query,
            opts.max_postings - progress.postings,opts.max_candidates)^
        // the posting budget bounds retained candidates; rank once after all routes.
        let next = {items:[*progress.items,*batch.items],keys:progress.keys + 1,
            postings:progress.postings + batch.postings,
            truncated:progress.truncated or batch.truncated};
        if (query.prefix) scan(table,line + 1,query,next,opts)^ else next
    }
}

fn search(engine,paths,opts,at,progress) map^ {
    if (at == len(paths)) progress
    else if (progress.keys >= opts.max_keys or progress.postings >= opts.max_postings) {*:progress,truncated:true}
    else {
        let q = query(paths[at])
        let table = if (q.abbreviation) engine.abbrev else engine.lexicon
        let first = p.lower_bound(table,q.key)^
        let next = scan(table,first,q,progress,opts)^;
        search(engine,paths,opts,at + 1,next)^
    }
}

pub fn candidates(engine,paths,opts,input_length) map^ {
    let result = search(engine,paths,opts,0,{items:[],keys:0,postings:0,truncated:false})^
    let ranked = rank.ranked(result.items,opts.coverage_floor,opts.group_leaders,input_length);
    {items:[for (item in take(ranked,opts.max_candidates)) {text:item.text,reading:item.reading,consumed:item.consumed,
        coverage:item.coverage,match:item.match,weight:item.weight}],truncated:result.truncated or len(ranked) > opts.max_candidates,
        scanned:{keys:result.keys,postings:result.postings}}
}
