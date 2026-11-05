// S12.1.3v2: providers are pure; the caller owns selection and editing policy.
// Both axes are windowed before either provider is called. The row provider
// supplies one leading header cell before the generated cells.
pub fn body(rows, columns, render_row, render_cell) any^ {
    let left=columns.before>0
    let right=columns.after>0
    let span=1+(columns.last-columns.first)+(if (left) 1 else 0)+(if (right) 1 else 0);
    <tbody
        if (rows.before>0) <tr class:"grid-spacer",'aria-hidden':"true",<td colspan:string(span),style:"height:" ++ string(rows.before) ++ "px">> else null;
        if (rows.first<rows.last) for (r in rows.first to (rows.last-1))
            render_row(r,[
                if (left) <td class:"grid-gap",'aria-hidden':"true"> else null,
                *[if (columns.first<columns.last) for (c in columns.first to (columns.last-1)) render_cell(r,c) else null],
                if (right) <td class:"grid-gap",'aria-hidden':"true"> else null]) else null;
        if (rows.after>0) <tr class:"grid-spacer",'aria-hidden':"true",<td colspan:string(span),style:"height:" ++ string(rows.after) ++ "px">> else null>
}
