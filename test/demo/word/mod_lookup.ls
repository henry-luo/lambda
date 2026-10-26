// Review lookups: an English dictionary (dictionaryapi.dev) and Google Translate.
// A document-owned task makes each request; its outcome returns to the window as
// a word_lookup event, and only the latest request's outcome is shown.
import dom
import lambda.editor.mod_doc
import lambda.editor.mod_editor
import model: .mod_model

pub let dictionary_api = "https://api.dictionaryapi.dev/api/v2/entries/en/"
// Google's keyless web endpoint (client=gtx); the Cloud Translation API needs a key.
pub let translate_api = "https://translate.googleapis.com/translate_a/single?client=gtx&dt=t"
// GET query strings stay well under the endpoint's URL limit.
pub let max_text = 2000
pub let languages = [{value:"es",label:"Spanish"},{value:"fr",label:"French"},{value:"de",label:"German"},
    {value:"it",label:"Italian"},{value:"pt",label:"Portuguese"},{value:"ru",label:"Russian"},
    {value:"zh-CN",label:"Chinese (Simplified)"},{value:"zh-TW",label:"Chinese (Traditional)"},
    {value:"ja",label:"Japanese"},{value:"ko",label:"Korean"},{value:"ar",label:"Arabic"},
    {value:"hi",label:"Hindi"},{value:"en",label:"English"}]
pub fn language_name(code) => [for (entry in languages where entry.value==code) entry.label][0] or code

pub fn dictionary_url(word) => dictionary_api ++ url_encode(lower(trim(word)))
// the API ignores case, but its Cloudflare edge caches each spelling apart and can keep a stale HTML 404 for one;
// the capitalized spelling is a second key to the same entry.
pub fn dictionary_urls(word) {
    let w = lower(trim(word));
    unique([dictionary_url(w),dictionary_api ++ url_encode(upper(slice(w,0,1)) ++ slice(w,1))])
}
pub fn translate_url(text,target) => translate_api ++ "&sl=auto&tl=" ++ url_encode(target) ++ "&q=" ++ url_encode(text)

// the run of letters around a caret offset; "" between words.
pub fn word_at(text,offset) => [for (m in find(text,\("\a"+)) where m.index<=offset and offset<=m.index+len(m.value)) m.value][0] or ""
fn caret_word(editor) {
    let sel = editor.selection
    let leaf = if (sel.kind=='text') node_at(editor.doc,sel.head.path) else null;
    if (leaf!=null and is_text(leaf)) word_at(leaf.text,sel.head.offset) else ""
}
// the dictionary looks up one headword: the text's first line.
pub fn query(kind,text) => if (kind=="dictionary") trim(split(trim(text),"\n")[0]) else trim(text)
// the lookup source: the selection, else the caret's word (dictionary) or paragraph (translate).
// selection is the range a translation replaces when inserted.
pub fn source(editor,kind) {
    let picked = query(kind,model.selected_lines(editor))
    let paragraph = if (picked=="" and kind=="translate") model.select_paragraph(editor) else null;
    if (picked!="") {text:picked,selection:editor.selection}
    else if (paragraph!=null) {text:trim(model.selected_lines(paragraph)),selection:paragraph.selection}
    else {text:caret_word(editor),selection:null}
}
pub fn pane(kind,text,previous,selection=null,revision=null) => {kind:kind,query:text,target:previous.target or "es",
    seq:previous.seq or 0,loading:false,result:null,error:null,selection:selection,revision:revision}
pub fn request(pane) => {kind:pane.kind,seq:pane.seq,text:trim(pane.query),
    urls:if (pane.kind=="dictionary") dictionary_urls(pane.query) else [translate_url(slice(trim(pane.query),0,max_text),pane.target)]}

// dictionaryapi.dev answers an array of entries per spelling; parts of speech keep their order.
pub fn read_dictionary(body) {
    let data = (parse(body,'json') ^ { null }) or []
    let entries = if (data is array) data else [];
    if (len(entries)==0) null else {word:entries[0].word,
        phonetic:entries[0].phonetic or [for (e in entries) for (p in e.phonetics or [] where p.text) p.text][0],
        meanings:[for (e in entries) for (m in e.meanings or []) {part:m.partOfSpeech,
            definitions:[for (d in take(m.definitions or [],4)) {text:d.definition,example:d.example}],
            synonyms:take(unique([*(m.synonyms or []),*[for (d in m.definitions or []) for (s in d.synonyms or []) s]]),8)}]}
}
// gtx answers [[[translated, original, …], …], …, detected source language, …].
pub fn read_translation(body) {
    let data = (parse(body,'json') ^ { null }) or []
    let parts = if (data is array and data[0] is array) data[0] else [];
    if (len(parts)==0) null else {text:join([for (part in parts where part[0] is string) part[0]],""),source:data[2]}
}
// only the API's own JSON 404 means the word is unknown; an HTML 404 is the edge's error page.
pub fn not_found(reply) => reply.status==404 and reply.body is string and
    ((parse(reply.body,'json') ^ { null }).title is string)
pub fn outcome(req,reply) {
    let read = if (not reply.ok or not (reply.body is string)) null
        else if (req.kind=="dictionary") read_dictionary(reply.body) else read_translation(reply.body)
    let service = if (req.kind=="dictionary") "dictionary" else "translation";
    if (read!=null) {result:read}
    else if (req.kind=="dictionary" and not_found(reply)) {error:"No definitions found for “" ++ req.text ++ "”."}
    else if (not reply.ok) {error:"The " ++ service ++ " service is unavailable right now (HTTP " ++ string(reply.status) ++ "). Try again later."}
    else {error:"The " ++ service ++ " service sent an unreadable answer."}
}
// a settled reply ends the attempts: an answer, or the API's own not-found.
pub fn settled(req,reply) => outcome(req,reply).result!=null or (req.kind=="dictionary" and not_found(reply))
pub pn run(root,req) {
    var reply = null
    var failure = null
    var i = 0
    while (i<len(req.urls) and (i==0 or failure is error or not settled(req,reply))) {
        failure = null
        io.fetch(req.urls[i],{timeout:15,headers:{Accept:"application/json"}}) ^ { failure = ^ } ~ { reply = ~ }
        i = i+1
    }
    let detail = {*:if (failure is error) {error:"Could not reach the service: " ++ failure.message} else outcome(req,reply),seq:req.seq}
    // a rebuild may replace the shell element, so it is looked up for each report.
    let owner = dom.get_element_by_id(root,"word")
    if (owner!=null) dom.dispatch(owner,{type:"word_lookup",bubbles:false,detail:detail})
}
// a closed pane or a newer request drops the report.
pub fn receive(pane,detail) => if (pane==null or detail.seq!=pane.seq) null
    else {*:pane,loading:false,result:detail.result,error:detail.error}
