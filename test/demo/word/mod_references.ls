// Source records and rendered fields share one undoable document transaction (D7.2.5).
import lambda.editor.mod_doc
import lambda.editor.mod_step
import lambda.editor.mod_source_pos
import lambda.editor.mod_transaction
import commands: lambda.editor.mod_commands
import inline_format: lambda.editor.mod_inline_format
import html: lambda.edit.html
import cp: lambda.doc.latex.citeproc.citeproc
import data: lambda.doc.latex.citeproc.data
import people: lambda.doc.latex.packages.bib_names
import output: lambda.doc.latex.to_html
import encoding: lambda.oauth

pub let styles = [{value:"apa",label:"APA"},{value:"chicago-author-date",label:"Chicago (author–date)"},{value:"ieee",label:"IEEE"}]
pub let types = [{value:"book",label:"Book"},{value:"article-journal",label:"Journal article"},{value:"webpage",label:"Website"}]
pub let fields = [{key:"author",label:"Author",limit:2000},{key:"title",label:"Title",limit:2000},
    {key:"year",label:"Year",limit:4},{key:"publisher",label:"Publisher",limit:500},{key:"city",label:"City",limit:200},
    {key:"container",label:"Journal / website name",limit:500},{key:"volume",label:"Volume",limit:40},
    {key:"issue",label:"Issue",limit:40},{key:"pages",label:"Pages",limit:80},{key:"doi",label:"DOI",limit:500},
    {key:"url",label:"URL",limit:2000},{key:"accessed",label:"Accessed (YYYY-MM-DD)",limit:10}]
pub let defaults = {kind:"book",corporate:false,author:"",title:"",year:"",publisher:"",city:"",container:"",volume:"",issue:"",pages:"",doi:"",url:"",accessed:""}
pub fn visible_fields(kind,show_all=false) => [for (f in fields where show_all or contains(["author","title","year"],f.key) or
    (kind=="book" and contains(["publisher","city"],f.key)) or
    (kind=="article-journal" and contains(["container","volume","issue","pages","doi"],f.key)) or
    (kind=="webpage" and contains(["container","url","accessed"],f.key))) f]
let resource_root = sys.lambda.home# ++ "/package/doc/latex/citeproc/"
let locale = input(resource_root ++ "locales/locales-en-US.xml",'text')^
let compiled = map([for (s in styles) for (part in [s.value,
    cp.compile(input(resource_root ++ "styles/" ++ s.value ++ ".csl",'text')^,{'en-US':locale},{locale:"en-US"})^]) part])
pub fn settings(doc) {
    let value = parse(attrs_get(doc.attrs,'data-word-references') or "null",'json') ^ { null };
    {style:if (contains([for (s in styles) s.value],value.style)) value.style else "apa",
        sources:if (value.sources is array) value.sources else []}
}
fn valid_source(s) => s is map and s.id is string and len(s.id)>0 and len(s.id)<=300 and
    contains([for (t in types) t.value],s.kind) and s.corporate is bool and
    all([for (f in fields) s[f.key] is string and len(s[f.key])<=f.limit]) and trim(s.title)!=""
pub fn citation(n) {
    let value = if (is_node(n) and n.tag=='a') parse(attrs_get(n.attrs,'data-word-citation') or "null",'json') ^ { null } else null;
    if (valid_source(value.source) and value.locator is string and len(value.locator)<=100) value else null
}
pub fn entries(doc) => [for (entry in descendants(doc),let value=citation(entry.node) where value!=null) {*:entry,value:value}]
// Embedded records let a copied citation carry its source into another document.
pub fn sources(doc) {
    let all_sources = [*[for (s in settings(doc).sources where valid_source(s)) s],*[for (entry in entries(doc)) entry.value.source]];
    [for (i,s in all_sources where not any([for (j,other in all_sources where j<i and other.id==s.id) true])) s]
}
pub fn source(doc,id) => [for (s in sources(doc) where s.id==id) s][0]
pub fn uses(doc,id) => len([for (entry in entries(doc) where entry.value.source.id==id) entry])
pub fn current(editor) {
    let path = commands.ancestor_tag(editor.doc,inline_format.selection_path(editor.selection),'a')
    let value = citation(node_at(editor.doc,path));
    if (value==null) null else {path:path,value:value}
}
pub fn bibliographies(doc) => [for (entry in descendants(doc) where attrs_get(entry.node.attrs,'data-word-bibliography')=="true") entry]
fn set_settings(tx,p) map^ => tx_step(tx,step_set_attr([],'data-word-references',format(p,'json')^))
fn normalize(p) map^ {
    let values = map([for (f in fields) for (part in [f.key,if (p[f.key] is string) trim(p[f.key]) else ""]) part])
    let s = {*:defaults,*:values,kind:p.kind,corporate:p.corporate==true}
    let id = if (p.id is string and p.id!="") p.id else "s-" ++ encoding.base64url(hash(format(s,'json')^,'sha256'))
    let result = {*:s,id:id}
    let year = if (s.year=="") null else int(s.year) ^ { null }
    let accessed_date = if (s.accessed=="") null else datetime(s.accessed) ^ { null };
    if (not valid_source(result)) raise error("Word: enter a title and keep source fields within their size limits")
    else if (s.year!="" and (len(s.year)!=4 or year==null or year<1 or
        not all([for (ch in split(s.year,"")) contains("0123456789",ch)]))) raise error("Word: enter a four-digit year, or leave it blank")
    else if (s.url!="" and not (starts_with(s.url,"https://") or starts_with(s.url,"http://")))
        raise error("Word: source URLs must start with https:// or http://")
    else if (s.accessed!="" and (len(s.accessed)!=10 or accessed_date==null))
        raise error("Word: enter an accessed date as YYYY-MM-DD")
    else result
}
fn csl(s) {
    let names = if (s.corporate) [{literal:s.author}] else [for (n in people.parse(replace(s.author,";"," and ")))
        if (n.literal!=null) {literal:n.literal} else {family:n.family,given:n.given,'non-dropping-particle':n.prefix,suffix:n.suffix}]
    let doi = replace(replace(s.doi,"https://doi.org/",""),"http://doi.org/","");
    {*:map([for (part in [{key:"id",value:s.id},{key:"type",value:s.kind},{key:"title",value:s.title},
        {key:"publisher",value:s.publisher},{key:"publisher-place",value:s.city},{key:"container-title",value:s.container},
        {key:"volume",value:s.volume},{key:"issue",value:s.issue},{key:"page",value:s.pages},{key:"DOI",value:doi},{key:"URL",value:s.url}]
        where part.value!="") for (item in [part.key,part.value]) item]),
        *:if (s.author=="") {} else {author:names},
        *:if (s.year=="") {} else {issued:{'date-parts':[[int(s.year)]]}},
        *:if (s.accessed=="") {} else {accessed:{'date-parts':[[for (v in split(s.accessed,"-")) int(v)]]}}}
}
pub fn rendered(doc) map^ {
    let refs = data.json([for (s in sources(doc)) csl(s)],"<Word sources>",{plain_text:true})^
    cp.process(compiled[settings(doc).style],refs.references,[for (i,entry in entries(doc))
        {id:string(i),items:[{id:entry.value.source.id,*:if (entry.value.locator=="") {} else {locator:entry.value.locator,label:"page"}}]}])^
}
fn rich(content) => html.import_text("<p>" ++ output.to_html(content) ++ "</p>")^.doc.content[0].content
fn bibliography_content(result) => [node('h2',[text("References")]),*[for (entry in result.bibliography)
    node_attrs('p',[{name:'id',value:"word-source-" ++ entry.id},{name:'style',value:"padding-left:24pt;text-indent:-24pt;"}],rich(entry.content))]]
fn refresh_citations(tx,items,result,i) => if (i<0) tx else {
    let entry = items[i]
    let value = {*:entry.value,source:source(tx.doc_after,entry.value.source.id)}
    let next = tx_step(tx_step(tx_step(tx,step_replace(entry.path,0,len(entry.node.content),rich(result.citations[i].content))),
        step_set_attr(entry.path,'data-word-citation',format(value,'json')^)),step_set_attr(entry.path,'title',value.source.title));
    refresh_citations(next,items,result,i-1)
}
fn refresh_bibliographies(tx,items,content,i) => if (i<0) tx else
    refresh_bibliographies(tx_step(tx,step_replace(items[i].path,0,len(items[i].node.content),content)),items,content,i-1)
fn refresh(tx) map^ {
    let result = rendered(tx.doc_after)^
    let next = refresh_citations(tx,entries(tx.doc_after),result,len(entries(tx.doc_after))-1)
    let bibs = bibliographies(next.doc_after);
    refresh_bibliographies(next,bibs,bibliography_content(result),len(bibs)-1)
}
fn after_citation(tx,path) {
    let parent = parent_path(path)
    let index = last_index(path)+1
    let next = if (is_text(node_at(tx.doc_after,[*parent,index]))) tx else tx_step(tx,step_replace(parent,index,index,[text("")]))
    let point = pos([*parent,index],0);
    tx_set_meta(tx_set_selection(next,text_selection(point,point)),"storedMarks",[])
}
fn insert_citation(tx,editor,s,locator) map^ {
    if (current({*:editor,doc:tx.doc_after,selection:tx.sel_after})!=null) raise error("Word: use Edit Citation to change this citation")
    else if (not (locator is string) or len(locator)>100) raise error("Word: page numbers must be at most 100 characters")
    else {
        let added = commands.cmd_insert_link({*:editor,doc:tx.doc_after,selection:tx.sel_after},"#word-source-" ++ s.id,s.title,s.title)
        if (added==null) raise error("Word: place the caret in a paragraph to insert a citation") else {
            let merged = tx_set_selection(commands.tx_apply_steps(tx,added.steps,0,len(added.steps)),added.sel_after)
            let path = commands.ancestor_tag(merged.doc_after,inline_format.selection_path(merged.sel_after),'a')
            let field = tx_step(merged,step_set_attr(path,'data-word-citation',format({source:s,locator:trim(locator)},'json')^));
            after_citation(refresh(field)^,path)
        }
    }
}
pub fn transaction(editor,id,p) map^ {
    let tx = tx_begin(editor.doc,editor.selection)
    let config = {*:settings(editor.doc),sources:sources(editor.doc)}
    let active = current(editor);
    if (id=="references.saveSource") {
        let s = normalize(p)^
        let updated = [*[for (old in config.sources where old.id!=s.id) old],s]
        let next = set_settings(tx,{*:config,sources:updated})^;
        if (p.insert==true) insert_citation(next,editor,s,p.locator or "")^ else refresh(next)^
    }
    else if (id=="references.deleteSource") {
        if (source(editor.doc,p.id)==null) raise error("Word: this source no longer exists")
        else if (uses(editor.doc,p.id)>0) raise error("Word: remove citations to this source before deleting it")
        else set_settings(tx,{*:config,sources:[for (s in config.sources where s.id!=p.id) s]})^
    }
    else if (id=="references.style") {
        if (not contains([for (s in styles) s.value],p.style)) raise error("Word: choose an available citation style")
        else refresh(set_settings(tx,{*:config,style:p.style})^)^
    }
    else if (id=="references.insertCitation") {
        let s = source(editor.doc,p.id);
        if (s==null) raise error("Word: choose an existing source") else insert_citation(tx,editor,s,p.locator or "")^
    }
    else if (id=="references.editCitation" or id=="references.removeCitation") {
        if (active==null) raise error("Word: select a citation first")
        else if (id=="references.removeCitation") {
            let path = active.path
            let next = tx_step(tx,step_replace(parent_path(path),last_index(path),last_index(path)+1,[text("")]))
            let point = pos(path,0);
            refresh(tx_set_selection(next,text_selection(point,point)))^
        }
        else if (not (p.locator is string) or len(p.locator)>100) raise error("Word: page numbers must be at most 100 characters")
        else after_citation(refresh(tx_step(tx,step_set_attr(active.path,'data-word-citation',format({*:active.value,locator:trim(p.locator)},'json')^)))^,active.path)
    }
    else if (id=="references.bibliography") {
        if (len(entries(editor.doc))==0) raise error("Word: insert a citation before adding a bibliography")
        else if (len(bibliographies(editor.doc))>0) refresh(tx)^
        else {
            let body = node_attrs('div',[{name:'data-word-bibliography',value:"true"}],bibliography_content(rendered(editor.doc)^))
            let added = commands.cmd_paste_fragment(editor,[body,node('p',[text("")])]);
            if (added==null) raise error("Word: place the caret in the document body to insert a bibliography") else added
        }
    }
    else if (id=="references.refresh") refresh(set_settings(tx,config)^)^
    else raise error("Word: unknown reference command")
}
