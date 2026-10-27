// Semantic prose extraction preserves inline joins and source paths (D7.2.5).
import spelling: lambda.doc.spelling

let excluded = ['code','pre','script','style','svg','math','canvas','audio','video','img','image','raw_html','html_block']
let blocks = ['doc','presentation','slide','text','content','notes','p','h1','h2','h3','h4','h5','h6','li','td','th','tr','div','blockquote','br']
fn attr(n,key) => if (n is element) n[key]
    else [for (a in n.attrs or [] where a.name==key) a.value][0]
fn omitted(n,language) => (lower(string(language))!="en" and not starts_with(lower(string(language)),"en-")) or
    attr(n,'spellcheck') in [false,"false"] or attr(n,'data-word-change-kind')=="delete"
fn pieces(n,path=[],language="en") {
    let tag = if (n is element) name(n) else n.tag
    let lang = attr(n,'lang') or language
    let hidden_mark = any([for (m in n.marks or []) m.name in excluded or
        any([for (a in (if (m.value is array) m.value else []))
            (a.name=='spellcheck' and a.value in [false,"false"]) or
            (a.name=='data-word-change-kind' and a.value=="delete")])]);
    if (n is string) [{text:n,path:path}]
    else if (tag in excluded or omitted(n,lang) or hidden_mark) [{text:"\n",path:null}]
    else if (n.kind=='text') [{text:n.text,path:path}]
    else {
        let kids = if (n is element) content(n) else n.content or []
        let spans = [for (i,child in kids) *pieces(child,[*path,i],lang)];
        if (tag in blocks) [{text:"\n",path:null},*spans,{text:"\n",path:null}] else spans
    }
}
pub fn prose(document) {
    let spans = pieces(document)
    let indexed = reduce([{offset:0,spans:[]},*spans],(st,span)=>{offset:st.offset+len(span.text),
        spans:[*st.spans,{*:span,start:st.offset,end:st.offset+len(span.text)}]});
    {text:join([for (s in spans) s.text],""),spans:indexed.spans}
}
pub fn check(checker,document) map^ {
    let source = prose(document)
    let report = spelling.check(checker,source.text)^;
    {*:report,issues:[for (issue in report.issues) {*:issue,ranges:[for (span in source.spans
        where span.path!=null and span.start<issue.end and span.end>issue.start)
        {path:span.path,start:max(issue.start,span.start)-span.start,end:min(issue.end,span.end)-span.start}]}]}
}
