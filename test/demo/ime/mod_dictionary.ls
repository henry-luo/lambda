// IME6/IME8: edit an in-memory pack value; persist only explicit local edits.
// each archived entry is [word, canonical reading, present]; the last edit replaces that pair.
import c: lambda.ime.chinese.common
import pack: lambda.ime.chinese.pack
import user: lambda.ime.chinese.user
import han: lambda.ime.chinese.han
import lines: lambda.io.line_index

pub fn empty(profile) map =>
    {format:"lambda.ime.chinese.edits",format_version:1,profile:profile,entries:[]}

pub fn reading(text) string => join([for (part in split(replace(lower(trim(text)),"'"," ")," ") where part != "") part],"'")

pub fn validate(value,engine) map^ {
    let header = c.require(value is map and value.format == "lambda.ime.chinese.edits" and
        value.format_version == 1 and value.profile == engine.profile and value.entries is array,
        "invalid dictionary customization format or profile")^
    let shape = c.require(all([for (entry in value.entries)
        entry is array and len(entry) == 3 and entry[2] is bool]),"invalid dictionary edit")^
    let checked = user.validate({*:user.empty(engine.profile),
        entries:[for (entry in value.entries) [entry[0],entry[1],1,0]]},engine.syllables,engine.profile)^;
    {format:value.format,format_version:1,profile:engine.profile,entries:value.entries}
}

pub fn change(edits,engine,word,text,present) map^ {
    let key = reading(text)
    let entries = [*[for (entry in edits.entries where not (entry[0] == trim(word) and entry[1] == key)) entry],
        [normalize(trim(word),"nfc"),key,present]];
    validate({*:edits,entries:entries},engine)^
}

fn postings(table,line,at,short,items = []) array^ =>
    if (at >= pack.end(table,line)) items else {
        let item = pack.posting(table,line,at,short)^;
        postings(table,line,item.next,short,[*items,item])^
    }

fn edit_table(table,key,word,reading,present,short) map^ {
    let line = pack.lower_bound(table,key)^
    let found = pack.equal(table,line,key)^
    let start = if (line == len(table.offsets)) len(table.bytes) else int(table.offsets[line])
    let end = if (found) pack.end(table,line) + 1 else start
    let old = if (found) postings(table,line,pack.posting_start(table,line),short)^ else []
    let same = [for (item in old where item.text == word and item.reading == reading) item]
    let kept = [for (item in old where not (item.text == word and item.reading == reading)) item]
    let items = if (present) sort([*kept,{text:word,reading:reading,
        weight:if (len(same) == 0) 0 else same[0].weight}],(item)=>[-item.weight,item.text,item.reading]) else kept
    let row = if (len(items) == 0) "" else key ++ "\t" ++ join([for (item in items)
        item.text ++ (if (short) " " ++ item.reading else "") ++ " " ++ string(item.weight)],"\t") ++ "\n"
    let bytes = slice(table.bytes,0,start)^ ++ binary(row)^ ++ slice(table.bytes,end)^;
    // removing the final posting leaves a valid empty lookup, which has no LF to index.
    if (len(bytes) == 0) {bytes:bytes,offsets:take(table.offsets,0)} else lines.from_bytes(bytes)^
}

pub fn edited(engine,entry) map^ {
    let parts = split(entry[1],"'")
    let head = string(len(parts)) ++ ":"
    let lexicon = edit_table(engine.lexicon,head ++ entry[1],entry[0],entry[1],entry[2],false)^
    let abbrev = if (len(parts) < 2) engine.abbrev else edit_table(engine.abbrev,
        head ++ join([for (part in parts) part[0]],"'"),entry[0],entry[1],entry[2],true)^;
    {*:engine,lexicon:lexicon,abbrev:abbrev}
}

pub fn restore(engine,edits,at = 0) map^ =>
    if (at == len(edits.entries)) engine else restore(edited(engine,edits.entries[at])^,edits,at + 1)^

pub fn forget(session,word,reading) map^ => {
    let entries = [for (entry in session.user.entries where not (entry[0] == word and entry[1] == reading)) entry];
    {*:session,user:user.validate({*:session.user,entries:entries},session.syllables,session.profile)^}
}

fn word_lines(table,word) array^ {
    if (len(table.offsets) == 0) [] else {
        // native UTF-8 decoding and line tests avoid decoding every posting during a word search.
        let rows = split(parse(table.bytes,"text")^,"\n");
        [for (i,row in rows where contains(row,word)) i]
    }
}

fn advance(table,query,cursor) map =>
    if (query.lines == null) {*:cursor,line:cursor.line + 1,at:0}
    else {size:1,row:cursor.row + 1,at:0,
        line:if (cursor.row + 1 < len(query.lines)) query.lines[cursor.row + 1] else len(table.offsets)}

fn scan(table,query,cursor,limit,items) map^ {
    if (cursor.line >= len(table.offsets) or (query.prefix != "" and
        not pack.prefix(table,cursor.line,string(cursor.size) ++ ":" ++ query.prefix)^)) {
        if (query.prefix != "" and cursor.size < 9) {
            let first = pack.lower_bound(table,string(cursor.size + 1) ++ ":" ++ query.prefix)^;
            scan(table,query,{size:cursor.size + 1,line:first,at:0,row:0},limit,items)^
        } else ({items:items,next:null})
    }
    else {
        let start = if (cursor.at == 0) pack.posting_start(table,cursor.line) else cursor.at;
        if (start >= pack.end(table,cursor.line)) scan(table,query,advance(table,query,cursor),limit,items)^
        else {
            let item = pack.posting(table,cursor.line,start,false)^
            let matches = query.word == "" or contains(item.text,query.word);
            if (matches and len(items) >= limit) ({items:items,next:{*:cursor,at:start,
                *:(if (query.lines == null) {} else {lines:query.lines})}})
            else scan(table,query,{*:cursor,at:item.next},limit,if (matches)
                [*items,{word:item.text,reading:item.reading,weight:item.weight}] else items)^
        }
    }
}

// word matches retain their line positions in the page cursor; pinyin uses the existing byte index.
pub fn browse(engine,text = "",cursor = null,limit = 12) map^ {
    let value = normalize(trim(text),"nfc")
    let word = if (han.valid(value)) value else ""
    let prefix = if (word == "") reading(value) else ""
    let valid = c.require(word != "" or all([for (char in prefix) char == "'" or (char >= "a" and char <= "z")]),
        "search with a Chinese word or toneless pinyin, separating syllables with spaces")^
    let matches = if (word == "") null else if (cursor != null) cursor.lines else word_lines(engine.lexicon,word)^
    let start = if (cursor != null) cursor else {size:1,
        line:if (matches != null) (if (len(matches) == 0) len(engine.lexicon.offsets) else matches[0])
            else if (prefix == "") 0 else pack.lower_bound(engine.lexicon,"1:" ++ prefix)^,at:0,row:0};
    scan(engine.lexicon,{prefix:prefix,word:word,lines:matches},start,limit,[])^
}
