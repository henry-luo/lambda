// Static tags follow S2.4.3v3; one constructor registry prevents source-controlled HTML.
import c: .common
import html: lambda.latex.to_html
import presentation: .presentation

let tags = {
    p:(a,k) => <p *:a,*k>, h1:(a,k) => <h1 *:a,*k>, h2:(a,k) => <h2 *:a,*k>,
    h3:(a,k) => <h3 *:a,*k>, h4:(a,k) => <h4 *:a,*k>, h5:(a,k) => <h5 *:a,*k>,
    h6:(a,k) => <h6 *:a,*k>, blockquote:(a,k) => <blockquote *:a,*k>, pre:(a,k) => <pre *:a,*k>,
    strong:(a,k) => <strong *:a,*k>, em:(a,k) => <em *:a,*k>, u:(a,k) => <u *:a,*k>,
    s:(a,k) => <s *:a,*k>, sup:(a,k) => <sup *:a,*k>, sub:(a,k) => <sub *:a,*k>,
    ol:(a,k) => <ol *:a,*k>, ul:(a,k) => <ul *:a,*k>, th:(a,k) => <th *:a,*k>, td:(a,k) => <td *:a,*k>
}
let marks = [{key:"b",tag:"strong"},{key:"i",tag:"em"},{key:"u",tag:"u"},{key:"strike",tag:"s"}]
fn make(tag, attributes, children) element^ => tags[tag](attributes,children)^
fn wrap(names, index, value) any^ => if (index >= len(names)) value else
    make(names[index],{},[wrap(names,index+1,value)^])^
fn graphic(node, value) => if (node.placement == null) value else
    <span class:"docx-anchor",style:node.placement.box,value>
fn inline_node(node, options) any^ {
    if (node.kind == "text") {
        let names = [for (entry in marks where node.marks[entry.key] == true or
            (entry.key == "strike" and node.marks.dstrike == true)) entry.tag] ++
            (if (node.marks.vertAlign == "superscript") ["sup"] else if (node.marks.vertAlign == "subscript") ["sub"] else []);
        let style = if (options.presentation == "source") presentation.run_style(node.marks) else "";
        if (style == "") wrap(names,0,node.text)^
        else <span style:style,wrap(names,0,node.text)^>
    } else if (node.kind == "link") <a href:node.href,*inline_nodes(node.children,options)^>
    else if (node.kind == "bookmark") <span id:node.id>
    // HTML attributes are text at the DOM boundary; numeric values stay in the private model.
    else if (node.kind == "image") graphic(node,<img src:node.src,alt:node.alt,
        *:if (node.placement == null) {} else {style:node.placement.image},
        width:if (node.width == null) null else string(node.width),
        height:if (node.height == null) null else string(node.height)>)
    else if (node.kind == "chart") graphic(node,<span class:"docx-chart",node.svg>)
    else if (node.kind == "page-break") <br class:"docx-page-break">
    else <br>
}
fn inline_nodes(nodes, options) array^ => [for (node in nodes) inline_node(node,options)^]
fn paragraph(node, options) array^ {
    let empty = all([for (child in node.children) child.kind == "text" and child.text == ""])
    let style = (if (options.presentation == "source")
        presentation.paragraph_style(node.props,if (empty) node.empty_run else node.run,node.list) else "") ++
        (if (node.list.format == "none") "text-indent:" ++ string(0.0-node.list.hanging) ++ "px;" else "");
    [if (node.break_before) <hr class:"docx-page-break"> else null,
        if (not empty or options.include_empty_paragraphs) make(node.tag,
            {*:node.attrs,*:presentation.attributes(style)},
            if (empty and options.presentation == "source") [<br>] else inline_nodes(node.children,options)^)^ else null,
        if (node.section_after) <hr class:"docx-section-break"> else null]
}
fn advance(info, counters) {
    let key = info.id ++ ":" ++ string(info.level)
    let prior = counters[key]
    let cleared = map([for (counter_key,value in counters)
        (counter_key,if (value.id == info.id and value.level > info.level and value.restart > info.level) null else value)])
    let ordinal = if (prior == null) info.start else prior.value+1;
    {counters:{*:cleared,[key]:{*:info,value:ordinal}},ordinal:ordinal}
}
fn nested_lists(blocks, index, level, counters, options, parent_indent) map^ {
    if (index >= len(blocks) or blocks[index].list == null or blocks[index].list.level <= level)
        {nodes:[],next:index,counters:counters,diagnostics:[]}
    else {
        let group = render_list(blocks,index,counters,options,parent_indent)^
        let rest = nested_lists(blocks,group.next,level,group.counters,options,parent_indent)^;
        {nodes:[group.node,*rest.nodes],next:rest.next,counters:rest.counters,
            diagnostics:(if (blocks[index].list.level > level+1)
                [c.issue("list-level-jump",null,"Skipped list levels are represented by one nested list")] else []) ++
                group.diagnostics ++ rest.diagnostics}
    }
}
fn list_items(blocks, index, info, counters, options) map^ {
    if (index >= len(blocks) or blocks[index].list == null or blocks[index].list.id != info.id or
        blocks[index].list.level != info.level) {nodes:[],next:index,counters:counters,diagnostics:[]}
    else {
        let stepped = advance(blocks[index].list,counters)
        let children = paragraph(blocks[index],options)^
        let nested = nested_lists(blocks,index+1,info.level,stepped.counters,options,info.indent)^
        let rest = list_items(blocks,nested.next,info,nested.counters,options)^;
        {nodes:[<li *(children ++ nested.nodes)>,*rest.nodes],next:rest.next,counters:rest.counters,
            diagnostics:nested.diagnostics ++ rest.diagnostics}
    }
}
fn render_list(blocks, index, counters, options, parent_indent = 0.0) map^ {
    let info = blocks[index].list
    let ordinal_start = advance(info,counters).ordinal
    let items = list_items(blocks,index,info,counters,options)^
    let kind = if (info.format == "bullet" or info.format == "none") "ul" else "ol"
    let type_code = {lowerLetter:"a",upperLetter:"A",lowerRoman:"i",upperRoman:"I"}[info.format]
    let attrs = if (info.format == "none") {class:"docx-list-none",
        style:"list-style-type:none;padding-left:" ++ string(max(0.0,info.indent-parent_indent)) ++ "px;"}
        else if (kind == "ul") {} else {*:if (ordinal_start == 1) {} else {start:string(ordinal_start)},
        *:if (type_code == null) {} else {type:type_code}};
    {node:make(kind,attrs,items.nodes)^,next:items.next,counters:items.counters,diagnostics:items.diagnostics}
}
fn row_span(rows, index, cell) {
    if (index >= len(rows)) 0 else {
        let next = c.first([for (value in rows[index].cells where value.column == cell.column) value]);
        if (next == null or next.merge != "continue" or next.span != cell.span) 0 else 1+row_span(rows,index+1,cell)
    }
}
fn cells(rows, row_index, index, counters, options, table) map^ {
    let row = rows[row_index];
    if (index >= len(row.cells)) {nodes:[],counters:counters,diagnostics:[]}
    else {
        let cell = row.cells[index]
        let covered = cell.merge == "continue" and any([for (i,prior in rows,
            origin in prior.cells where i < row_index and origin.column == cell.column and origin.span == cell.span and origin.merge == "restart")
            i+row_span(rows,i+1,origin) >= row_index])
        let contents = if (covered) {nodes:[],counters:counters,diagnostics:[]} else render_blocks(cell.blocks,0,counters,options)^
        let rest = cells(rows,row_index,index+1,contents.counters,options,table)^
        let span = if (cell.merge == "restart") 1+row_span(rows,row_index+1,cell) else 1
        let attrs = {*:if (options.presentation == "source")
                presentation.attributes(presentation.cell_style(table.props,cell.props,
                    row_index,len(rows),span,cell.column,len(table.grid),cell.span)) else {},
            *:if (cell.span > 1) {colspan:string(cell.span)} else {},
            *:if (span > 1) {rowspan:string(span)} else {},*:if (row.header) {scope:"col"} else {}}
        let merge_issues = if (cell.merge == "continue" and not covered)
            [c.issue("unmatched-table-merge",null,"Vertical continuation has no matching origin")]
            else if (covered and any([for (block in cell.blocks) block.kind != "paragraph" or
                any([for (n in block.children) n.kind != "text" or n.text != ""])]))
                [c.issue("merged-cell-content",null,"Covered table cell has additional source content")] else [];
        {nodes:[if (not covered) make(if (row.header) "th" else "td",attrs,contents.nodes)^ else null, *rest.nodes],
            counters:rest.counters,diagnostics:merge_issues ++ contents.diagnostics ++ rest.diagnostics}
    }
}
fn table_rows(rows, index, counters, options, table) map^ {
    if (index >= len(rows)) {nodes:[],counters:counters,diagnostics:[]} else {
        let row = cells(rows,index,0,counters,options,table)^
        let rest = table_rows(rows,index+1,row.counters,options,table)^;
        {nodes:[<tr *:if (options.presentation == "source" and rows[index].height > 0)
            {style:"height:" ++ string(rows[index].height) ++ "px;"} else {},
            *row.nodes>,*rest.nodes],counters:rest.counters,diagnostics:row.diagnostics ++ rest.diagnostics}
    }
}
fn render_blocks(blocks, index, counters, options) map^ {
    if (index >= len(blocks)) {nodes:[],counters:counters,diagnostics:[]} else {
        let node = blocks[index]
        let current = if (node.list != null) {
            let group = render_list(blocks,index,counters,options)^;
            {nodes:[group.node],counters:group.counters,next:group.next,diagnostics:group.diagnostics}
        } else if (node.kind == "table") {
            let rows = table_rows(node.rows,0,counters,options,node)^
            let columns = [if (options.presentation == "source" and len(node.grid) > 0)
                <colgroup *[for (width in node.grid) <col style:("width:" ++ string(width) ++ "px;")>]> else null];
            {nodes:[<table *:if (options.presentation == "source")
                presentation.attributes(presentation.table_style(node.props,node.grid)) else {},
                *(columns ++ [<tbody *rows.nodes>])>],counters:rows.counters,next:index+1,diagnostics:rows.diagnostics}
        } else {nodes:paragraph(node,options)^,counters:counters,next:index+1,diagnostics:[]}
        let rest = render_blocks(blocks,current.next,current.counters,options)^;
        {nodes:current.nodes ++ rest.nodes,counters:rest.counters,diagnostics:current.diagnostics ++ rest.diagnostics}
    }
}
fn inline_text(nodes) => join([for (node in nodes)
    if (node.kind == "text") node.text else if (node.kind == "link") inline_text(node.children)
    else if (node.kind == "chart") node.text
    else if (contains(["break","page-break"],node.kind)) "\n" else ""],"")
fn plain_text(blocks, options) => join([for (node in blocks)
    if (node.kind == "paragraph") {
        let text = inline_text(node.children);
        if (text == "" and not options.include_empty_paragraphs) "" else text ++ "\n\n"
    } else join([for (row in node.rows) join([for (cell in row.cells where cell.merge != "continue")
        (let text = plain_text(cell.blocks,options), if (ends_with(text,"\n\n")) slice(text,0,len(text)-2) else text)],"\t") ++ "\n"],"")],"")
pub fn render(model, options) map^ {
    let blocks = render_blocks(model.blocks,0,{},options)^
    let document = <article class:"docx-document",*:if (options.presentation == "source")
        presentation.attributes(model.page_style) else {},*blocks.nodes>;
    {document:document,html:html.to_html(document),text:plain_text(model.blocks,options),diagnostics:blocks.diagnostics}
}
