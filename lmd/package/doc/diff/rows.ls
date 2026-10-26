// Byte spans remain authoritative; only requested rows become display strings (S2.5.8).
import math
import c: .common
import search: lambda.io.search

pub fn index(change) map {
    let counts = [for (h in change.hunks) len(h.lines) + 1]
    let ends = math.cumsum(counts);
    {starts:[0,*take(ends,max(0,len(ends) - 1))],count:if (len(ends) == 0) 1 else ends[len(ends) - 1]}
}
pub fn file_name(change) => c.get(change,"new_name",c.get(change,"old_name","Untitled"))
pub fn summary(change) string {
    if (change.kind == "unmerged") "Unmerged path — resolve the conflict in Git to review a two-way patch."
    else if (change.binary == true) "Binary files differ."
    else if (change.submodule == true) "Submodule pointer changed."
    else if (change.kind == "renamed" or change.kind == "copied") change.old_name ++ " → " ++ change.new_name
    else if (change.old_mode != change.new_mode) "Mode: " ++ string(change.old_mode) ++ " → " ++ string(change.new_mode)
    else "No textual changes."
}
pub fn at(document,change,projection,row) map^ {
    if (len(change.hunks) == 0) {row:row,kind:"summary",text:summary(change)}
    else {
        let h = search.lower_bound(projection.starts,row + 1) - 1
        let own = row - projection.starts[h]
        let block = change.hunks[h];
        if (own == 0) {row:row,kind:"hunk",text:block.header,hunk:h}
        else {
            let line = block.lines[own - 1];
            {*:line,row:row,hunk:h,text:c.display(slice(document.source,line.start,line.end))^}
        }
    }
}
pub fn window(document,change,projection,top,size) array^ =>
    [for (row in max(0,top) to min(projection.count,top + size) - 1) at(document,change,projection,row)^]
pub fn hunk_step(projection,top,direction) int {
    let candidates = [for (position in projection.starts where if (direction > 0) position > top else position < top) position];
    if (len(candidates) == 0) top else if (direction > 0) min(candidates) else max(candidates)
}
pub fn search(document,change,projection,query) array^ {
    if (query == "") [] else [for (h,block in change.hunks,i,line in block.lines
        where contains(lower(c.display(slice(document.source,line.start,line.end))^),lower(query)))
        projection.starts[h] + i + 1]
}
pub fn point(row,column) => {row:row,column:column}
pub fn compare_points(a,b) => if (a.row != b.row) sign(a.row - b.row) else sign(a.column - b.column)
pub fn selection(anchor,head) => {anchor:anchor,head:head}
pub fn selected_all(document,change,projection) map^ {
    let finish = projection.count - 1
    let row = at(document,change,projection,finish)^;
    selection(point(0,0),point(finish,len(row.text)))
}
pub fn copy_text(document,change,projection,side = "new",selected = null) string^ {
    let valid = c.require(side in ["old","new","unified"],"unknown copy side")^
    let first = if (selected == null) point(0,0) else if (compare_points(selected.anchor,selected.head) <= 0) selected.anchor else selected.head
    let finish = if (selected == null) point(projection.count,0) else if (compare_points(selected.anchor,selected.head) <= 0) selected.head else selected.anchor
    let parts = [for (h,block in change.hunks,i,line in block.lines)
        for (row in [projection.starts[h] + i + 1] where row >= first.row and row <= finish.row and
            (side == "unified" or (if (side == "old") line.old_line != null else line.new_line != null))) {
            let text = c.display(slice(document.source,line.start,line.end))^
            let from = if (row == first.row) first.column else 0
            let end = if (row == finish.row) finish.column else len(text)
            let newline = (selected == null or row < finish.row) and
                not (if (side == "old") line.old_no_newline else if (side == "new") line.new_no_newline
                    else line.old_no_newline or line.new_no_newline);
            slice(text,from,end) ++ (if (newline) "\n" else "")
        }];
    join(parts,"")
}
