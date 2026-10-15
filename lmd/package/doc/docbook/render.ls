// Port of xslTNG 2.8.5 rendering modes; UPSTREAM.md and port-map.json describe coverage.
import c: .common
import h: .html
import tables: .tables
import gentext: .gentext
import inlines: .inlines
import strings: lambda.latex.util

let suppressed = ["info","title","subtitle","titleabbrev","colspec","spanspec","textobject","alt","indexterm"]
let divisions = ["set","book","part","reference","partintro"]
let components = ["article","chapter","appendix","preface","dedication","colophon","acknowledgements","topic"]
let verbatim = ["programlisting","screen","literallayout","synopsis","address","funcsynopsisinfo","classsynopsisinfo"]
let formal = ["figure","example","equation","table","procedure"]
let block_map = {para:"p",simpara:"p",blockquote:"blockquote",epigraph:"blockquote",
    itemizedlist:"ul",orderedlist:"ol",variablelist:"dl",term:"dt",listitem:"li",
    bridgehead:"div",sidebar:"aside",abstract:"div",remark:"div",attribution:"div",
    informalexample:"div",informalfigure:"div",informalequation:"div",caption:"div",
    glosslist:"dl",glossentry:"div",glossterm:"dt",glossdef:"dd"}
let labels = {chapter:"Chapter",appendix:"Appendix",section:"Section",figure:"Figure",table:"Table",
    example:"Example",equation:"Equation",procedure:"Procedure",note:"Note",tip:"Tip",
    important:"Important",warning:"Warning",caution:"Caution",danger:"Danger"}
let admonitions = {note:"ⓘ",tip:"☞",important:"☝",caution:"⚠",warning:"🛑",danger:"⚡"}

fn label_parts(value) => [for (i,part in split(value,"."),piece in
    (if (i == 0) [part] else [<span class:"sep",".">,part])) piece]
fn numbered(node,context) => c.get(context.model.formal_numbers,node.key,node.number)
fn titlepage_div(children) element^ => h.make("header",{},[h.make("div",{class:"title"},children)^])^
fn numbered_title(node,context,toc = false) array^ {
    let title_nodes = nodes(c.title(node),{*:context,preserve_text:true})^
    let ordinal = numbered(node,context);
    if (ordinal == null) title_nodes else [
        if (not toc and node.tag in ["chapter","appendix"]) labels[node.tag] ++ " " else null,
        <span class:"label",*label_parts(ordinal)>,<span class:"sep",". ">,*title_nodes]
}
fn header(node,context) any^ {
    let title_nodes = c.title(node)
    let subtitle = c.child(c.child(node,"info"),"subtitle")
    let heading = "h" ++ string(node.heading)
    let headings = (if (len(title_nodes) == 0) [] else [h.make(heading,{},numbered_title(node,context)^)^]) ++
        (if (subtitle == null) [] else [h.make("h" ++ string(min(6,node.heading+1)),{class:"subtitle"},
            nodes(subtitle.children,{*:context,preserve_text:true})^)^]);
    if (len(headings) == 0) null else <header *headings>
}
fn structural_children(node) => [for (child in c.children(node) where c.is_structure(child)) child]
fn toc_items(node,context) array^ => [for (child in structural_children(node),
    let nested = if (len(structural_children(child)) == 0) [] else [<ul class:"toc",*toc_items(child,context)^>])
    h.make("li",{},[<a href:href(child,context),*numbered_title(child,context,true)^>,*nested])^]
fn formal_lists(node,context) array^ {
    let families = [{tag:"figure",plural:"figures",token:"ListofFigures"},
        {tag:"table",plural:"tables",token:"ListofTables"},{tag:"example",plural:"examples",token:"ListofExamples"},
        {tag:"equation",plural:"equations",token:"ListofEquations"},{tag:"procedure",plural:"procedures",token:"ListofProcedures"}];
    if (not (node.tag in ["book","set"])) [] else [for (family in families,
        let entries = [for (entry in c.nodes(node) where entry.tag == family.tag and len(c.title(entry)) > 0) entry]
        where len(entries) > 0 and context.options.parameters["lists-of-" ++ family.plural])
        h.make("div",{class:"list-of-" ++ family.plural ++ " lot"},[
            <div class:"title",gentext.token(family.token)>,
            <ul class:"toc",*[for (entry in entries) <li <a href:href(entry,context),*numbered_title(entry,context,true)^>>]>])^]
}
fn toc(node,context) any^ {
    let entries = [for (child in c.nodes(node) where child.key != node.key and c.is_structure(child)) child]
    let enabled = context.options.parameters["auto-toc"] and
        (node.tag in divisions or (node.tag == "article" and node.parent == null))
    let lists = formal_lists(node,context)^;
    if (not enabled or (len(lists) == 0 and (len(entries) == 0 or
        (len(entries) == 1 and not context.options.parameters["generate-trivial-toc"])))) null
    else h.make("div",{class:"list-of-titles"},[
        <div class:"lot toc",<div class:"title",gentext.token("TableofContents")> <ul class:"toc",*toc_items(node,context)^>>,
        *lists])^
}

pub fn href(target,context) => if (context.owners[target.key] == null or
    context.owners[target.key] == context.page) "#" ++ target.id
    else context.owners[target.key] ++ if (context.chunks[target.key] != null) "" else "#" ++ target.id

fn structure(node,context) element^ {
    let division = node.tag in divisions
    let classes = [node.tag,if (division) "division" else if (node.tag in components) "component" else null]
    let tag = if (division or (node.tag in components and node.parent == null)) "article" else "section"
    let body = [for (child in node.children where not (child is map and child.tag in ["info","title","subtitle"])) child]
    let first_section = c.first([for (i,child in body where child is map and c.is_section(child)) i])
    let prefix = if (first_section == null or division or c.is_section(node)) [] else nodes(take(body,first_section),context)^
    let contents = if (first_section == null or division or c.is_section(node)) nodes(body,context)^
        else [if (len(prefix) == 0) null else <div class:"db-bfs",*prefix>,*nodes(drop(body,first_section),context)^];
    let attrs = {*:h.attributes(node,classes,true),*:if (context.options.output == "chunked" and
        node.key != context.model.root.key and context.chunks[node.key] != null)
        {["db-chunk"]:context.chunks[node.key],["db-id"]:node.id} else {}};
    h.make(tag,attrs,[header(node,context)^,toc(node,context)^,*contents])^
}

fn inline_node(node,context) element^ {
    let spec = c.get(inlines.TEMPLATES,node.tag,{tag:"span",classed:true,class_attr:false})
    let tag = if (node.tag == "emphasis") (if (any([for (role in c.words(c.attr(node,"role")))
        role in ["bold","strong"]])) "strong" else "em")
        else if (context.in_verbatim == true and spec.verbatim_tag != null) spec.verbatim_tag else spec.tag
    let classes = [*if (node.tag == "emphasis" or not spec.classed) [] else [node.tag],
        if (spec.class_attr) c.attr(node,"class") else null]
    let rendered = nodes(node.children,{*:context,preserve_text:true})^;
    h.make(tag,h.attributes(node,classes),rendered)^
}

fn link(node,context) any^ {
    let target_id = c.attr(node,"linkend")
    let target = context.model.ids[target_id]
    let url = c.attr(node,"href",c.XLINK)
    let destination = if (target != null) href(target,context) else url
    let children = nodes(node.children,{*:context,preserve_text:true})^;
    if (node.tag == "xref" and target != null) {
        let title_nodes = nodes(c.title(target),{*:context,preserve_text:true})^
        let kind = if (c.is_section(target)) "section" else target.tag
        let ordinal = numbered(target,context)
        let text = if (ordinal == null) title_nodes else [c.get(labels,kind,kind) ++ " ",
            <span class:"label",*label_parts(ordinal)>,
            if (c.is_section(target)) ", “" else ", ",*title_nodes,if (c.is_section(target)) "”" else null];
        <a *:h.attributes(node,["xref","xref-" ++ kind]),href:href(target,context),*text>
    } else if (destination == null) <span class:"unresolved-reference",
        *if (len(children) > 0) children else ["[???" ++ c.get(node.attrs,"#linkend","") ++ "???]"]>
    else <a *:h.attributes(node,["link"]),href:destination,*children>
}

fn render_list(node,context) element^ {
    let items = if (node.tag == "variablelist") [for (entry in c.children(node,"varlistentry")) c.child(entry,"listitem")]
        else c.children(node,"listitem")
    let compact = c.attr(node,"spacing") == "compact" and all([for (item in items)
        len(c.children(item)) == 1 and c.children(item)[0].tag in ["para","simpara"]])
    let first_line = c.attr(node,"startingnumber")
    let numeration = {arabic:"1",upperalpha:"A",loweralpha:"a",upperroman:"I",lowerroman:"i"}
    let attrs = {*:h.attributes(node,[node.tag]),*:if (node.tag != "orderedlist") {} else
        {type:c.get(numeration,c.attr(node,"numeration"),"1"),*:if (first_line == null) {} else {start:first_line}}};
    let title_nodes = c.title(node)
    let body = nodes(node.children,{*:context,compact:compact})^
    let list_element = h.make(block_map[node.tag],attrs,body)^;
    if (len(title_nodes) == 0) list_element else h.make("div",h.attributes(node,[node.tag]),[
        titlepage_div(nodes(title_nodes,{*:context,preserve_text:true})^)^,list_element])^
}
fn variable_entry(node,context) element^ {
    let terms = [for (term in c.children(node,"term"))
        <span *:h.attributes(term,["term"]),*nodes(term.children,{*:context,preserve_text:true})^>]
    let joined = gentext.list_items("varlistentry",terms)^;
    h.make("div",h.attributes(node,["varlistentry"]),[<dt *joined>,*nodes(c.children(node,"listitem"),context)^])^
}
fn block(node,context) any^ {
    let tag = if (node.tag == "listitem" and context.model.by_key[node.parent].tag == "varlistentry") "dd"
        else if (node.tag == "bridgehead") "h" ++ string(node.heading+1) else block_map[node.tag]
    let classes = if (node.tag in ["para","simpara","listitem","term"]) [] else [node.tag]
    let kids = if (node.tag == "listitem" and context.compact == true) c.children(node)[0].children else node.children;
    h.make(tag,h.attributes(node,classes),nodes(kids,{*:context,
        preserve_text:node.tag in ["para","simpara","term","bridgehead","attribution"]})^)^
}

fn trim_lines(lines,leading,trailing) {
    if (leading and len(lines) > 0 and trim(lines[0]) == "") trim_lines(drop(lines,1),leading,trailing)
    else if (trailing and len(lines) > 0 and trim(lines[len(lines)-1]) == "") trim_lines(take(lines,len(lines)-1),leading,trailing)
    else lines
}
fn verbatim_line(line,ordinal,index,total,width,numbered,use_code,params) element^ {
    let visible = total >= params["verbatim-number-minlines"] and
        ((index == 0 and params["verbatim-number-first-line"]) or ordinal % params["verbatim-number-every-nth"] == 0)
    let digits = if (visible) string(ordinal) else ""
    let marker = if (not numbered) null else h.make("span",{class:"ln"},
        [strings.str_repeat(" ",width-len(digits)) ++ digits ++ " ",
            if (params["verbatim-number-separator"] == "") null
            else <span class:"nsep",params["verbatim-number-separator"]>])^
    let line_content = if (use_code) <code line> else line;
    h.make("span",{class:"line",*:if (numbered) {["db-line"]:string(ordinal)} else {}},
        [marker,<span class:"ld",line_content>])^
}
fn verbatim_block(node,context) element^ {
    let params = context.options.parameters
    let raw_lines = trim_lines(split(c.text(node),"\n"),params["verbatim-trim-leading-blank-lines"],params["verbatim-trim-trailing-blank-lines"])
    // xslTNG protects interior empty lines from collapsing in the renderer.
    let lines = [for (line in raw_lines) if (line == "") " " else line]
    let first_line = if (c.attr(node,"startinglinenumber") == null) 1 else int(c.attr(node,"startinglinenumber"))^
    let numbered = if (c.attr(node,"linenumbering") == null) node.tag == "programlisting"
        else c.attr(node,"linenumbering") == "numbered"
    // Explicit upstream element styles take precedence over verbatim-style-default.
    let plain = node.tag in ["address","literallayout","funcsynopsisinfo","classsynopsisinfo"]
    let use_code = not (node.tag in ["address","literallayout"])
    let language = c.get(node.attrs,"#language",if (node.tag == "programlisting") "none" else null)
    let classes = [if (language == null) null else "language-" ++ language,
        if (numbered) "numbered" else null,
        if (numbered and len(lines) >= params["verbatim-number-minlines"]) "long" else null,
        c.attr(node,"class"),node.tag,"verbatim",if (plain) null else "verblines"]
    let content_nodes = if (plain) [if (use_code) <code join(lines,"\n")> else join(lines,"\n")]
        else [for (i,line in lines,piece in (if (i == 0) [] else ["\n"]) ++
            [verbatim_line(line,first_line+i,i,len(lines),len(string(len(lines)+first_line-1)),numbered,use_code,params)^]) piece];
    <div class:("pre-wrap " ++ node.tag ++ "-wrap"),*:if (numbered)
        {["db-startinglinenumber"]:string(first_line),["db-numberoflines"]:string(len(lines))} else {},
        <pre *:h.attributes(node,classes),*content_nodes>>
}

fn table(node,context) element^ {
    let groups = c.children(node,"tgroup")
    let classes = [node.tag,if (node.tag == "table") "formalobject" else "informalobject"]
    let attrs = h.attributes(node,classes,true)
    let contents = if (len(groups) > 0) [for (group in groups) tables.cals(group,context,nodes)^]
        else [<table *:map([for (key,value in attrs where string(key) != "id") (key,value)]),
            *nodes([for (child in node.children where not (child is map and child.tag in ["info","title"])) child],context)^>]
    h.make("div",attrs,[formal_header(node,context)^,*contents])^
}

fn formal_header(node,context) any^ {
    let titled = c.title(node)
    let ordinal = numbered(node,context);
    if (len(titled) == 0) null else titlepage_div(
        [c.get(labels,node.tag,node.tag) ++ " ",<span class:"label",*label_parts(ordinal)>,<span class:"sep",". ">,
            *nodes(titled,{*:context,preserve_text:true})^])^
}
fn formal_object(node,context) element^ {
    let body = nodes(node.children,context)^
    let titlepage = formal_header(node,context)^;
    h.make("div",h.attributes(node,[node.tag,"formalobject"],true),
        [*body,titlepage])^
}
fn admonition(node,context) element^ {
    let title = c.title(node)
    let titlepage = titlepage_div(if (len(title) == 0) [labels[node.tag]] else nodes(title,{*:context,preserve_text:true})^)^
    let body = h.make("div",{class:"body"},[titlepage,<div *nodes(node.children,context)^>])^;
    h.make("div",h.attributes(node,["admonition",node.tag]),[h.make("div",{},[
        <div class:"icon",admonitions[node.tag]>,body])^])^
}
fn note_number(node,context) => context.model.notes[node.key]
fn note_reference(node,context) any^ {
    let target = if (node.tag == "footnote") node else context.model.ids[c.attr(node,"linkend")]
    let ordinal = note_number(target,context);
    if (target == null or target.tag != "footnote") <sup class:"footnote-number","[???]">
    else <sup id:(node.id ++ "-fref"),class:"footnote-number",
        <a href:(href(target,context) ++ "-fnote"),string(ordinal)>>
}
fn note_body(node,context) element^ {
    let ordinal = note_number(node,context)
    let reference = <sup class:"footnote-number",
        <a href:(href(node,context) ++ "-fref"),string(ordinal)>>;
    h.make("div",{class:"footnote",id:node.id ++ "-fnote"},[
        <div class:"footnote-number",reference>,<div class:"footnote-body",*nodes(node.children,context)^>])^
}
fn media_object(node,context) element^ {
    let image = c.first([for (entry in c.nodes(node) where entry.tag == "imagedata") entry])
    let alternate = c.first([for (entry in c.nodes(node) where entry.tag in ["alt","textobject"]) entry])
    let uri = if (image == null) null else c.resolve_uri(image.base_uri,c.attr(image,"fileref"))
    let asset = context.media.by_uri[uri]
    let media_nodes = if (asset.resource.bytes == null) [c.text(alternate)] else
        [<span class:"imageobject",<img src:asset.path,alt:c.text(alternate),
            width:c.attr(image,"contentwidth"),height:c.attr(image,"contentdepth")>>];
    h.make(if (node.tag == "inlinemediaobject") "span" else "div",h.attributes(node,[node.tag]),media_nodes)^
}

fn fallback(node,context) element^ => <span class:"docbook-unsupported",["data-docbook-element"]:(node.uri ++ "#" ++ node.tag),
    *nodes(node.children,context)^>

fn default_node(node,context) any^ {
    if (c.is_structure(node)) structure(node,context)^
    else if (node.tag in suppressed) null
    else if (node.tag in ["link","xref"]) link(node,context)^
    else if (node.tag in ["emphasis","phrase"] or inlines.TEMPLATES[node.tag] != null)
        inline_node(node,context)^
    else if (node.tag in ["itemizedlist","orderedlist","variablelist"]) render_list(node,context)^
    else if (node.tag in verbatim) verbatim_block(node,context)^
    else if (node.tag in ["table","informaltable"]) table(node,context)^
    else if (node.tag in formal) formal_object(node,context)^
    else if (admonitions[node.tag] != null) admonition(node,context)^
    else if (node.tag in ["footnote","footnoteref"]) note_reference(node,context)^
    else if (node.tag in ["mediaobject","inlinemediaobject"]) media_object(node,context)^
    else if (node.tag in ["thead","tbody","tfoot","tr","td","th","colgroup","col"])
        tables.html(node,context,nodes)^
    else if (block_map[node.tag] != null) block(node,context)^
    else if (node.tag == "varlistentry") variable_entry(node,context)^
    else if (starts_with(node.tag,"!") or starts_with(node.tag,"?")) null
    else fallback(node,context)^
}
fn one(value,context) any^ {
    if (value is string) {
        if (context.preserve_text == true or trim(value) != "") value else null
    } else if (c.is_structure(value) and context.owners[value.key] != null and context.owners[value.key] != context.page) null
    else if (value.uri != c.DB) {
        if (starts_with(value.tag,"!") or starts_with(value.tag,"?")) null else fallback(value,context)^
    } else {
        let handler = context.options.customization[value.tag]
        let inherited = {*:context,preserve_text:context.preserve_text == true or value.space == "preserve"};
        if (handler == null) default_node(value,inherited)^
        else handler(value,inherited,(node,ctx)=> default_node(node,ctx)^)^
    }
}
pub fn nodes(values,context) array^ => [for (value in values,let rendered = one(value,context)^ where rendered != null) rendered]

pub fn supported(node) => inlines.TEMPLATES[node.tag] != null or node.tag in (suppressed ++ divisions ++ components ++
    verbatim ++ formal ++ ["emphasis","phrase","section","sect1","sect2","sect3","sect4","sect5",
    "simplesect","refentry","refsection","refsect1","refsect2","refsect3","link","xref","varlistentry","informaltable",
    "tgroup","row","entry","thead","tbody","tfoot","tr","td","th","colgroup","col",
    "footnote","footnoteref","mediaobject","inlinemediaobject","imageobject","imagedata"]) or
    block_map[node.tag] != null or admonitions[node.tag] != null

fn navigation_link(page,caption) => if (page == null) null else <a href:page.path,caption>
fn navigation(context,position) any^ {
    let nav = context.navigation;
    if (context.options.output == "single" or not context.options.parameters["chunk-nav"]) null
    else if (position == "top") h.make("div",{},[navigation_link(nav.home,"Home")," ",
        navigation_link(nav.up,"Up")," ",navigation_link(nav.next,"Next")," ",navigation_link(nav.previous,"Prev")])^
    else h.make("table",{},[h.make("tr",{},[
        <td class:"previous",navigation_link(nav.previous,"Prev")>,<td class:"up",navigation_link(nav.up,"Up")>,
        <td class:"next",navigation_link(nav.next,"Next")>])^,
        h.make("tr",{},[<td class:"previous",*if (nav.previous == null) [] else numbered_title(nav.previous.node,context)^>,
            <td class:"up",navigation_link(nav.home,"Home")>,
            <td class:"next",*if (nav.next == null) [] else numbered_title(nav.next.node,context)^>])^])^
}
pub fn page(model,options,page_path,owners,media = null,chunks = {},nav = null) map^ {
    let context = {model:model,options:options,page:page_path,owners:owners,media:media,chunks:chunks,navigation:nav,preserve_text:false}
    let contents = nodes([c.get(model,"page_root",model.root)],context)^
    let notes = [for (node in model.nodes where node.tag == "footnote" and
        (options.output == "single" or owners[node.key] == page_path)) note_body(node,context)^]
    let footer = if (len(notes) == 0) null else h.make("footer",{},[
        h.make("div",{class:"footnotes"},[<hr>,*notes])^])^
    let main_nodes = [*contents,footer]
    let body_nodes = [<nav class:"top",navigation(context,"top")^>,<main *main_nodes>,<nav class:"bottom",navigation(context,"bottom")^>]
    let page_root = c.get(model,"page_root",model.root)
    let page_body = <body class:if (page_root.key == model.root.key) "home" else contents[0].class,*body_nodes>;
    {body:page_body,toc:[for (node in model.nodes where c.is_structure(node) and node.key != model.root.key)
        {title:c.normalized(c.title(node)),number:node.number,href:href(node,context)}]}
}
