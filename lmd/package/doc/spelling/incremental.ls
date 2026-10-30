// paragraph caches retain lexical results, never DOM nodes or geometry (D7.2.5, S2.5.8).
import document: .document
import spelling: lambda.doc.spelling
import text_offsets: lambda.dom.edit_history

let empty_report = {language:"en-US",issues:[],checked:0,skipped:0}
fn boundary(n) => document.tag(n) in document.blocks or
    document.tag(n) in ['ul','ol','table','thead','tbody','tfoot']
fn paragraphs(n) => document.tag(n) in ['p','h1','h2','h3','h4','h5','h6']
pub fn complete(unit) => unit.baseline!=null and unit.baseline.text==unit.source.text

fn unit(n,path,language,previous,checked,first=null,offset=null) {
    let key = string(path) ++ (if (first==null) "" else ":" ++ string(first))
    let old = previous[key]
    let retained = old!=null and old.node==n and old.language==language
    let source = if (retained) old.source
        else if (first==null) document.prose(n,[],language)
        else document.from_pieces([for (i,child in document.children(n))
            *document.pieces(child,[first+i],language)])
    let blank = if (retained) old.blank else trim(source.text)==""
    let baseline = if (retained) old.baseline else if (blank) {text:source.text,report:empty_report}
        else checked[source.text] or old.baseline;
    {key:key,node:n,path:path,language:language,source:source,baseline:baseline,offset:offset,blank:blank,cache:checked,
        edited:if (old==null) len(previous)>0 else old.edited or old.source.text!=source.text}
}

// group loose inline siblings so formatting never splits one lexical word.
fn groups(kids,index=0,first=0) => if (index==len(kids))
    (if (index>first) [{first:first,kids:slice(kids,first,index)}] else [])
    else if (boundary(kids[index])) [*if (index>first) [{first:first,kids:slice(kids,first,index)}] else [],
        {first:index,node:kids[index]},*groups(kids,index+1,index+1)]
    else groups(kids,index+1,first)

fn collect(n,path,language,previous,checked) {
    let lang = document.attr(n,'lang') or language
    let kids = document.children(n);
    if (n==null or not document.included(n,lang)) []
    else if (paragraphs(n) or not any([for (child in kids) boundary(child)]))
        [unit(n,path,lang,previous,checked)]
    else [for (group in groups(kids)) *if (group.node!=null)
        collect(group.node,[*path,group.first],lang,previous,checked)
        else [unit({tag:'fragment',content:group.kids},path,lang,previous,checked,group.first)]]
}

pub fn refresh(source,previous=[]) {
    let by_path = map([for (entry in previous) (entry.key,entry)])
    // reuse the completed-text index; rebuilding it per key would hash the whole article again.
    let checked = previous[0].cache or {};
    if (source is string) {
        let lines = split(source,"\n")^
        let cursor = reduce([{offset:0,units:[]},*lines],(st,line)=>
            {offset:st.offset+len(line)+1,units:[*st.units,
                unit(line,[len(st.units)],"en",by_path,checked,null,st.offset)]});
        cursor.units
    } else if (source==null) []
    else collect(source,[],"en",by_path,checked)
}

// newly edited prose takes priority over the remainder of an opening document.
pub fn pending(units) => [for (edited in [true,false]) for (entry in units
    where entry.edited==edited and not complete(entry))
    {key:entry.key,text:entry.source.text}]

pub fn accept(units,results) {
    let by_text = map([*[for (entry in units where entry.baseline!=null) (entry.baseline.text,entry.baseline)],
        *[for (entry in results) (entry.text,entry)]])
    let by_path = map([for (entry in results) (entry.key,entry)]);
    [for (entry in units) {
        // an undo can already have restored a cached result while this worker ran.
        let baseline = by_text[entry.source.text] or
            (if (complete(entry)) entry.baseline else by_path[entry.key] or entry.baseline);
        {*:entry,baseline:baseline,cache:by_text,edited:entry.edited and baseline.text!=entry.source.text}
    }]
}

fn issues(unit) {
    let baseline = unit.baseline
    let text = unit.source.text
    let prefix = if (baseline==null) 0 else if (baseline.text==text) len(text)
        else text_offsets.common_prefix(baseline.text,text,0,min(len(baseline.text),len(text)))
    let suffix = if (baseline==null or complete(unit)) 0 else
        text_offsets.common_suffix(baseline.text,text,0,min(len(baseline.text),len(text))-prefix)
    let retained = [for (issue in baseline.report.issues or []
        where issue.end<=prefix or issue.start>=len(baseline.text)-suffix) {
        let shift = if (issue.end<=prefix) 0 else len(text)-len(baseline.text);
        {*:issue,start:issue.start+shift,end:issue.end+shift}
    }];
    [for (issue in retained where complete(unit) or spelling.issue_present(text,issue))
        {*:issue,start:issue.start+(unit.offset or 0),end:issue.end+(unit.offset or 0),
            ranges:document.ranges(unit.source,issue,unit.path)}]
}
pub fn report(units) => {language:"en-US",issues:[for (unit in units) *issues(unit)],
    checked:sum([for (unit in units where complete(unit)) unit.baseline.report.checked]),
    skipped:sum([for (unit in units where complete(unit)) unit.baseline.report.skipped])}
