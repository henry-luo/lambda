// One Mark Doc traversal serves three syntaxes; unsupported semantics remain visible.
import c: .common
import tasks: lambda.doc.wiki.tasks
import grid: .grid
fn quoted(value) => "\"" ++ replace(replace(replace(replace(c.newline(value),"\\","\\\\"),"\"","\\\""),"\n","\\n"),"\t","\\t") ++ "\""
fn escaped(value,target) {
    let normalized = c.newline(value);
    if (target == "typst") "#text(" ++ quoted(normalized) ++ ")"
    else if (target == "man") join([for (line in split(normalized,"\n"))
        "\\&" ++ replace(replace(line,"\\","\\e"),"-","\\-")],"\n")
    else {
        let entities = replace(replace(replace(normalized,"&","&amp;"),"<","&lt;"),">","&gt;")
        let safe = join([for (ch in split(entities,"")) if (contains("\\*_`#^~{}[]+|",ch)) "\\" ++ ch else ch],"");
        join([for (line in split(safe,"\n")) if (starts_with(line,"include::") or starts_with(line,"ifdef::") or
            starts_with(line,"ifndef::") or starts_with(line,"endif::") or starts_with(line,"ifeval::") or
            starts_with(line,"//") or starts_with(line,"= ") or starts_with(line,". ") or
            starts_with(line,"- ") or starts_with(line,"----") or starts_with(line,"....")) "\\" ++ line else line],"\n")
    }
}
fn children(node,context) map^ => c.merge([for (child in c.kids(node)) render(child,context)^])^
fn fence_for(source,marker) => c.times(marker,max(4,1+max([0,*[for (line in split(source,"\n"))
    if (replace(line,marker,"") == "") len(line) else 0]])))
fn wrapped(node,context,kind) map^ {
    let target = context.type
    let fonts = {strong:"B",b:"B",em:"I",i:"I",cite:"I",u:"I",code:"CR"}
    let delimiters = {strong:"**",b:"**",em:"__",i:"__",cite:"__",code:"``",sup:"^",sub:"~"}
    let typst = {strong:"strong",b:"strong",em:"emph",i:"emph",cite:"emph",u:"underline",del:"strike",s:"strike",sup:"super",sub:"sub"}
    let body = children(node,{*:context,font:c.get(fonts,kind,context.font)})^;
    if (target == "typst") c.result("#" ++ c.get(typst,kind,"text") ++ "[" ++ body.text ++ "]",body.diagnostics)
    else if (target == "man" and fonts[kind] != null) c.result("\\f[" ++ fonts[kind] ++ "]" ++ body.text ++ "\\f[" ++ context.font ++ "]",body.diagnostics)
    else if (target == "asciidoc" and delimiters[kind] != null) c.result(delimiters[kind] ++ body.text ++ delimiters[kind],body.diagnostics)
    else if (target == "asciidoc" and kind in ["u","del","s"]) c.result("[." ++ (if (kind == "u") "underline" else "line-through") ++ "]#" ++ body.text ++ "#",body.diagnostics)
    else c.result(body.text,body.diagnostics ++ [c.issue("unsupported-text-effect",node,"Target cannot represent " ++ kind)])
}
fn code(node,context) map^ {
    let source = c.text(node)
    let block = node.type == "block" or c.tag(node) == "pre"
    let language = c.get(node,"language","")
    let target = context.type;
    if (target == "typst") c.result("#raw(" ++ quoted(source) ++ ", block: " ++ (if (block) "true" else "false") ++
        (if (language == "") "" else ", lang: " ++ quoted(language)) ++ ")" ++ (if (block) "\n\n" else ""))
    else if (target == "man") c.result((if (block) ".nf\n" else "\\f[CR]") ++ escaped(source,target) ++
        (if (block) "\n.fi\n" else "\\f[" ++ context.font ++ "]"),
        if (language == "") [] else [c.issue("code-language",node,"man has no syntax-highlighting language field")])
    else if (not block) {
        c.result("`" ++ escaped(source,target) ++ "`",
            if (contains(source,"\n") or contains(source,"`")) [c.issue("inline-code-delimiter",node,"AsciiDoc inline code contains a delimiter or newline")] else [])
    } else {
        let fence = fence_for(source,"-")
        let safe_language = c.single_line(language,"code language")^;
        c.result((if (language == "") "[listing]\n" else "[source," ++ safe_language ++ "]\n") ++
            fence ++ "\n" ++ source ++ (if (ends_with(source,"\n")) "" else "\n") ++ fence ++ "\n\n",
            if (contains(language,"]") or contains(language,",")) [c.issue("code-language",node,"AsciiDoc language contains an attribute delimiter")] else [])
    }
}
fn link(node,context) map^ {
    let href = c.single_line(c.get(node,"href",""),"link target")^
    let body = children(node,context)^
    let target = context.type;
    if (href == "") c.result(body.text,body.diagnostics ++ [c.issue("missing-link-target",node,"Link has no href")])
    else if (target == "typst") c.result("#link(" ++ quoted(href) ++ ")[" ++ body.text ++ "]",body.diagnostics)
    else if (target == "man") c.result(body.text ++ " (" ++ escaped(href,target) ++ ")",body.diagnostics)
    else c.result((if (starts_with(href,"#")) "xref:" ++ slice(href,1) else "link:" ++ href) ++ "[" ++ body.text ++ "]",body.diagnostics ++
        if (contains(href,"[") or contains(href,"]") or contains(href," ")) [c.issue("link-target",node,"AsciiDoc link target contains a syntax delimiter")] else [])
}
fn image(node,context) map^ {
    let src = c.single_line(c.get(node,"src",""),"image source")^
    let alt = c.get(node,"alt","")
    let block = c.get(context,"image_block",false)
    let target = context.type;
    if (target == "typst") c.result("#image(" ++ quoted(src) ++ ", alt: " ++ quoted(alt) ++ ")")
    else if (target == "asciidoc") c.result("image:" ++ (if (block) ":" else "") ++ src ++ "[" ++ escaped(alt,target) ++ "]" ++ (if (block) "\n\n" else ""),
        if (contains(src,"[") or contains(src,"]") or contains(src," ")) [c.issue("image-source",node,"AsciiDoc image source contains a syntax delimiter")] else [])
    else c.lost("unsupported-image",node,"man has no portable image embedding",escaped(alt,target))^
}
fn heading(node,context) map^ {
    let level = int(c.get(node,"level",slice(c.tag(node),1,2)))
    let body = children(node,context)^;
    if (level == null or level < 1 or level > 6) raise c.fail("heading level must be 1..6")
    else if (context.type == "man") c.result((if (level == 1) ".SH\n" else ".SS\n") ++ body.text ++ "\n",body.diagnostics ++
        if (level > 2) [c.issue("heading-depth",node,"man has two section heading levels")] else [])
    else if (context.type == "asciidoc") c.result(c.times("=",min(6,level+1)) ++ " " ++ body.text ++ "\n\n",body.diagnostics ++
        if (level > 5) [c.issue("heading-depth",node,"AsciiDoc has five body section levels; the document title is separate")] else [])
    else c.result(c.times("=",level) ++ " " ++ body.text ++ "\n\n",body.diagnostics)
}
fn list_item(node,context,index) map^ {
    let task = tasks.model(node)
    let target = context.type
    let body = c.merge([for (i,child in c.kids(node) where not (c.tag(child) == "input" and child.type == "checkbox")) {
        let value = render(child,{*:context,in_item:true})^
        let kind = c.tag(child)
        let block = kind in ["p","ul","ol","dl","table","blockquote","div","pre"] or (kind == "code" and child.type == "block")
        let prefix = if (not block or i == 0) "" else if (target == "asciidoc" and not (kind in ["ul","ol"])) "\n+\n" else "\n";
        c.result(prefix ++ value.text,value.diagnostics)
    }])^;
    let valid_task = if (task.state != null and not task.valid) raise c.fail("invalid task attributes")
    let task_prefix = if (task.state == null) "" else if (target == "asciidoc")
        (if (task.state == "done") "[x] " else "[ ] ") else
        escaped(if (task.state == "done") "[x] " else "[ ] ",target)
    let binary_progress = task.lo == null or (task.lo == task.hi and task.lo == if (task.state == "done") 100 else 0)
    let issues = body.diagnostics ++ (if (task.state != null and (target != "asciidoc" or not (task.state in ["todo","done"]) or not binary_progress or task.label != null))
        [c.issue("task-state",node,"Target cannot retain this task workflow or progress metadata")] else []) ++
        c.unknown(node,["sourcepos","data-checked","data-task-state","data-task-label","data-task-progress-min","data-task-progress-max","data-task-marker","data-task-flavor"] ++
            (if (node.class == "task-list-item") ["class"] else []),"list item attribute")
    let prefix = if (target == "man") ".IP \"" ++ (if (context.ordered) string(index) ++ "." else "\\(bu") ++ "\" 4\n"
        else c.times(if (context.ordered) "." else "*",context.depth) ++ " ";
    if (target == "typst") c.result("[" ++ task_prefix ++ body.text ++ "]",issues)
    else c.result(prefix ++ task_prefix ++ body.text ++ "\n",issues)
}
fn render_list(node,context) map^ {
    let ordered = c.tag(node) == "ol"
    let start = int(c.get(node,"start",1));
    let valid_list = if (start == null or start < 1) raise c.fail("ordered list start must be positive")
    else if (any([for (item in c.kids(node)) c.tag(item) != "li"])) raise c.fail("list children must be li elements")
    let body = c.merge([for (i,item in c.kids(node)) list_item(item,{*:context,depth:context.depth+1,ordered:ordered},start+i)^],
        if (context.type == "typst") ", " else "")^
    let issues = body.diagnostics ++ (if (context.type == "asciidoc" and ordered and start != 1 and context.depth > 0)
        [c.issue("nested-list-start",node,"Nested AsciiDoc start values are outside this writer profile")] else []);
    if (context.type == "typst") c.result((if (ordered) "#enum(start: " ++ string(start) ++ ", " else "#list(") ++ body.text ++ ")\n\n",issues)
    else if (context.type == "man") c.result(".RS\n" ++ body.text ++ ".RE\n",issues)
    else c.result((if (ordered and start != 1) "[start=" ++ string(start) ++ "]\n" else "") ++ body.text ++ "\n",issues)
}
fn table_rows(node) => [for (child in c.kids(node)) if (c.tag(child) == "tr") child
    else if (c.tag(child) in ["thead","tbody","tfoot"]) for (row in c.kids(child) where c.tag(row) == "tr") row]
fn span_count(cell,key) int^ {
    let value = int(c.get(cell,key,1));
    if (value == null or value < 1 or value > 1000) raise c.fail("invalid table " ++ key) else value
}
fn place_cells(cells,index,cursor,blocked) map^ {
    if (index >= len(cells)) {cells:[],occupied:blocked}
    else {
        let node = cells[index]
        let first = grid.free_column(blocked,cursor)
        let end_col = first+span_count(node,"colspan")^ - 1
        let height = span_count(node,"rowspan")^;
        if (not (c.tag(node) in ["td","th"])) raise c.fail("table row children must be td/th elements")
        else if (end_col >= 1024 or any([for (i in range(first,min(end_col+1,len(blocked)),1)) blocked[i] > 0])) raise c.fail("overlapping or excessive table span")
        else {
            let rest = place_cells(cells,index+1,end_col+1,grid.reserve(blocked,first,end_col,height))^;
            {cells:[{node:node,column:first},*rest.cells],occupied:rest.occupied}
        }
    }
}
fn place_rows(rows,index,blocked) map^ {
    if (index >= len(rows)) {
        if (any([for (value in blocked) value > 0])) raise c.fail("row span extends beyond table")
        else {rows:[],columns:len(blocked)}
    } else {
        let row = place_cells(c.kids(rows[index]),0,0,blocked)^
        let rest = place_rows(rows,index+1,grid.advance(row.occupied))^;
        {rows:[row,*rest.rows],columns:max(len(row.occupied),rest.columns)}
    }
}
fn positioned_cells(row,columns,target) => [for (column in range(0,columns,1),
    let starts = [for (cell in row.cells where cell.column == column) cell]
    where len(starts) > 0 or target == "man" or column >= len(row.occupied) or row.occupied[column] == 0)
    if (len(starts) > 0) starts[0] else {node:<td>,column:column}]
fn table_cell(cell,context) map^ {
    let body = children(cell,{*:context,in_cell:true})^
    let issues = body.diagnostics ++ c.unknown(cell,["colspan","rowspan","sourcepos"],"table cell attribute")
    let columns = span_count(cell,"colspan")^
    let rows = span_count(cell,"rowspan")^
    let target = context.type;
    if (target == "typst") c.result("table.cell(x: " ++ string(context.column) ++ ", y: " ++ string(context.row) ++ ", colspan: " ++ string(columns) ++ ", rowspan: " ++ string(rows) ++ ")[" ++
        (if (c.tag(cell) == "th") "#strong[" ++ body.text ++ "]" else body.text) ++ "]",issues)
    else if (target == "asciidoc") c.result((if (columns == 1 and rows == 1) "" else string(columns) ++ "." ++ string(rows) ++ "+") ++
        (if (c.tag(cell) == "th") "h|" else "a|") ++ body.text ++ "\n",issues)
    else c.result("T{\n" ++ (if (c.tag(cell) == "th") "\\f[B]" ++ body.text ++ "\\f[R]" else body.text) ++ "\nT}",issues ++
        if (columns > 1 or rows > 1) [c.issue("table-span",cell,"man table spans are outside this writer profile")] else [])
}
fn table(node,context) map^ {
    let rows = table_rows(node)
    let layout = place_rows(rows,0,[])^
    let columns = max(1,layout.columns)
    let target = context.type
    let values = [for (i,row in layout.rows) c.merge([for (cell in positioned_cells(row,columns,target))
        table_cell(cell.node,{*:context,row:i,column:cell.column})^],if (target == "man") "\t" else if (target == "typst") ", " else "")^]
    let body = c.merge(if (target == "typst") [for (value in values where value.text != "") value] else values,
        if (target == "typst") ",\n" else "\n")^
    let captions = [for (child in c.kids(node) where c.tag(child) == "caption") child]
    let caption = c.merge([for (child in captions) children(child,context)^])^
    let headers = len(rows) > 0 and len(c.kids(rows[0])) > 0 and all([for (cell in c.kids(rows[0])) c.tag(cell) == "th"])
    let attribute_issues = [for (row in rows,issue in c.unknown(row,["sourcepos"],"table row attribute")) issue] ++
        [for (group in c.kids(node),issue in if (c.tag(group) in ["thead","tbody","tfoot","caption"])
            c.unknown(group,["sourcepos"],"table group attribute") else []) issue]
    let all_issues = body.diagnostics ++ caption.diagnostics ++ attribute_issues;
    if (target == "typst") c.result((if (len(captions) > 0) "#figure(" else "") ++ (if (len(captions) > 0) "table(columns: " else "#table(columns: ") ++ string(columns) ++ ",\n" ++ body.text ++ ")" ++
        (if (len(captions) > 0) ", caption: [" ++ caption.text ++ "])" else "") ++ "\n\n",all_issues)
    else if (target == "asciidoc") c.result((if (len(captions) == 0) "" else "." ++ caption.text ++ "\n") ++
        "[cols=\"" ++ string(columns) ++ "*\"" ++ (if (headers) ",options=\"header\"" else "") ++ "]\n|===\n" ++ body.text ++ "\n|===\n\n",all_issues)
    else c.result((if (len(captions) == 0) "" else ".PP\n" ++ caption.text ++ "\n") ++ ".TS\nallbox;\n" ++
        join([for (i in range(0,columns,1)) "l"]," ") ++ ".\n" ++ body.text ++ "\n.TE\n",all_issues)
}
fn definition(node,context) map^ {
    let body = c.merge([for (child in c.kids(node)) {
        let value = children(child,context)^;
        c.result((if (c.tag(child) == "dt") (if (context.type == "man") ".TP\n" else if (context.type == "typst") "/ " else "") else "") ++
            value.text ++ (if (c.tag(child) == "dt") (if (context.type == "man") "\n" else if (context.type == "typst") ": " else ":: ") else "\n"),value.diagnostics)
    }])^;
    c.result(body.text ++ "\n",body.diagnostics)
}
fn metadata(node,context) map^ {
    let target = context.type
    let title = node.title
    let authors = c.sequence(node.author)
    let author_names = [for (author in authors) if (author is string) author else c.get(author,"name","")]
    let known = ["title","author"];
    if (target == "typst") c.result("#set document(" ++ (if (title == null) "" else "title: " ++ quoted(c.text(title)) ++ ", ") ++
        "author: (" ++ join([for (author in author_names) quoted(string(author))],", ") ++ (if (len(author_names) == 1) "," else "") ++ "))\n",
        c.unknown(node,known,"metadata"))
    else if (target == "asciidoc") c.result((if (title == null) "" else "= " ++ escaped(c.text(title),target) ++ "\n") ++
        join([for (author in author_names) c.single_line(author,"author")^],"; ") ++ "\n\n",c.unknown(node,known,"metadata"))
    else c.result("",c.unknown(node,["title"],"metadata"))
}
fn render_node(node,context) map^ {
    let kind = c.tag(node)
    let target = context.type;
    if (node == null) c.result("")
    else if (node is string or node is symbol) c.result(escaped(string(node),target))
    else if (node is array or node is list) c.merge([for (child in node) render(child,context)^])^
    else if (not (node is element)) c.lost("unsupported-value",node,"Markup writer requires Mark Doc elements")^
    else if (kind in ["doc","body","document","html","main","section","article"]) children(node,context)^
    else if (kind == "meta") metadata(node,context)^
    else if (kind == "p") {
        let body = children(node,context)^;
        c.result((if (target == "man" and not context.in_item and not context.in_cell) ".PP\n" else "") ++ body.text ++
            (if (context.in_item or context.in_cell) "\n" else "\n\n"),body.diagnostics)
    }
    else if (kind == "span") children(node,context)^
    else if (kind in ["h1","h2","h3","h4","h5","h6"]) heading(node,context)^
    else if (kind == "code" or kind == "pre") code(node,context)^
    else if (kind in ["strong","b","em","i","cite","u","del","s","sup","sub"]) wrapped(node,context,kind)^
    else if (kind == "a") link(node,context)^
    else if (kind == "img") image(node,context)^
    else if (kind in ["ul","ol"]) render_list(node,context)^
    else if (kind == "dl") definition(node,context)^
    else if (kind == "table") table(node,context)^
    else if (kind == "blockquote") {
        let body = children(node,context)^
        let fence = fence_for(body.text,"_");
        c.result((if (target == "typst") "#quote(block: true)[" else if (target == "man") ".RS\n" else "[quote]\n" ++ fence ++ "\n") ++ body.text ++
            (if (target == "typst") "]\n\n" else if (target == "man") ".RE\n" else fence ++ "\n\n"),body.diagnostics)
    }
    else if (kind == "br") c.result(if (target == "typst") "#linebreak()" else if (target == "man") "\n.br\n" else " +\n")
    else if (kind == "hr") c.result(if (target == "typst") "#line(length: 100%)\n\n" else if (target == "man") ".sp\n\\l'3i'\n.sp\n" else "'''\n\n")
    else if (kind in ["figure","div"]) {
        let body = children(node,{*:context,image_block:kind == "figure"})^;
        c.result(body.text,body.diagnostics ++ if (kind == "figure") [c.issue("figure-layout",node,"Figure grouping is outside this writer profile")] else [])
    }
    else if (kind == "raw" and node.format in [target,if (target == "man") "troff" else target]) c.result(c.text(node))
    else {
        let body = children(node,context)^;
        c.result(body.text,body.diagnostics ++ [c.issue(if (kind == "raw") "raw-format-mismatch" else "unsupported-element",node,"Target cannot represent " ++ kind)])
    }
}
pub fn render(node,context) map^ {
    let valid_depth = if (context.nesting > 128) raise c.fail("document nesting exceeds 128")
    let next = {*:context,nesting:context.nesting+1}
    let value = render_node(node,next)^
    let kind = c.tag(node)
    let task_keys = ["class","data-checked","data-task-state","data-task-label","data-task-progress-min","data-task-progress-max","data-task-marker","data-task-flavor"]
    let allowed = ["sourcepos"] ++ (if (kind == "doc") ["version"] else if (kind == "meta") ["title","author"]
        else if (kind in ["h1","h2","h3","h4","h5","h6"]) ["level"]
        else if (kind == "code") ["type","language"] else if (kind == "a") ["href"] else if (kind == "img") ["src","alt"]
        else if (kind == "ol") ["start"] else if (kind in ["td","th"]) ["colspan","rowspan"]
        else if (kind == "li") task_keys else if (kind == "raw") ["format","data-kind","data-diagnostic"] else [])
    let anchor_supported = (context.type == "asciidoc" and kind in ["p","h1","h2","h3","h4","h5","h6","ul","ol","dl","table","blockquote","code","div","figure"]) or
        (context.type == "typst" and kind in ["h1","h2","h3","h4","h5","h6","table"])
    let attribute_issues = if (node is element and kind != "meta") c.unknown(node,allowed ++ (if (anchor_supported) ["id"] else []),"attribute") else []
    let identifier = if (node is element) node.id else null
    let safe_id = if (identifier == null or not anchor_supported) null else c.token(identifier,"anchor identifier","_.:")^
    let anchor = if (safe_id == null) "" else if (context.type == "asciidoc") "[[" ++ safe_id ++ "]]\n" else " <" ++ safe_id ++ ">"
    let ending = if (ends_with(value.text,"\n\n")) 2 else if (ends_with(value.text,"\n")) 1 else 0
    let output = if (context.type == "asciidoc") anchor ++ value.text else
        slice(value.text,0,len(value.text)-ending) ++ anchor ++ c.times("\n",ending);
    c.result(output,value.diagnostics ++ attribute_issues)
}
pub fn render_document(node,options) map^ {
    let value = render(node,{*:options,depth:0,nesting:0,font:"R",in_item:false,in_cell:false})^;
    if (options.type != "man") value
    else {
        let metas = [for (child in c.kids(node) where c.tag(child) == "meta") child]
        let title = c.get(options,"title",c.get(metas[0],"title","UNTITLED"))
        let args = [title,c.get(options,"section","1"),c.get(options,"date",""),c.get(options,"source",""),c.get(options,"manual","")]
        let safe = [for (arg in args) replace(replace(c.single_line(if (arg is element) c.text(arg) else string(arg),"man title argument")^,"\\","\\e"),"\"","\\(dq")];
        c.result(".TH " ++ join([for (arg in safe) "\"" ++ arg ++ "\""]," ") ++ "\n" ++ value.text,value.diagnostics)
    }
}
