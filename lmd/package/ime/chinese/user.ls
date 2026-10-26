import c: .common
import han: .han
import lookup: .lookup
import rank: .rank

let counter_max = 9007199254740991

pub fn empty(profile) map =>
    {format:"lambda.ime.chinese.user",format_version:1,profile:profile,next_seq:1,entries:[],index:{}}

pub fn data(user) map => {format:user.format,format_version:user.format_version,
    profile:user.profile,next_seq:user.next_seq,entries:user.entries}

fn bucket(reading) string {
    let parts = split(reading,"'");
    string(len(parts)) ++ ":" ++ parts[0][0]
}

fn indexed(user) map {
    let sorted = sort(user.entries,(entry)=>[-entry[3],-entry[2],entry[0],entry[1]])
    let keys = unique([for (entry in sorted) bucket(entry[1])]);
    {*:user,index:map([for (key in keys) (key,[for (entry in sorted where bucket(entry[1]) == key) entry])])}
}

pub fn validate(user,table,profile = null) map^ {
    let header = c.require(user is map and user.format == "lambda.ime.chinese.user" and
        user.format_version == 1 and user.profile is string and len(user.profile) > 0 and
        (profile == null or user.profile == profile) and user.entries is array,
        "invalid user overlay format, version or profile")^
    let sequence = c.bounded(user.next_seq,0,counter_max,"user next_seq")^
    let entries = [for (entry in user.entries) {
        let shape = c.require(entry is array and len(entry) == 4 and entry[0] is string and entry[1] is string,
            "user entry must be [word, reading, count, last_seq]")^
        let reading = split(entry[1],"'")
        let word = entry[0]
        let valid = c.require(len(word) >= 1 and len(word) <= 9 and han.valid(word) and
            normalize(word,"nfc") == word and len(reading) == len(word) and
            all([for (part in reading) table.full[part] == true]),"invalid user word or reading")^
        let count = c.bounded(entry[2],1,counter_max,"user count")^
        let selected_at = c.bounded(entry[3],0,counter_max,"user last_seq")^
        let ordered = c.require(selected_at < sequence,"user last_seq must precede next_seq")^;
        [word,entry[1],count,selected_at]
    }]
    let identities = [for (entry in entries) entry[0] ++ "\t" ++ entry[1]]
    let unique_pairs = c.require(len(unique(identities)) == len(identities),"duplicate user word/reading pair")^;
    indexed({format:user.format,format_version:1,profile:user.profile,next_seq:sequence,entries:entries})
}

// selections are learned at commit; undone/canceled choices never enter the overlay.
pub fn learn(user,selections) map^ {
    if (len(selections) == 0) user
    else {
        let item = selections[0]
        let previous = [for (entry in user.entries where entry[0] == item.text and entry[1] == item.reading) entry]
        let count = if (len(previous) == 0) 1 else previous[0][2] + 1
        let valid = c.require(user.next_seq < counter_max and count <= counter_max,"user selection counter exhausted")^
        let entries = [*[for (entry in user.entries where not (entry[0] == item.text and entry[1] == item.reading)) entry],
            [item.text,item.reading,count,user.next_seq]]
        let updated = {*:user,entries:entries,next_seq:user.next_seq + 1};
        learn(indexed(updated),drop(selections,1))^
    }
}

fn scan(user,paths,opts,path_at,entry_at,inspected,items) map {
    if (path_at == len(paths)) {items:items,truncated:false}
    else {
        let q = lookup.query(paths[path_at])
        let pool = c.get(user.index,slice(q.key,0,3),[]);
        if (entry_at == len(pool)) scan(user,paths,opts,path_at + 1,0,inspected,items)
        else if (inspected >= opts.max_postings) {items:items,truncated:true}
        else {
            let entry = pool[entry_at]
            let found = if (lookup.matches(entry[1],q)) [*items,{text:entry[0],reading:entry[1],
                consumed:q.consumed,coverage:q.coverage,match:q.match,weight:0,
                count:entry[2],last_seq:entry[3],order:inspected}] else items;
            scan(user,paths,opts,path_at,entry_at + 1,inspected + 1,found)
        }
    }
}

pub fn promote(user,paths,base,opts) map^ {
    let matches = scan(user,paths,opts,0,0,0,[])
    let weights = map([for (item in base.items) (item.text ++ "\t" ++ item.reading,item.weight)])
    let ordered = sort([for (item in matches.items) {*:item,
        weight:c.get(weights,item.text ++ "\t" ++ item.reading,0)}],
        (item)=>[-item.last_seq,-item.count,-item.coverage,rank.quality(string(item.match)),-item.weight,item.order])
    let combined = rank.distinct([*ordered,*base.items]);
    {items:[for (item in take(combined,opts.max_candidates)) {text:item.text,reading:item.reading,
        consumed:item.consumed,coverage:item.coverage,match:item.match,weight:item.weight}],
        truncated:base.truncated or matches.truncated or len(combined) > opts.max_candidates,scanned:base.scanned}
}
