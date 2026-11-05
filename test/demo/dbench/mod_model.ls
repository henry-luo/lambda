// S1.4v2/S12.1.3v2: result generations and viewport state are explicit values.
import sql: .mod_sql
import grid: lambda.ui.core.virtual_grid
import profiles: .mod_profiles

pub let MAX_ROWS=2048
pub let MAX_BYTES=16*1024*1024
pub fn initial(sample) => {engine:"sqlite",target:sample,sqlite_path:null,tls:"verify-full",connected:false,connecting:false,generation:0,token:0,
    schema:[],details:[],show_details:false,object_filter:"",table:null,mode:"table",columns:[],column_widths:{},column_key:null,column_axis:null,
    rows:[],bytes:0,offset:0,done:true,loading:false,page_cursors:{},keyset:false,
    spooled:false,total_rows:0,row_base:0,window_token:0,window_pending:null,copy_token:0,copy_ready:null,
    show_editor:false,edit_mode:"text",edit_value:"",edit_base:null,show_insert:false,insert_text:"{}",insert_base:null,
    pending:[],pending_bytes:0,applying:false,show_write:false,write_preparing:false,write_plan:null,
    show_export:false,exporting:false,export_path:"",export_kind:"csv",export_scope:"loaded",stream_available:false,show_value:false,
    show_profile:false,profile_busy:false,profile_path:"",profile_form:profiles.form(),
    sidebar_width:270,editor_height:62,resize_drag:null,
    sort_column:null,sort_direction:"ASC",filter_column:null,filter_value:"",filter_input_column:null,filter_input_value:"",
    key_ordered:false,query:"SELECT * FROM customers",params:"[]",sql_range:null,
    history:[],show_history:false,
    row:0,col:0,anchor:{row:0,col:0},selection_drag:null,selection_click:false,
    viewport:{x:0.0,y:0.0,width:0.0,height:0.0},status:"Ready",error:null}

pub fn objects(schema) => sort(unique([for (row in schema) row[0]]))
pub fn table_columns(schema,table) => [for (row in schema where row[0]==table and row[2]!=null)
    {name:row[2],declared:row[3],nullable:row[4],primary:row[5]}]
pub fn table_kind(schema,table) => if (any([for (row in schema where row[0]==table) row[1]])) "view" else "table"
pub fn remember(history,query) => if (len(query)>65536) history else take([query,*[for (item in history where item!=query) item]],20)

pub fn primary_keys(schema,table) any^ => [for (column in sort([for (column in table_columns(schema,table) where column.primary>0) column],{by:(c) => c.primary})^) column.name]
pub fn table_request(st,table,offset=0,cursor=null,whole=false) map^ {
    let columns=table_columns(st.schema,table)
    let names=[for (column in columns) column.name]
    let keys=primary_keys(st.schema,table)^
    let ordering=if (st.sort_column!=null and st.sort_column in names)
        [{column:st.sort_column,direction:st.sort_direction}] else []
    let filters=if (st.filter_column!=null and st.filter_column in names and st.filter_value!="")
        [{column:st.filter_column,op:"=",value:st.filter_value}] else [];
    sql.table_page(st.engine,table,names,filters,ordering,keys,offset,sql.PAGE_SIZE,null,cursor,whole)^
}

// Nullable explicit sorts use offset paging because engines order NULL differently.
pub fn next_cursor(st) any^ {
    let keys=primary_keys(st.schema,st.table)^
    let names=[for (column in st.columns) column[0]]
    let fields=table_columns(st.schema,st.table)
    let sorted=[for (field in fields where field.name==st.sort_column) field]
    let ordering=if (st.sort_column==null) [] else [{column:st.sort_column,direction:st.sort_direction}]
    let ordered=sql.ordered_columns(ordering,keys)
    if (len(st.rows)==0 or len(keys)==0 or len(ordered)>32 or
            (st.sort_column!=null and (len(sorted)!=1 or sorted[0].nullable)) or
            any([for (item in ordered) not (item.column in names)])) null
    else {
        let final_row=st.rows[len(st.rows)-1]
        let values=[for (item in ordered) final_row[([for (i,name in names where name==item.column) i])[0]]];
        if (any([for (value in values) not sql.cursor_value(value)^])) null else values
    }
}
// Retain a bounded history; older pages remain reachable with offset paging.
pub fn remember_cursor(st,offset,cursor) map^ => map([*[for (key,value in st.page_cursors
    where (int(string(key))>=max(0,offset-64*sql.PAGE_SIZE) and int(string(key))<offset)^) *[key,value]],string(offset),cursor])^

fn value_size(value) => if (value is string) len(value)*4 else if (value is binary) max(24,len(value)) else 16
pub fn result_size(rows) => sum([for (row in rows,value in row) value_size(value)])
// Query results can outlive the moving in-memory window without changing indices.
pub fn row_count(st) int^ => if (st.mode=="query" and st.spooled) st.total_rows else len(st.rows)^
pub fn row_cached(st,row) bool^ => if (st.mode=="query" and st.spooled)
    row>=st.row_base and row<st.row_base+len(st.rows)^ else row>=0 and row<len(st.rows)^
pub fn row_value(st,row) any^ => if (row_cached(st,row)^) st.rows[row-(if (st.mode=="query" and st.spooled) st.row_base else 0)] else null
pub fn bounds(st) => grid.bounds(st)
pub fn selected(st,row,col) => grid.selected(st,row,col)
pub fn select(st,row,col,extend=false) map^ {
    let r=max(0,min(row_count(st)^-1,row))
    let c=max(0,min(len(st.columns)-1,col));
    {*:st,row:r,col:c,anchor:if (extend) st.anchor else {row:r,col:c}}
}
fn clipboard_value(value) string^ {
    let raw=if (value==null) "NULL" else if (value is binary) "[binary: " ++ string(len(value))^ ++ " bytes]" else string(value)^;
    if (contains(raw,"\t") or contains(raw,"\n") or contains(raw,"\r") or contains(raw,"\""))
        "\"" ++ replace(raw,"\"","\"\"") ++ "\"" else raw
}
pub fn copy_rows(rows,left,right) string^ {
    let bytes=sum([for (row in rows,c in left to right) value_size(row[c])]);
    if (bytes>1024*1024) raise error("DBench: selection is too large to copy")
    else join([for (row in rows) join([for (c in left to right) clipboard_value(row[c])^],"\t")],"\n")^
}
pub fn copy_text(st) string^ {
    let b=bounds(st)
    let count=(b.bottom-b.top+1)*(b.right-b.left+1);
    if (count>100000) raise error("DBench: selection is too large to copy")
    else if (any([for (r in b.top to b.bottom) not row_cached(st,r)^])) raise error("DBench: selected rows are not cached")
    else copy_rows([for (r in b.top to b.bottom) row_value(st,r)^],b.left,b.right)^
}
pub fn accept_batch(st,message) map^ {
    if (message.generation!=st.generation or message.token!=st.token) st else {
        let bytes=result_size(message.rows)
        let append=message.op=="more"
        let spooled=st.mode=="query" and (st.spooled or message.spooled==true);
        if (spooled) {
            let old=if (append) st.total_rows else 0
            let contiguous=append and st.row_base+len(st.rows)==old
            let merge=contiguous and len(st.rows)+len(message.rows)<=MAX_ROWS and st.bytes+bytes<=MAX_BYTES;
            if (bytes>MAX_BYTES) {*:st,done:true,loading:false,status:"Result cache limit reached",error:"A fetched batch exceeds the cache limit."}
            else {*:st,spooled:true,columns:message.columns,rows:if (merge) [*st.rows,*message.rows] else message.rows,
                row_base:if (merge) st.row_base else old,total_rows:old+len(message.rows),
                bytes:if (merge) st.bytes+bytes else bytes,window_pending:null,offset:0,done:message.done,loading:false,
                status:if (message.done) "Result complete" else "More rows available",error:null}
        } else {
        let total=(if (append) len(st.rows) else 0)+len(message.rows)
        let used=(if (append) st.bytes else 0)+bytes;
        if (total>MAX_ROWS or used>MAX_BYTES) {*:st,done:true,loading:false,status:"Result cache limit reached",error:"Fetch stopped at the cache limit."}
        else {*:st,columns:message.columns,rows:if (append) [*st.rows,*message.rows] else message.rows,
            bytes:used,offset:message.offset,done:message.done,loading:false,
            status:if (message.op=="page") (if (st.keyset) "Keyset page loaded · concurrent changes may affect results"
                else if (st.key_ordered) "Table page loaded · pages can shift when data changes"
                else "No primary key · pages can shift when data changes") else if (message.done) "Result complete" else "More rows available",error:null}
        }
    }
}
