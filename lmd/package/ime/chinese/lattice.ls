import c: .common
import s: .syllables

fn next_at(text,at) => if (at < len(text) and text[at] == "'") at + 1 else at
fn edge(text,start,end,value,kind,fuzzy) =>
    {start:start,end:next_at(text,end),typed:slice(text,start,end),value:value,kind:kind,fuzzy:fuzzy}

fn at(text,position,table,pairs) array {
    if (text[position] == "'") []
    else {
        let full = [for (width in 1 to min(6,len(text) - position)) {
            let token = slice(text,position,position + width);
            [for (value in s.expand(table,token,pairs))
                edge(text,position,position + width,value,"full",value != token)]
        }]
        let initials = [for (width in 1 to min(2,len(text) - position)) {
            let token = slice(text,position,position + width);
            if (token in table.initials) [for (value in s.initials(token,pairs))
                edge(text,position,position + width,value,"initial",value != token)] else []
        }]
        let tail = slice(text,position)
        let prefix = if (table.prefixes[tail] == true)
            [edge(text,position,len(text),tail,"prefix",false)] else []
        let values = [*[for (items in full,item in items) item],
            *[for (items in initials,item in items) item],*prefix];
        // longest exact syllables lead the bounded walk; alternatives remain in the DAG.
        sort(values,(item)=>[if (item.kind == "full") (if (item.fuzzy) 1 else 0)
            else if (item.kind == "prefix") 2 else 3,-item.end,item.value])
    }
}

pub fn build(table,raw,pairs) map^ {
    let valid = c.require(raw is string,"input must be a pinyin string")^
    let text = replace(raw,"ü","v")
    let bytes = binary(text)^
    let letters = [for (byte in bytes where byte != 39) byte]
    let alphabet = c.require(all([for (byte in bytes) byte == 39 or (byte >= 97 and byte <= 122)]),
        "input accepts lowercase pinyin, ü/v and apostrophe separators")^
    let bounded = c.require(len(letters) <= 32,"input exceeds 32 letters")^
    let separators = c.require(not starts_with(text,"'") and not contains(text,"''"),
        "empty syllable before a separator")^
    let edges = [for (position in 0 to len(text) - 1) at(text,position,table,pairs)];
    {input:text,edges:edges}
}

// a leading match must belong to a complete lattice route; otherwise x in xi'an
// could invent a separate first syllable and ignore the unsegmentable i' suffix.
fn reachable(graph,at,suffix) array {
    if (at < 0) suffix
    else {
        let viable = any([for (item in graph.edges[at]) suffix[item.end - at - 1]]);
        reachable(graph,at - 1,[viable,*suffix])
    }
}

fn walk(graph,viable,pending,result,limit) map {
    if (len(pending) == 0) {paths:result,truncated:false}
    else if (len(result) >= limit) {paths:result,truncated:true}
    else {
        let current = pending[0]
        let children = if (current.end < len(graph.input) and len(current.tokens) < 9 and
            current.tokens[len(current.tokens) - 1].kind != "prefix")
            [for (item in graph.edges[current.end] where viable[item.end])
                {tokens:[*current.tokens,item],end:item.end}] else [];
        walk(graph,viable,[*children,*drop(pending,1)],[*result,current],limit)
    }
}

pub fn paths(graph,limit) map =>
    if (len(graph.input) == 0) {paths:[],truncated:false}
    else {
        let viable = reachable(graph,len(graph.input) - 1,[true]);
        walk(graph,viable,[for (item in graph.edges[0] where viable[item.end])
            {tokens:[item],end:item.end}],[],limit)
    }
