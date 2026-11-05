// S12.1.3v2: viewport geometry is pure; applications own measurement and IO.
import search: lambda.io.search

// Store only sizes that differ from the default, with prefix deltas for lookup.
pub fn axis(item_count, default_size, overrides = []) {
    let entries = sort([for (entry in overrides where entry.index >= 0 and entry.index < item_count and entry.size != default_size)
        entry], {by:(entry) => entry.index})
    let indexes = [for (entry in entries) entry.index]
    let deltas = [for (entry in entries) entry.size - default_size];
    {count:item_count, default_size:default_size, indexes:indexes,
        deltas:if (len(deltas) == 0) [] else math.cumsum(deltas)}
}

// The coordinate before item `index`; binary search avoids visiting empty rows.
pub fn offset(axis, index) any^ {
    let position = max(0, min(axis.count, index))
    let at = search.lower_bound(axis.indexes, position)^;
    position * axis.default_size + (if (at == 0) 0 else axis.deltas[at - 1])
}

pub fn extent(axis) any^ => offset(axis, axis.count)^

// First item whose end is strictly after the given coordinate.
fn after(axis, point, lo, hi) int^ {
    if (lo >= hi) lo else {
        let mid = (lo + hi) div 2;
        if (offset(axis, mid + 1)^ <= point) after(axis, point, mid + 1, hi)^
        else after(axis, point, lo, mid)^
    }
}

// First item whose start is at or beyond the exclusive viewport end.
fn at_or_after(axis, point, lo, hi) int^ {
    if (lo >= hi) lo else {
        let mid = (lo + hi) div 2;
        if (offset(axis, mid)^ < point) at_or_after(axis, point, mid + 1, hi)^
        else at_or_after(axis, point, lo, mid)^
    }
}

// `last` is exclusive. A hidden viewport produces no cells, including overscan.
pub fn window(axis, scroll_pos, length, overscan = 0) map^ {
    if (length <= 0 or axis.count == 0) {first:0,last:0,before:0,after:extent(axis)^}
    else {
        let first = max(0, after(axis, max(0, scroll_pos), 0, axis.count)^ - max(0, overscan))
        let end = min(axis.count, at_or_after(axis, max(0, scroll_pos) + length, 0, axis.count)^ + max(0, overscan));
        {first:first,last:end,before:offset(axis, first)^,after:extent(axis)^ - offset(axis, end)^}
    }
}

// Keep a rendered window while it still covers the unpadded viewport.
// A hidden viewport must release its cells even if its empty range is contained.
pub fn covers(rendered, visible) => if (visible.first==visible.last) rendered.first==rendered.last
    else rendered.first<=visible.first and rendered.last>=visible.last

// Minimal scroll offset that brings one logical item into the visible region.
pub fn reveal(axis, index, scroll_pos, length) any^ {
    let first = offset(axis, index)^
    let end = offset(axis, index + 1)^;
    if (first < scroll_pos) first else if (end > scroll_pos + length) max(0, end - length) else scroll_pos
}

// Selection identity stays in logical coordinates when cells are unmounted.
pub fn bounds(selection) => {top:min(selection.row,selection.anchor.row),bottom:max(selection.row,selection.anchor.row),
    left:min(selection.col,selection.anchor.col),right:max(selection.col,selection.anchor.col)}
// Renderers can reuse bounds across all cells in a viewport.
pub fn contains(b,row,column) => row>=b.top and row<=b.bottom and column>=b.left and column<=b.right
pub fn selected(selection,row,column) => contains(bounds(selection),row,column)

// Coordinates are relative to the data region, after accounting for headers.
pub fn hit(rows, columns, x, y) any^ {
    if (rows.count==0 or columns.count==0) null else {
        let row=window(rows,max(0,y),1)^.first
        let column=window(columns,max(0,x),1)^.first;
        {row:min(rows.count-1,row),col:min(columns.count-1,column)}
    }
}
