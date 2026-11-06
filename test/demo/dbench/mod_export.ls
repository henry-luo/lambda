// S12.1.3v2: export uses retained logical cells, independently of display formatting.
import model: .mod_model

pub let MAX_CELLS=100000
pub let MAX_BYTES=16*1024*1024

pub fn data(st,scope) map^ {
    if (not (scope in ["loaded","selection"]) or len(st.rows)==0 or len(st.columns)==0)
        raise error("DBench: no rows to export")
    else {
        let bounds=if (scope=="selection") model.bounds(st) else
            {top:0,bottom:len(st.rows)-1,left:0,right:len(st.columns)-1}
        let count=(bounds.bottom-bounds.top+1)*(bounds.right-bounds.left+1)
        if (count>MAX_CELLS) raise error("DBench: export exceeds the cell limit")
        else {
            let columns=[for (c in bounds.left to bounds.right) st.columns[c]]
            let rows=[for (r in bounds.top to bounds.bottom) [for (c in bounds.left to bounds.right) st.rows[r][c]]];
            {columns:columns,rows:rows}
        }
    }
}

fn csv_text(text) string^ => "\"" ++ replace(text,"\"","\"\"")^ ++ "\""
fn csv_value(value) string^ {
    if (value==null) ""
    else if (value is binary) {
        let encoded=format(value,'json')^;
        csv_text("base64:" ++ slice(encoded,1,len(encoded)-1)^)^
    } else if (value is string) csv_text(value)^
    else string(value)^
}

// The stream uses the same positional descriptors and cell encoding as the
// bounded export; callers write each row before requesting another batch.
pub fn stream_header(columns,kind) string^ => if (kind=="json")
    "{\"columns\":" ++ (format(columns,'json')^) ++ ",\"rows\":["
    else (join([for (column in columns) csv_text(column[0])^],",")^) ++ "\r\n"
pub fn stream_row(row,kind,first) string^ => if (kind=="json")
    (if (first) "" else ",") ++ (format(row,'json')^)
    else (join([for (value in row) csv_value(value)^],",")^) ++ "\r\n"
pub fn stream_footer(kind) => if (kind=="json") "]}" else ""
pub fn encode(columns,rows,kind) string^ {
    let descriptor_bytes=sum([for (column in columns,field in column) if (field is string) len(field)*4 else 16])
    if (not (kind in ["csv","json"])) raise error("DBench: unsupported export format")
    // JSON escaping may expand text; reject before constructing a large string.
    else if (2*model.result_size(rows)+2*descriptor_bytes+16*len(rows)*len(columns)>MAX_BYTES)
        raise error("DBench: export exceeds the size limit")
    else {
        let content=if (kind=="json") format({columns:columns,rows:rows},'json')^ else
            join([join([for (column in columns) csv_text(column[0])^],",")^,
                *[for (row in rows) join([for (value in row) csv_value(value)^],",")^]],"\r\n")^ ++ "\r\n"
        if (4*len(content)>MAX_BYTES) raise error("DBench: export exceeds the size limit") else content
    }
}
