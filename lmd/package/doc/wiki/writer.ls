// One pure Mark Doc traversal for all wiki writers (S2.6, D7.2.4).
import tasks: .tasks
import c: lambda.doc.misc.common
fn twiki_word_at(chars,index) {
    let uppercase = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
    let lowercase = "abcdefghijklmnopqrstuvwxyz"
    let allowed = uppercase ++ lowercase ++ "0123456789_-.#"
    let previous = if (index == 0) "" else chars[index-1];
    if (not contains(uppercase,chars[index]) or (previous != "" and contains(allowed ++ "@",previous))) false
    else {
        let stops = [for (i in index to len(chars)-1 where not contains(allowed,chars[i])) i]
        let ending = if (len(stops) == 0) len(chars) else stops[0]
        let lowers = [for (i in index to ending-1 where contains(lowercase,chars[i])) i];
        len(lowers) > 0 and any([for (i in index to ending-1) i > lowers[0] and contains(uppercase,chars[i])])
    }
}
fn escaped(value,profile) {
    let chars = split(value,"")
    let special = "[]{}|*_~^+`'=!#<>\\%/@:.()-&";
    if (profile.name == "mediawiki" and any([for (char in split(value,"")) contains(special,char)]))
        "<nowiki>" ++ replace(value,"</nowiki>","</nowiki>&lt;/nowiki&gt;<nowiki>") ++ "</nowiki>"
    else if (profile.name == "twiki" and (any([for (char in chars) contains(special,char)]) or any([for (i,char in chars) twiki_word_at(chars,i)])))
        "<literal>" ++ replace(value,"</literal>","</literal>&lt;/literal&gt;<literal>") ++ "</literal>"
    else if (profile.name == "dokuwiki" and contains(value,"\\")) "%%" ++ value ++ "%%"
    else join([for (char in chars) if (char == "\\" and profile.name in ["jira","confluence"]) "&#92;"
        else if (contains(special,char)) profile.escape ++ char else char],"")
}
fn children(node,context) map^ => c.merge([for (child in c.kids(node)) render(child,context)^])^
fn wrap(node,context,opening,closing = null) map^ {
    let body = children(node,context)^;
    c.result(opening ++ body.text ++ if (closing == null) opening else closing,body.diagnostics)
}
fn effect(node,context,delimiter) map^ {
    let body = children(node,context)^;
    if (delimiter == null) c.result(body.text,body.diagnostics ++ [c.issue("unsupported-text-effect",node,"The target has no equivalent text effect")])
    else c.result((if (delimiter is array) delimiter[0] else delimiter) ++ body.text ++
        (if (delimiter is array) delimiter[1] else delimiter),body.diagnostics)
}
fn heading(node,context) map^ {
    let profile = context.profile
    let level = max(1,min(6,int(c.get(node,"level",if (len(c.tag(node)) == 2) slice(c.tag(node),1,2) else "1"))))
    let body = children(node,context)^
    let task = tasks.model(node)
    let prefix = if (profile.heading == "dot") "h" ++ string(level) ++ "."
        else if (profile.heading == "plus") "---" ++ c.times("+",level)
        else if (profile.heading == "bang") c.times("!",level)
        else c.times("=",if (profile.heading == "inverse") 7-level else level)
    let suffix = if (profile.heading in ["equals","inverse"]) " " ++ prefix else "";
    c.result(prefix ++ " " ++ body.text ++ suffix ++ "\n\n",body.diagnostics ++
        if (task.state == null) [] else [c.issue("unsupported-heading-task",node,"Wiki task markers apply to list items")])
}
fn code(node,context) map^ {
    let profile = context.profile
    let raw = c.text(node)
    let block = node.type == "block" or node.display == "block" or c.tag(node) == "pre"
    let language = c.get(node,"language","")
    let opening = if (not block) profile.code else if (profile.block == "mediawiki")
        (if (language == "") "<pre>" else "<syntaxhighlight lang=\"" ++ language ++ "\">")
        else if (profile.block == "twiki") "<verbatim>" else if (profile.block == "doku") "<code" ++ (if (language == "") "" else " " ++ language) ++ ">"
        else if (profile.block == "tiki") "~pp~" else if (profile.block == "zim") "'''"
        else if (profile.block == "macro") "{code" ++ (if (language == "") "" else ":language=" ++ language) ++ "}"
        else if (profile.block == "xwiki") "{{code" ++ (if (language == "") "" else " language=\"" ++ language ++ "\"") ++ "}}"
        else "{{{" ++ if (profile.name == "vimwiki" and language != "") "class=\"brush: " ++ language ++ "\"" else ""
    let closing = if (not block) profile.code_end else if (profile.block == "mediawiki")
        (if (language == "") "</pre>" else "</syntaxhighlight>") else if (profile.block == "twiki") "</verbatim>"
        else if (profile.block == "doku") "</code>" else if (profile.block == "tiki") "~/pp~"
        else if (profile.block == "zim") "'''" else if (profile.block == "macro") "{code}"
        else if (profile.block == "xwiki") "{{/code}}" else "}}}"
    let issues = (if (not block and language != "")
        [c.issue("unsupported-code-language",node,"Inline code has no language field in this profile")] else []) ++
        (if (contains(language,"\n") or contains(language,"\"") or contains(language,"}") or contains(language,">"))
        [c.issue("invalid-code-language",node,"Code language contains a structural delimiter")] else []) ++
        (if (contains(raw,closing)) [c.issue("delimiter-collision",node,"Literal code contains its closing delimiter")] else []) ++
        (if (block and language != "" and profile.block in ["tiki","zim","braces"] and profile.name != "vimwiki")
            [c.issue("unsupported-code-language",node,"Target code block has no language field")] else []);
    c.result(opening ++ (if (block) "\n" else "") ++ raw ++ (if (block and not ends_with(raw,"\n")) "\n" else "") ++ closing ++ (if (block) "\n\n" else ""),issues)
}
fn link(node,context) map^ {
    let profile = context.profile
    let destination = c.get(node,"href","")
    let label_result = children(node,context)^
    let target = if (destination == "") c.text(node) else destination
    let syntax = if (profile.links == "atlassian") "[" ++ label_result.text ++ "|" ++ target ++ "]"
        else if (profile.links == "tiki") "((" ++ target ++ "|" ++ label_result.text ++ "))"
        else if (profile.links == "twiki") "[[" ++ target ++ "][" ++ label_result.text ++ "]]"
        else if (profile.links == "xwiki") "[[" ++ label_result.text ++ ">>" ++ target ++ "]]"
        else if (profile.name == "mediawiki" and (contains(target,"://") or starts_with(target,"mailto:"))) "[" ++ target ++ " " ++ label_result.text ++ "]"
        else "[[" ++ target ++ "|" ++ label_result.text ++ "]]";
    c.result(syntax,label_result.diagnostics ++ if (contains(target,"|") or contains(target,"]") or contains(target,"\n"))
        [c.issue("invalid-link-target",node,"Wiki link target contains a structural delimiter")] else [])
}
fn image(node,context) map^ {
    let profile = context.profile
    let source = c.get(node,"src","")
    let alt = c.get(node,"alt","")
    let syntax = if (profile.links == "atlassian") "!" ++ source ++ (if (alt == "") "" else "|alt=" ++ alt) ++ "!"
        else if (profile.name == "mediawiki") "[[File:" ++ source ++ (if (alt == "") "" else "|" ++ alt) ++ "]]"
        else if (profile.name == "tikiwiki") "{img src=\"" ++ source ++ "\" alt=\"" ++ alt ++ "\"}"
        else if (profile.name == "twiki") "<img src=\"" ++ source ++ "\" alt=\"" ++ alt ++ "\" />"
        else if (profile.name == "xwiki") "[[" ++ alt ++ ">>image:" ++ source ++ "]]"
        else "{{" ++ source ++ (if (alt == "") "" else "|" ++ alt) ++ "}}";
    c.result(syntax,if (contains(source,"\n") or contains(source,"|") or contains(source,"\"") or contains(alt,"\"") or contains(alt,"|"))
        [c.issue("invalid-image",node,"Image contains a wiki structural delimiter")] else [])
}
fn list_item(node,context,index,ordered) map^ {
    let profile = context.profile
    let indentation = if (profile.name == "zimwiki") c.times("\t",context.depth)
        else c.times(" ",context.depth * profile.indent + if (profile.name in ["twiki","dokuwiki"]) profile.indent else 0)
    let marker = if (profile.lists == "repeat") context.prefix ++ if (ordered) "#" else "*"
        else indentation ++
            if (ordered) (if (profile.name == "dokuwiki") "-" else string(index) ++ ".") else (if (profile.name == "vimwiki") "-" else "*")
    let task = tasks.marker(node,profile.name)
    let task_model = tasks.model(node)
    let inline = c.merge([for (child in c.kids(node) where not (c.tag(child) in ["ul","ol"]) and
        not (c.tag(child) == "input" and child.type == "checkbox" and task_model.state != null)) render(child,{*:context,in_list:true})^])^
    let nested = c.merge([for (child in c.kids(node) where c.tag(child) in ["ul","ol"]) render(child,{*:context,prefix:if (profile.lists == "repeat") marker else "",depth:context.depth+1})^])^;
    c.result((if (profile.name == "zimwiki" and task_model.state != null) indentation else marker ++ " ") ++ task.text ++ trim(inline.text) ++ "\n" ++ nested.text,
        inline.diagnostics ++ nested.diagnostics ++ if (task.code == null) [] else [c.issue(task.code,node,"Task state or progress cannot be represented in the target wiki")])
}
fn render_list(node,context) map^ {
    let items = [for (child in c.kids(node) where c.tag(child) == "li") child]
    let first_number = int(c.get(node,"start","1"))
    let body = c.merge([for (i,item in items) list_item(item,context,first_number+i,c.tag(node) == "ol")^])^;
    c.result(body.text ++ (if (context.depth == 0) "\n" else ""),body.diagnostics ++
        if (first_number != 1 and context.profile.lists == "repeat") [c.issue("unsupported-list-start",node,"Repeated-marker lists cannot encode a starting number")] else [])
}
fn mw_attributes(node,keys) => join([for (key,value in map(node) where key in keys)
    string(key) ++ "=\"" ++ (if (value is array) join(value," ") else string(value)) ++ "\""]," ")
fn table_attribute_issues(node,profile,keys) => [for (key,value in map(node) where key != "sourcepos" and
    (profile.name != "mediawiki" or not (key in keys) or (not (value is string or value is int or value is array))))
    c.issue("unsupported-table-attribute",node,"Table attribute has no target mapping: " ++ string(key))]
fn rows(node,header = false) => [for (child in c.kids(node),row in
    (if (c.tag(child) == "tr") [{node:child,header:header}] else rows(child,header or c.tag(child) == "thead"))) row]
fn table_row(row,context) map^ {
    let profile = context.profile
    let cells = [for (cell in c.kids(row.node) where c.tag(cell) in ["td","th"]) cell]
    let values = [for (cell in cells) children(cell,context)^]
    let delimiters = [for (cell in cells) if (row.header or c.tag(cell) == "th")
        (if (profile.table == "atlassian") "||" else if (profile.table == "doku") "^" else if (profile.table == "creole") "|=" else "|") else "|"]
    let cell_keys = ["id","class","style","align","width","height","rowspan","colspan"]
    let row_keys = ["id","class","style","align"]
    let rendered = if (profile.table == "tiki") join([for (value in values) value.text],"|") ++ "\n"
        else if (profile.table == "mediawiki") "|-" ++
            (if (mw_attributes(row.node,row_keys) == "") "" else " " ++ mw_attributes(row.node,row_keys)) ++ "\n" ++ join([for (i,cell in cells)
        (if (row.header or c.tag(cell) == "th") "! " else "| ") ++
        (if (mw_attributes(cell,cell_keys) == "") "" else mw_attributes(cell,cell_keys) ++ " | ") ++ values[i].text ++ "\n"],"")
        else join([for (i,cell in cells) delimiters[i] ++ " " ++
            (if (profile.table == "twiki" and (row.header or c.tag(cell) == "th")) "*" else "") ++ values[i].text ++
            (if (profile.table == "twiki" and (row.header or c.tag(cell) == "th")) "*" else "") ++ " "],"") ++
            (if (len(cells) > 0 and profile.table == "atlassian" and row.header) "||" else if (profile.table == "doku" and row.header) "^" else "|") ++ "\n"
    let issues = table_attribute_issues(row.node,profile,row_keys) ++
        [for (cell in cells,diagnostic in table_attribute_issues(cell,profile,cell_keys)) diagnostic];
    c.result(rendered,[for (value in values,diagnostic in value.diagnostics) diagnostic] ++ issues)
}
fn table(node,context) map^ {
    let profile = context.profile
    let table_rows = rows(node)
    let captions = [for (child in c.kids(node) where c.tag(child) == "caption") child]
    let caption = c.merge([for (child in captions) children(child,context)^]," ")^
    let body = c.merge([for (i,row in table_rows) {
        let value = table_row(row,context)^;
        c.result(value.text ++ if (profile.table == "vim" and (row.header or any([for (cell in c.kids(row.node)) c.tag(cell) == "th"])))
            "|" ++ join([for (cell in c.kids(row.node)) "---"],"|") ++ "|\n" else "",value.diagnostics)
    }])^
    let issues = caption.diagnostics ++ [for (item in captions,diagnostic in table_attribute_issues(item,profile,[])) diagnostic] ++ (if (len(captions) > 0 and profile.table != "mediawiki")
        [c.issue("unsupported-table-caption",node,"Target table caption syntax is outside this profile")] else []) ++
        if (profile.table in ["pipe","tiki"] and any([for (row in table_rows,cell in c.kids(row.node)) row.header or c.tag(cell) == "th"]))
        [c.issue("unsupported-table-header",node,"Target table has no header-cell notation")] else [];
    if (profile.table == "mediawiki") c.result("{| " ++ (if (node.class == null) "class=\"wikitable\" " else "") ++ mw_attributes(node,["id","class"]) ++ "\n" ++
        (if (len(captions) == 0) "" else "|+ " ++ caption.text ++ "\n") ++ body.text ++ "|}\n\n",body.diagnostics ++ issues)
    else if (profile.table == "tiki") c.result("||" ++ trim(body.text) ++ "||\n\n",body.diagnostics ++ issues)
    else c.result(body.text ++ "\n",body.diagnostics ++ issues)
}
fn blockquote(node,context) map^ {
    let body = children(node,context)^
    let profile = context.profile;
    if (profile.block == "macro") c.result("{quote}\n" ++ trim(body.text) ++ "\n{quote}\n\n",body.diagnostics)
    else if (profile.name in ["xwiki","dokuwiki"]) c.result(join([for (line in split(trim(body.text),"\n")) "> " ++ line],"\n") ++ "\n\n",body.diagnostics)
    else c.result(body.text,body.diagnostics ++ [c.issue("unsupported-blockquote",node,"Target blockquote syntax is outside this writer profile")])
}
fn definition(node,context) map^ {
    let profile = context.profile
    let supported = profile.name in ["mediawiki","tikiwiki","vimwiki"]
    let body = c.merge([for (child in c.kids(node)) {
        let value = children(child,context)^;
        c.result((if (profile.name == "vimwiki") "" else if (c.tag(child) == "dt") "; " else ": ") ++ value.text ++
            (if (profile.name == "vimwiki" and c.tag(child) == "dt") ":: " else "\n"),value.diagnostics)
    }])^;
    c.result(body.text ++ "\n",body.diagnostics ++ if (supported) [] else [c.issue("unsupported-definition-list",node,"Target has no supported definition-list syntax")])
}
fn span(node,context) map^ {
    let profile = context.profile
    let body = children(node,context)^;
    if (node["data-wiki-nowiki"] != null and profile.name == "mediawiki")
        c.result(c.get(node,"source","<nowiki>" ++ c.text(node) ++ "</nowiki>"))
    else if (node["data-tag"] != null and profile.name == "zimwiki") c.result("@" ++ string(node["data-tag"]))
    else if (node["data-tag"] != null and profile.name == "vimwiki") c.result(
        if (c.get(context,"metadata_tags",false)) "" else ":" ++ string(node["data-tag"]) ++ ":")
    else if (node["data-color"] != null and profile.block == "macro") c.result("{color:" ++ node["data-color"] ++ "}" ++ body.text ++ "{color}",body.diagnostics)
    else if (node.id != null and body.text == "") {
        let syntax = if (profile.block == "macro") "{anchor:" ++ node.id ++ "}"
            else if (profile.name == "twiki") "#" ++ node.id ++ "\n"
            else if (profile.name == "vimwiki") null
            else if (profile.name == "zimwiki") "##" ++ node.id
            else if (profile.name == "xwiki") "{{id name=\"" ++ node.id ++ "\"/}}" else null;
        if (syntax == null) c.lost("unsupported-anchor",node,"Target has no supported explicit anchor",body.text)^ else c.result(syntax,body.diagnostics)
    } else c.result(body.text,body.diagnostics ++ if (node["data-tag"] != null or node["data-color"] != null or node.id != null)
        [c.issue("unsupported-span",node,"Span semantics cannot be represented in the target wiki")] else [])
}
fn panel(node,context) map^ {
    let titles = [for (child in c.kids(node) where child.class == "wiki-panel-title") child]
    let body = c.merge([for (child in c.kids(node) where child.class != "wiki-panel-title") render(child,context)^])^;
    if (node.class == "wiki-panel" and context.profile.block == "macro") c.result(
        "{panel" ++ (if (len(titles) == 0) "" else ":title=" ++ c.text(titles[0])) ++ "}\n" ++ trim(body.text) ++ "\n{panel}\n\n",body.diagnostics)
    else if (node.class == null and node.id == null) children(node,context)^
    else c.result(body.text,body.diagnostics ++ [c.issue("unsupported-container",node,"Container attributes have no target mapping")])
}
fn descendants(node) => if (not (node is element)) [] else
    [node,*[for (child in c.kids(node),found in descendants(child)) found]]
fn document(node,context) map^ {
    let all_nodes = descendants(node)
    let metadata = [for (child in all_nodes where c.tag(child) == "meta") child]
    let page_tags = unique([for (meta in metadata,value in c.get(meta,"tags",[])) value])
    let inline_tags = unique([for (child in all_nodes where child["data-tag"] != null) child["data-tag"]])
    let missing = [for (value in page_tags where not (value in inline_tags)) value]
    let created = if (len(metadata) == 0) null else metadata[0].created
    let profile = context.profile
    let prefix = if (profile.name == "zimwiki") "Content-Type: text/x-zim-wiki\nWiki-Format: zim 0.6\n" ++
        (if (created == null) "" else "Creation-Date: " ++ string(created) ++ "\n") ++ "\n" else ""
    let suffix = if (len(missing) > 0 and profile.name == "zimwiki") join([for (value in missing) "@" ++ value]," ") ++ "\n" else ""
    let body = children(node,{*:context,metadata_tags:profile.name in ["zimwiki","vimwiki"],metadata_created:profile.name == "zimwiki"})^;
    c.result(prefix ++ (if (profile.name == "vimwiki" and len(page_tags) > 0) ":" ++ join(page_tags,":") ++ ":\n" else "") ++ body.text ++
        (if (profile.name == "zimwiki") suffix else ""),body.diagnostics)
}
fn metadata(node,context) map^ => c.result("",[for (key,value in map(node) where
    not ((key == "tags" and c.get(context,"metadata_tags",false)) or (key == "created" and c.get(context,"metadata_created",false))))
    c.issue("metadata-not-serialized",node,"Metadata field has no target representation: " ++ string(key))])

fn render_node(node,context) map^ {
    let kind = c.tag(node)
    let profile = context.profile;
    if (node is string) c.result(escaped(node,profile),if (profile.name == "dokuwiki" and contains(node,"\\") and contains(node,"%%"))
        [c.issue("delimiter-collision",node,"Literal DokuWiki text contains both backslashes and the no-parse delimiter")] else [])
    else if (node == null) c.result("")
    else if (node is array or node is list) c.merge([for (child in node) render(child,context)^])^
    else if (kind == "doc") document(node,context)^
    else if (kind in ["body","html","document"]) children(node,context)^
    else if (kind == "meta") metadata(node,context)^
    else if (kind in ["h1","h2","h3","h4","h5","h6","heading","header"]) heading(node,context)^
    else if (kind == "p") {
        let body = children(node,context)^;
        c.result(body.text ++ (if (c.get(context,"in_list",false)) " " else "\n\n"),body.diagnostics)
    } else if (kind in ["strong","b"]) wrap(node,context,profile.strong)^
    else if (kind in ["em","i"]) wrap(node,context,profile.em)^
    else if (kind in ["del","s","strike"]) effect(node,context,profile.del)^
    else if (kind in ["u","ins"]) effect(node,context,profile.underline)^
    else if (kind == "sup") effect(node,context,profile.sup)^
    else if (kind == "sub") effect(node,context,profile.sub)^
    else if (kind == "cite") effect(node,context,profile.cite)^
    else if (kind in ["code","pre"]) code(node,context)^
    else if (kind == "a") link(node,context)^
    else if (kind == "img") image(node,context)^
    else if (kind == "br") {
        if (profile.br == null) c.lost("unsupported-line-break",node,"Target has no supported hard line break","\n")^
        else c.result(profile.br)
    }
    else if (kind == "hr") c.result("----\n\n")
    else if (kind in ["ul","ol"]) render_list(node,context)^
    else if (kind == "dl") definition(node,context)^
    else if (kind == "table") table(node,context)^
    else if (kind == "blockquote") blockquote(node,context)^
    else if (kind == "span") span(node,context)^
    else if (kind == "div") panel(node,context)^
    else if (kind == "raw") {
        if (node.format == profile.name or (node.format == "wiki" and profile.name == "mediawiki")) c.result(c.text(node) ++ (if (node.type == "block") "\n\n" else ""),
            if (node["data-kind"] == "local-tags") [c.issue("local-tag-scope",node,"Vimwiki local tag association requires source line or heading context")] else [])
        else c.lost("raw-flavor-mismatch",node,"Raw wiki source cannot be reinterpreted as another dialect",c.text(node))^
    } else if (kind in ["var","wiki-template"] and (node["data-wiki-template"] != null or node["data-wiki-parameter"] != null or kind == "wiki-template")) {
        let source = c.get(node,"source","{{" ++ c.get(node,"name","") ++ (if (node.arguments == null) "" else "|" ++ node.arguments) ++ "}}");
        if (profile.name == "mediawiki") c.result(source) else c.lost("raw-flavor-mismatch",node,"MediaWiki templates require MediaWiki output",source)^
    } else {
        let body = children(node,context)^;
        c.result(body.text,body.diagnostics ++ [c.issue("unsupported-element",node,"Element has no wiki mapping")])
    }
}

// Attribute handling is shared, so an otherwise supported node cannot silently lose semantics.
pub fn render(node,context) map^ {
    let value = render_node(node,context)^;
    if (not (node is element)) value
    else {
        let kind = c.tag(node)
        let profile = context.profile
        let block = kind in ["p","h1","h2","h3","h4","h5","h6","ul","ol","dl","table","blockquote","div"] or (kind == "code" and node.type == "block")
        let xattrs = profile.name == "xwiki" and block
        let known = ["sourcepos","version","level","type","display","language","href","namespace","src","alt","start","format",
            "data-checked","data-task-state","data-task-label","data-task-progress-min","data-task-progress-max","data-task-marker","data-task-flavor",
            "data-tag","data-color","data-wiki-nowiki","data-wiki-template","data-wiki-parameter","data-wiki-parser-function","source","name","args","default",
            "data-kind","data-diagnostic"]
        let issues = [for (key,item in map(node) where kind != "meta" and not (key in known) and
            not (key == "id" and (xattrs or kind == "span" or (profile.name == "mediawiki" and kind == "table") or (profile.name == "vimwiki" and node.id == c.text(node) and (kind == "strong" or starts_with(kind,"h"))))) and
            not (key == "class" and (xattrs or (profile.name == "mediawiki" and kind == "table") or item in ["contains-task-list","task-list-item","wiki-panel","wiki-panel-title"])))
            c.issue("unsupported-attribute",node,"Attribute has no target mapping: " ++ string(key))]
        let prefix = if (not xattrs or (node.id == null and node.class == null)) "" else "(%" ++
            (if (node.id == null) "" else " id=\"" ++ node.id ++ "\"") ++
            (if (node.class == null) "" else " class=\"" ++ string(node.class) ++ "\"") ++ " %)\n";
        c.result(prefix ++ value.text,value.diagnostics ++ issues)
    }
}
