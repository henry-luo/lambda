// One pure Mark Doc traversal for all wiki writers (S2.6, D7.2.4).
import tasks: .tasks
fn get(value,key,fallback = null) => if (value[key] == null) fallback else value[key]
fn tag(value) => if (value is element) string(name(value)) else ""
fn kids(value) => if (value is element) content(value) else []
fn times(text,repetitions) => join([for (i in 1 to repetitions) text],"")
fn result(text: string,issues: array = []) map => {text:text,diagnostics:issues}
fn issue(code,node,message) => {code:code,severity:"warning",element:tag(node),message:message}
fn merge(values,separator = "") map^ => result(join([for (value in values) value.text],separator),
    [for (value in values,diagnostic in value.diagnostics) diagnostic])
fn lost(code,node,message,text = "") map^ => result(text,[issue(code,node,message)])
fn text(value) => if (value is string) value else join([for (child in kids(value)) text(child)],"")
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
fn children(node,context) map^ => merge([for (child in kids(node)) render(child,context)^])^
fn wrap(node,context,opening,closing = null) map^ {
    let body = children(node,context)^;
    result(opening ++ body.text ++ if (closing == null) opening else closing,body.diagnostics)
}
fn effect(node,context,delimiter) map^ {
    let body = children(node,context)^;
    if (delimiter == null) result(body.text,body.diagnostics ++ [issue("unsupported-text-effect",node,"The target has no equivalent text effect")])
    else result((if (delimiter is array) delimiter[0] else delimiter) ++ body.text ++
        (if (delimiter is array) delimiter[1] else delimiter),body.diagnostics)
}
fn heading(node,context) map^ {
    let profile = context.profile
    let level = max(1,min(6,int(get(node,"level",if (len(tag(node)) == 2) slice(tag(node),1,2) else "1"))))
    let body = children(node,context)^
    let task = tasks.model(node)
    let prefix = if (profile.heading == "dot") "h" ++ string(level) ++ "."
        else if (profile.heading == "plus") "---" ++ times("+",level)
        else if (profile.heading == "bang") times("!",level)
        else times("=",if (profile.heading == "inverse") 7-level else level)
    let suffix = if (profile.heading in ["equals","inverse"]) " " ++ prefix else "";
    result(prefix ++ " " ++ body.text ++ suffix ++ "\n\n",body.diagnostics ++
        if (task.state == null) [] else [issue("unsupported-heading-task",node,"Wiki task markers apply to list items")])
}
fn code(node,context) map^ {
    let profile = context.profile
    let raw = text(node)
    let block = node.type == "block" or node.display == "block" or tag(node) == "pre"
    let language = get(node,"language","")
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
        [issue("unsupported-code-language",node,"Inline code has no language field in this profile")] else []) ++
        (if (contains(language,"\n") or contains(language,"\"") or contains(language,"}") or contains(language,">"))
        [issue("invalid-code-language",node,"Code language contains a structural delimiter")] else []) ++
        (if (contains(raw,closing)) [issue("delimiter-collision",node,"Literal code contains its closing delimiter")] else []) ++
        (if (block and language != "" and profile.block in ["tiki","zim","braces"] and profile.name != "vimwiki")
            [issue("unsupported-code-language",node,"Target code block has no language field")] else []);
    result(opening ++ (if (block) "\n" else "") ++ raw ++ (if (block and not ends_with(raw,"\n")) "\n" else "") ++ closing ++ (if (block) "\n\n" else ""),issues)
}
fn link(node,context) map^ {
    let profile = context.profile
    let destination = get(node,"href","")
    let label_result = children(node,context)^
    let target = if (destination == "") text(node) else destination
    let syntax = if (profile.links == "atlassian") "[" ++ label_result.text ++ "|" ++ target ++ "]"
        else if (profile.links == "tiki") "((" ++ target ++ "|" ++ label_result.text ++ "))"
        else if (profile.links == "twiki") "[[" ++ target ++ "][" ++ label_result.text ++ "]]"
        else if (profile.links == "xwiki") "[[" ++ label_result.text ++ ">>" ++ target ++ "]]"
        else if (profile.name == "mediawiki" and (contains(target,"://") or starts_with(target,"mailto:"))) "[" ++ target ++ " " ++ label_result.text ++ "]"
        else "[[" ++ target ++ "|" ++ label_result.text ++ "]]";
    result(syntax,label_result.diagnostics ++ if (contains(target,"|") or contains(target,"]") or contains(target,"\n"))
        [issue("invalid-link-target",node,"Wiki link target contains a structural delimiter")] else [])
}
fn image(node,context) map^ {
    let profile = context.profile
    let source = get(node,"src","")
    let alt = get(node,"alt","")
    let syntax = if (profile.links == "atlassian") "!" ++ source ++ (if (alt == "") "" else "|alt=" ++ alt) ++ "!"
        else if (profile.name == "mediawiki") "[[File:" ++ source ++ (if (alt == "") "" else "|" ++ alt) ++ "]]"
        else if (profile.name == "tikiwiki") "{img src=\"" ++ source ++ "\" alt=\"" ++ alt ++ "\"}"
        else if (profile.name == "twiki") "<img src=\"" ++ source ++ "\" alt=\"" ++ alt ++ "\" />"
        else if (profile.name == "xwiki") "[[" ++ alt ++ ">>image:" ++ source ++ "]]"
        else "{{" ++ source ++ (if (alt == "") "" else "|" ++ alt) ++ "}}";
    result(syntax,if (contains(source,"\n") or contains(source,"|") or contains(source,"\"") or contains(alt,"\"") or contains(alt,"|"))
        [issue("invalid-image",node,"Image contains a wiki structural delimiter")] else [])
}
fn list_item(node,context,index,ordered) map^ {
    let profile = context.profile
    let indentation = if (profile.name == "zimwiki") times("\t",context.depth)
        else times(" ",context.depth * profile.indent + if (profile.name in ["twiki","dokuwiki"]) profile.indent else 0)
    let marker = if (profile.lists == "repeat") context.prefix ++ if (ordered) "#" else "*"
        else indentation ++
            if (ordered) (if (profile.name == "dokuwiki") "-" else string(index) ++ ".") else (if (profile.name == "vimwiki") "-" else "*")
    let task = tasks.marker(node,profile.name)
    let task_model = tasks.model(node)
    let inline = merge([for (child in kids(node) where not (tag(child) in ["ul","ol"]) and
        not (tag(child) == "input" and child.type == "checkbox" and task_model.state != null)) render(child,{*:context,in_list:true})^])^
    let nested = merge([for (child in kids(node) where tag(child) in ["ul","ol"]) render(child,{*:context,prefix:if (profile.lists == "repeat") marker else "",depth:context.depth+1})^])^;
    result((if (profile.name == "zimwiki" and task_model.state != null) indentation else marker ++ " ") ++ task.text ++ trim(inline.text) ++ "\n" ++ nested.text,
        inline.diagnostics ++ nested.diagnostics ++ if (task.code == null) [] else [issue(task.code,node,"Task state or progress cannot be represented in the target wiki")])
}
fn render_list(node,context) map^ {
    let items = [for (child in kids(node) where tag(child) == "li") child]
    let first_number = int(get(node,"start","1"))
    let body = merge([for (i,item in items) list_item(item,context,first_number+i,tag(node) == "ol")^])^;
    result(body.text ++ (if (context.depth == 0) "\n" else ""),body.diagnostics ++
        if (first_number != 1 and context.profile.lists == "repeat") [issue("unsupported-list-start",node,"Repeated-marker lists cannot encode a starting number")] else [])
}
fn mw_attributes(node,keys) => join([for (key,value in map(node) where key in keys)
    string(key) ++ "=\"" ++ (if (value is array) join(value," ") else string(value)) ++ "\""]," ")
fn table_attribute_issues(node,profile,keys) => [for (key,value in map(node) where key != "sourcepos" and
    (profile.name != "mediawiki" or not (key in keys) or (not (value is string or value is int or value is array))))
    issue("unsupported-table-attribute",node,"Table attribute has no target mapping: " ++ string(key))]
fn rows(node,header = false) => [for (child in kids(node),row in
    (if (tag(child) == "tr") [{node:child,header:header}] else rows(child,header or tag(child) == "thead"))) row]
fn table_row(row,context) map^ {
    let profile = context.profile
    let cells = [for (cell in kids(row.node) where tag(cell) in ["td","th"]) cell]
    let values = [for (cell in cells) children(cell,context)^]
    let delimiters = [for (cell in cells) if (row.header or tag(cell) == "th")
        (if (profile.table == "atlassian") "||" else if (profile.table == "doku") "^" else if (profile.table == "creole") "|=" else "|") else "|"]
    let cell_keys = ["id","class","style","align","width","height","rowspan","colspan"]
    let row_keys = ["id","class","style","align"]
    let rendered = if (profile.table == "tiki") join([for (value in values) value.text],"|") ++ "\n"
        else if (profile.table == "mediawiki") "|-" ++
            (if (mw_attributes(row.node,row_keys) == "") "" else " " ++ mw_attributes(row.node,row_keys)) ++ "\n" ++ join([for (i,cell in cells)
        (if (row.header or tag(cell) == "th") "! " else "| ") ++
        (if (mw_attributes(cell,cell_keys) == "") "" else mw_attributes(cell,cell_keys) ++ " | ") ++ values[i].text ++ "\n"],"")
        else join([for (i,cell in cells) delimiters[i] ++ " " ++
            (if (profile.table == "twiki" and (row.header or tag(cell) == "th")) "*" else "") ++ values[i].text ++
            (if (profile.table == "twiki" and (row.header or tag(cell) == "th")) "*" else "") ++ " "],"") ++
            (if (len(cells) > 0 and profile.table == "atlassian" and row.header) "||" else if (profile.table == "doku" and row.header) "^" else "|") ++ "\n"
    let issues = table_attribute_issues(row.node,profile,row_keys) ++
        [for (cell in cells,diagnostic in table_attribute_issues(cell,profile,cell_keys)) diagnostic];
    result(rendered,[for (value in values,diagnostic in value.diagnostics) diagnostic] ++ issues)
}
fn table(node,context) map^ {
    let profile = context.profile
    let table_rows = rows(node)
    let captions = [for (child in kids(node) where tag(child) == "caption") child]
    let caption = merge([for (child in captions) children(child,context)^]," ")^
    let body = merge([for (i,row in table_rows) {
        let value = table_row(row,context)^;
        result(value.text ++ if (profile.table == "vim" and (row.header or any([for (cell in kids(row.node)) tag(cell) == "th"])))
            "|" ++ join([for (cell in kids(row.node)) "---"],"|") ++ "|\n" else "",value.diagnostics)
    }])^
    let issues = caption.diagnostics ++ [for (item in captions,diagnostic in table_attribute_issues(item,profile,[])) diagnostic] ++ (if (len(captions) > 0 and profile.table != "mediawiki")
        [issue("unsupported-table-caption",node,"Target table caption syntax is outside this profile")] else []) ++
        if (profile.table in ["pipe","tiki"] and any([for (row in table_rows,cell in kids(row.node)) row.header or tag(cell) == "th"]))
        [issue("unsupported-table-header",node,"Target table has no header-cell notation")] else [];
    if (profile.table == "mediawiki") result("{| " ++ (if (node.class == null) "class=\"wikitable\" " else "") ++ mw_attributes(node,["id","class"]) ++ "\n" ++
        (if (len(captions) == 0) "" else "|+ " ++ caption.text ++ "\n") ++ body.text ++ "|}\n\n",body.diagnostics ++ issues)
    else if (profile.table == "tiki") result("||" ++ trim(body.text) ++ "||\n\n",body.diagnostics ++ issues)
    else result(body.text ++ "\n",body.diagnostics ++ issues)
}
fn blockquote(node,context) map^ {
    let body = children(node,context)^
    let profile = context.profile;
    if (profile.block == "macro") result("{quote}\n" ++ trim(body.text) ++ "\n{quote}\n\n",body.diagnostics)
    else if (profile.name in ["xwiki","dokuwiki"]) result(join([for (line in split(trim(body.text),"\n")) "> " ++ line],"\n") ++ "\n\n",body.diagnostics)
    else result(body.text,body.diagnostics ++ [issue("unsupported-blockquote",node,"Target blockquote syntax is outside this writer profile")])
}
fn definition(node,context) map^ {
    let profile = context.profile
    let supported = profile.name in ["mediawiki","tikiwiki","vimwiki"]
    let body = merge([for (child in kids(node)) {
        let value = children(child,context)^;
        result((if (profile.name == "vimwiki") "" else if (tag(child) == "dt") "; " else ": ") ++ value.text ++
            (if (profile.name == "vimwiki" and tag(child) == "dt") ":: " else "\n"),value.diagnostics)
    }])^;
    result(body.text ++ "\n",body.diagnostics ++ if (supported) [] else [issue("unsupported-definition-list",node,"Target has no supported definition-list syntax")])
}
fn span(node,context) map^ {
    let profile = context.profile
    let body = children(node,context)^;
    if (node["data-wiki-nowiki"] != null and profile.name == "mediawiki")
        result(get(node,"source","<nowiki>" ++ text(node) ++ "</nowiki>"))
    else if (node["data-tag"] != null and profile.name == "zimwiki") result("@" ++ string(node["data-tag"]))
    else if (node["data-tag"] != null and profile.name == "vimwiki") result(
        if (get(context,"metadata_tags",false)) "" else ":" ++ string(node["data-tag"]) ++ ":")
    else if (node["data-color"] != null and profile.block == "macro") result("{color:" ++ node["data-color"] ++ "}" ++ body.text ++ "{color}",body.diagnostics)
    else if (node.id != null and body.text == "") {
        let syntax = if (profile.block == "macro") "{anchor:" ++ node.id ++ "}"
            else if (profile.name == "twiki") "#" ++ node.id ++ "\n"
            else if (profile.name == "vimwiki") null
            else if (profile.name == "zimwiki") "##" ++ node.id
            else if (profile.name == "xwiki") "{{id name=\"" ++ node.id ++ "\"/}}" else null;
        if (syntax == null) lost("unsupported-anchor",node,"Target has no supported explicit anchor",body.text)^ else result(syntax,body.diagnostics)
    } else result(body.text,body.diagnostics ++ if (node["data-tag"] != null or node["data-color"] != null or node.id != null)
        [issue("unsupported-span",node,"Span semantics cannot be represented in the target wiki")] else [])
}
fn panel(node,context) map^ {
    let titles = [for (child in kids(node) where child.class == "wiki-panel-title") child]
    let body = merge([for (child in kids(node) where child.class != "wiki-panel-title") render(child,context)^])^;
    if (node.class == "wiki-panel" and context.profile.block == "macro") result(
        "{panel" ++ (if (len(titles) == 0) "" else ":title=" ++ text(titles[0])) ++ "}\n" ++ trim(body.text) ++ "\n{panel}\n\n",body.diagnostics)
    else if (node.class == null and node.id == null) children(node,context)^
    else result(body.text,body.diagnostics ++ [issue("unsupported-container",node,"Container attributes have no target mapping")])
}
fn descendants(node) => if (not (node is element)) [] else
    [node,*[for (child in kids(node),found in descendants(child)) found]]
fn document(node,context) map^ {
    let all_nodes = descendants(node)
    let metadata = [for (child in all_nodes where tag(child) == "meta") child]
    let page_tags = unique([for (meta in metadata,value in get(meta,"tags",[])) value])
    let inline_tags = unique([for (child in all_nodes where child["data-tag"] != null) child["data-tag"]])
    let missing = [for (value in page_tags where not (value in inline_tags)) value]
    let created = if (len(metadata) == 0) null else metadata[0].created
    let profile = context.profile
    let prefix = if (profile.name == "zimwiki") "Content-Type: text/x-zim-wiki\nWiki-Format: zim 0.6\n" ++
        (if (created == null) "" else "Creation-Date: " ++ string(created) ++ "\n") ++ "\n" else ""
    let suffix = if (len(missing) > 0 and profile.name == "zimwiki") join([for (value in missing) "@" ++ value]," ") ++ "\n" else ""
    let body = children(node,{*:context,metadata_tags:profile.name in ["zimwiki","vimwiki"],metadata_created:profile.name == "zimwiki"})^;
    result(prefix ++ (if (profile.name == "vimwiki" and len(page_tags) > 0) ":" ++ join(page_tags,":") ++ ":\n" else "") ++ body.text ++
        (if (profile.name == "zimwiki") suffix else ""),body.diagnostics)
}
fn metadata(node,context) map^ => result("",[for (key,value in map(node) where
    not ((key == "tags" and get(context,"metadata_tags",false)) or (key == "created" and get(context,"metadata_created",false))))
    issue("metadata-not-serialized",node,"Metadata field has no target representation: " ++ string(key))])

fn render_node(node,context) map^ {
    let kind = tag(node)
    let profile = context.profile;
    if (node is string) result(escaped(node,profile),if (profile.name == "dokuwiki" and contains(node,"\\") and contains(node,"%%"))
        [issue("delimiter-collision",node,"Literal DokuWiki text contains both backslashes and the no-parse delimiter")] else [])
    else if (node == null) result("")
    else if (node is array or node is list) merge([for (child in node) render(child,context)^])^
    else if (kind == "doc") document(node,context)^
    else if (kind in ["body","html","document"]) children(node,context)^
    else if (kind == "meta") metadata(node,context)^
    else if (kind in ["h1","h2","h3","h4","h5","h6","heading","header"]) heading(node,context)^
    else if (kind == "p") {
        let body = children(node,context)^;
        result(body.text ++ (if (get(context,"in_list",false)) " " else "\n\n"),body.diagnostics)
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
        if (profile.br == null) lost("unsupported-line-break",node,"Target has no supported hard line break","\n")^
        else result(profile.br)
    }
    else if (kind == "hr") result("----\n\n")
    else if (kind in ["ul","ol"]) render_list(node,context)^
    else if (kind == "dl") definition(node,context)^
    else if (kind == "table") table(node,context)^
    else if (kind == "blockquote") blockquote(node,context)^
    else if (kind == "span") span(node,context)^
    else if (kind == "div") panel(node,context)^
    else if (kind == "raw") {
        if (node.format == profile.name or (node.format == "wiki" and profile.name == "mediawiki")) result(text(node) ++ (if (node.type == "block") "\n\n" else ""),
            if (node["data-kind"] == "local-tags") [issue("local-tag-scope",node,"Vimwiki local tag association requires source line or heading context")] else [])
        else lost("raw-flavor-mismatch",node,"Raw wiki source cannot be reinterpreted as another dialect",text(node))^
    } else if (kind in ["var","wiki-template"] and (node["data-wiki-template"] != null or node["data-wiki-parameter"] != null or kind == "wiki-template")) {
        let source = get(node,"source","{{" ++ get(node,"name","") ++ (if (node.arguments == null) "" else "|" ++ node.arguments) ++ "}}");
        if (profile.name == "mediawiki") result(source) else lost("raw-flavor-mismatch",node,"MediaWiki templates require MediaWiki output",source)^
    } else {
        let body = children(node,context)^;
        result(body.text,body.diagnostics ++ [issue("unsupported-element",node,"Element has no wiki mapping")])
    }
}

// Attribute handling is shared, so an otherwise supported node cannot silently lose semantics.
pub fn render(node,context) map^ {
    let value = render_node(node,context)^;
    if (not (node is element)) value
    else {
        let kind = tag(node)
        let profile = context.profile
        let block = kind in ["p","h1","h2","h3","h4","h5","h6","ul","ol","dl","table","blockquote","div"] or (kind == "code" and node.type == "block")
        let xattrs = profile.name == "xwiki" and block
        let known = ["sourcepos","version","level","type","display","language","href","namespace","src","alt","start","format",
            "data-checked","data-task-state","data-task-label","data-task-progress-min","data-task-progress-max","data-task-marker","data-task-flavor",
            "data-tag","data-color","data-wiki-nowiki","data-wiki-template","data-wiki-parameter","data-wiki-parser-function","source","name","args","default",
            "data-kind","data-diagnostic"]
        let issues = [for (key,item in map(node) where kind != "meta" and not (key in known) and
            not (key == "id" and (xattrs or kind == "span" or (profile.name == "mediawiki" and kind == "table") or (profile.name == "vimwiki" and node.id == text(node) and (kind == "strong" or starts_with(kind,"h"))))) and
            not (key == "class" and (xattrs or (profile.name == "mediawiki" and kind == "table") or item in ["contains-task-list","task-list-item","wiki-panel","wiki-panel-title"])))
            issue("unsupported-attribute",node,"Attribute has no target mapping: " ++ string(key))]
        let prefix = if (not xattrs or (node.id == null and node.class == null)) "" else "(%" ++
            (if (node.id == null) "" else " id=\"" ++ node.id ++ "\"") ++
            (if (node.class == null) "" else " class=\"" ++ string(node.class) ++ "\"") ++ " %)\n";
        result(prefix ++ value.text,value.diagnostics ++ issues)
    }
}
