// CALS row occupancy is computed before HTML emission; row spans reserve their columns.
import c: .common
import h: .html
import grid: lambda.doc.misc.grid

fn nonnegative_int(value,fallback,description) int^ {
    if (value == null) fallback else {
        let lexical = trim(value)
        let digits = if (starts_with(lexical,"+")) slice(lexical,1,len(lexical)) else lexical;
        if (digits == "" or not all([for (ch in split(digits,"")) ord(ch) >= 48 and ord(ch) <= 57]))
            raise c.fail("invalid-table","Invalid " ++ description)
        else int(digits) ^ { raise c.fail("invalid-table","Invalid " ++ description) }
    }
}
fn column_specs(nodes,index,next,result,total) map^ {
    if (index >= len(nodes)) result else {
        let node = nodes[index]
        let col = nonnegative_int(c.attr(node,"colnum"),next,"colnum")^
        let key = c.attr(node,"colname");
        if (col < next or col > total or (key != null and result[key] != null))
            raise c.fail("invalid-table","Duplicate or out-of-order CALS column")
        else column_specs(nodes,index+1,col+1,
            if (key == null) result else {*:result,[key]:col-1},total)^
    }
}
fn empty_cells(blocked,begin_col,end,header) array^ => [for (i in begin_col to (end-1) where blocked[i] == 0)
    h.make(if (header) "th" else "td",{},[])^]

fn cells(entries,index,cursor,blocked,specs,spans,header,context,renderer) map^ {
    if (index >= len(entries)) {nodes:empty_cells(blocked,cursor,len(blocked),header)^,occupied:blocked}
    else {
        let entry = entries[index]
        let span_name = c.attr(entry,"spanname")
        let span = if (span_name == null) null else spans[span_name]
        let first_name = c.get(span,"begin_col",c.get(entry.attrs,"#namest",c.attr(entry,"colname")))
        let last_name = c.get(span,"end",c.attr(entry,"nameend"))
        let first = if (first_name == null) grid.free_column(blocked,cursor) else specs[first_name]
        let end_col = if (last_name == null) first else specs[last_name]
        let height = nonnegative_int(c.attr(entry,"morerows"),0,"morerows")^+1;
        if ((span_name != null and span == null) or first == null or end_col == null or first < cursor or
            end_col < first or end_col >= len(blocked) or any([for (i in first to end_col) blocked[i] > 0]))
            raise c.fail("invalid-table","Unknown column/span, overlapping cell or excessive CALS cell count")
        else {
            let occupied = grid.reserve(blocked,first,end_col,height)
            let attrs = {*:h.attributes(entry),*:if (end_col > first) {colspan:string(end_col-first+1)} else {},
                *:if (height > 1) {rowspan:string(height)} else {},
                *:if (c.attr(entry,"align") == null) {} else {style:"text-align:" ++ c.attr(entry,"align") ++ ";"}}
            let cell = h.make(if (header) "th" else "td",attrs,renderer(entry.children,context)^)^
            let rest = cells(entries,index+1,end_col+1,occupied,specs,spans,header,context,renderer)^;
            {nodes:[*empty_cells(blocked,cursor,first,header)^,cell,*rest.nodes],occupied:rest.occupied}
        }
    }
}
fn rows(entries,index,blocked,specs,spans,header,context,renderer) array^ {
    if (index >= len(entries)) {
        if (any([for (value in blocked) value > 0])) raise c.fail("invalid-table","CALS row span exceeds its row group")
        else []
    } else {
        let row = entries[index]
        let rendered = cells(c.children(row,"entry"),0,0,blocked,specs,spans,header,context,renderer)^
        let rest = rows(entries,index+1,grid.advance(rendered.occupied),
            specs,spans,header,context,renderer)^;
        [<tr *:h.attributes(row),*rendered.nodes>,*rest]
    }
}
pub fn cals(group,context,renderer) element^ {
    let total = nonnegative_int(c.attr(group,"cols"),0,"cols")^;
    if (total < 1 or total > 1024) raise c.fail("invalid-table","CALS cols must be between 1 and 1024")
    else {
        let specs = column_specs(c.children(group,"colspec"),0,1,{},total)^
        let declarations = c.children(group,"spanspec")
        let invalid = any([for (i,span in declarations,let key = c.attr(span,"spanname"))
            key == null or key == "" or specs[c.attr(span,"namest")] == null or specs[c.attr(span,"nameend")] == null or
            any([for (j,prior in declarations where j < i) c.attr(prior,"spanname") == key])]);
        if (invalid) raise c.fail("invalid-table","Invalid or duplicate CALS spanspec")
        else {
        let spans = c.dictionary([for (span in declarations) {key:c.attr(span,"spanname"),
            value:{begin_col:c.attr(span,"namest"),end:c.attr(span,"nameend")}}])
        let bodies = [for (section in c.children(group) where section.tag in ["thead","tbody","tfoot"])
            h.make(section.tag,h.attributes(section),rows(c.children(section,"row"),0,
                [for (i in 1 to total) 0],specs,spans,section.tag == "thead",context,renderer)^)^];
        <table *bodies>
        }
    }
}

pub fn html(node,context,renderer) element^ {
    let attributes = {*:h.attributes(node),*:map([for (key in ["colspan","rowspan","scope","headers","abbr","width","height"]
        where c.attr(node,key) != null) (key,c.attr(node,key))])};
    h.make(node.tag,attributes,renderer(node.children,context)^)^
}
