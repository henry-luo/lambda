// English prose checks; original ranges use code points (S2.5.8, S17.4.1).
import front: .spelling.front_coded

pub pn load(language="en-US") map^ {
    if (language!="en-US") raise error("Spelling: only en-US is installed")
    let base = sys.lambda.home# ++ "/package/doc/spelling/data/"
    let manifest = input(base ++ "manifest.json",'json')^
    let dictionary = front.read(base ++ "en-US.dict",manifest.entries)^;
    return {language:language,dictionary:dictionary,words:[]}
}
pub fn with_words(checker,words) => {*:checker,words:unique([*checker.words,*words])}

fn letter(ch) => ch!=null and starts_with(io.unicode_category(ord(ch)) or "","L")
fn combining(ch) => ch!=null and starts_with(io.unicode_category(ord(ch)) or "","M")
fn connector(ch) => ch in ["'","’","-"]
fn part(text,i) => letter(text[i]) or combining(text[i]) or
    (connector(text[i]) and letter(text[i-1]) and letter(text[i+1]))
fn token_step(st,text,i) {
    let active = i<len(text) and part(text,i)
    let start = if (st.start==null and active and letter(text[i])) i else st.start;
    if (active) {*:st,start:start}
    else if (start==null) st
    else {start:null,items:[*st.items,{word:slice(text,start,i),start:start,end:i}]}
}
pub fn tokens(text: string) => reduce([{start:null,items:[]},*(0 to len(text))],
    (st,i)=>token_step(st,text,i)).items

// The installed profile covers Latin prose; other scripts are reported as skipped.
fn supported(word) => all([for (i in 0 to len(word)-1) {
    let cp = ord(word[i]);
    cp<128 or (cp>=192 and cp<=591) or (cp>=768 and cp<=879) or
        (cp>=7680 and cp<=7935) or word[i]=="’"
}])
fn separator(ch) => ch==null or ch in [" ","\t","\n","\r",";",",","(",")","<",">","\""]
fn atom_start(text,i,steps=0) => if (i==0 or steps==512 or separator(text[i-1])) i
    else atom_start(text,i-1,steps+1)
fn atom_end(text,i,steps=0) => if (i==len(text) or steps==512 or separator(text[i])) i
    else atom_end(text,i+1,steps+1)
// Keep addresses and numeric identifiers out of prose issues without changing offsets.
fn excluded_context(text,token) {
    let before = if (token.start==0) null else text[token.start-1]
    let after = if (token.end==len(text)) null else text[token.end]
    let atom = lower(slice(text,atom_start(text,token.start),atom_end(text,token.end)));
    (before!=null and io.unicode_category(ord(before))=="Nd") or
    (after!=null and io.unicode_category(ord(after))=="Nd") or
    contains(atom,"://") or contains(atom,"@") or starts_with(atom,"www.")
}
fn member(checker,word) bool^ => word in checker.words or front.lookup(checker.dictionary,word)^!=null
pub fn correct(checker,word: string) bool^ {
    let key = normalize(replace(word,"’","'"),"nfc")
    let folded = lower(key);
    member(checker,key)^ or ((key==upper(key) or key==upper(key[0]) ++ slice(folded,1)) and member(checker,folded)^)
}
fn checked_token(checker,text,token) map^ {
    if (not supported(token.word) or len(token.word)>128 or excluded_context(text,token)) {*:token,skipped:true,unknown:false}
    else if (correct(checker,token.word)^) {*:token,skipped:false,unknown:false}
    else if (contains(token.word,"-")) {
        let parts = split(token.word,"-")^
        let checked = [for (i,word in parts) {
            let start = token.start+sum([for (p in take(parts,i)) len(p)+1]);
            {word:word,start:start,end:start+len(word),unknown:not correct(checker,word)^}
        }];
        {*:token,skipped:false,unknown:false,parts:checked}
    }
    else {*:token,skipped:false,unknown:true}
}
pub fn check(checker,text: string) map^ {
    let checked = [for (token in tokens(text)) checked_token(checker,text,token)^]
    let issues = [for (token in checked) for (part in token.parts or [token] where part.unknown)
        {word:part.word,start:part.start,end:part.end}];
    {language:checker.language,issues:issues,checked:len(checked),skipped:len([for (t in checked where t.skipped) t])}
}
