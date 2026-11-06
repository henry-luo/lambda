// RDB5: quote identifiers separately and bind all data values.
pub let PAGE_SIZE=256
pub let MAX_PAGE_SIZE=1024
pub let MAX_FILTERS=32
pub let MAX_CURSOR_TEXT=1024

pub fn ident(engine, value) string^ {
    if (not (value is string) or value=="" or contains(value,"\u0000") or len(value)>1024)
        raise error("DBench: invalid SQL identifier")
    else if (engine=="mysql") "`" ++ replace(value,"`","``") ++ "`"
    else if (engine in ["sqlite","postgresql"]) "\"" ++ replace(value,"\"","\"\"") ++ "\""
    else raise error("DBench: unsupported database engine")
}

pub fn placeholder(engine, index) string^ => if (engine=="postgresql") "$" ++ string(index)
    else if (engine=="sqlite") "?" ++ string(index)
    else if (engine=="mysql") "?" else raise error("DBench: unsupported database engine")

fn bound_index(filters,index) => 1+len([for (i,f in filters where i<index and f.value!=null) f])
fn condition(engine,filter,index) string^ {
    let column=ident(engine,filter.column)^
    if (filter.value==null) column ++ (if (filter.op=="=") " IS NULL" else " IS NOT NULL")
    else column ++ " " ++ filter.op ++ " " ++ placeholder(engine,index)^
}
pub fn ordered_columns(ordering,primary_key) => [*ordering,*[for (key in primary_key where not (key in [for (item in ordering) item.column]))
    {column:key,direction:"ASC"}]]
// Bound each retained cursor so history and repeated predicate parameters stay small.
pub fn cursor_value(value) bool^ => if (value is string) len(value)^<=MAX_CURSOR_TEXT and not contains(value,"\u0000")^
    else if (value is number) not (value is nan or value is inf or value == -inf) else value is bool

// A key breaks ties within a snapshot; pages can still change between reads.
pub fn table_page(engine, table, columns, filters=[], ordering=[], primary_key=[], offset=0, limit=PAGE_SIZE, schema=null, cursor=null, whole=false) map^ {
    if (not (columns is array) or len(columns)==0 or len(columns)>1024 or
            not (filters is array) or len(filters)>MAX_FILTERS or not (ordering is array) or
            not (primary_key is array) or not (offset is int) or offset<0 or
            not (limit is int) or limit<1 or limit>MAX_PAGE_SIZE or (whole and (offset!=0 or cursor!=null)))
        raise error("DBench: invalid table page options")
    else if (any([for (f in filters) not (f.column in columns) or
            not (f.op in ["=","<>","<",">","<=",">=","LIKE"]) or
            (f.value==null and not (f.op in ["=","<>"]))]))
        raise error("DBench: invalid table filter")
    else if (any([for (s in ordering) not (s.column in columns) or not (s.direction in ["ASC","DESC"])]) or
            len(unique([for (s in ordering) s.column]))!=len(ordering) or
            any([for (key in primary_key) not (key in columns)]))
        raise error("DBench: invalid table ordering")
    else {
        let ordered=ordered_columns(ordering,primary_key)
        if (cursor!=null and (not (cursor is array) or len(cursor)!=len(ordered) or
                len(primary_key)==0 or len(ordered)>32 or
                any([for (value in cursor) not cursor_value(value)^])))
            raise error("DBench: invalid keyset cursor")
        else {
        let source=(if (schema==null) "" else ident(engine,schema)^ ++ ".") ++ ident(engine,table)^
        let selected=join([for (column in columns) ident(engine,column)^],", ")
        let filters_sql=[for (i,f in filters) condition(engine,f,bound_index(filters,i))^]
        let order_sql=join([for (item in ordered) ident(engine,item.column)^ ++ " " ++ item.direction],", ")
        let filter_values=[for (f in filters where f.value!=null) f.value]
        // Each lexicographic term binds its own prefix for all three dialects.
        let cursor_sql=if (cursor==null) null else "(" ++ join([for (i,item in ordered)
            "(" ++ join([for (j in 0 to i) ident(engine,ordered[j].column)^ ++
                (if (j!=i) " = " else if (item.direction=="DESC") " < " else " > ") ++
                placeholder(engine,len(filter_values)+1+(i*(i+1) div 2)+j)^]," AND ")^ ++ ")"]," OR ")^ ++ ")"
        let cursor_values=if (cursor==null) [] else [for (i,item in ordered,j in 0 to i) cursor[j]]
        let values=[*filter_values,*cursor_values]
        let predicates=if (cursor==null) filters_sql else [*filters_sql,cursor_sql]
        let sql="SELECT " ++ selected ++ " FROM " ++ source ++
            (if (len(predicates)>0) " WHERE " ++ join(predicates," AND ") else "") ++
            (if (len(ordered)>0) " ORDER BY " ++ order_sql else "") ++
            (if (whole) "" else " LIMIT " ++ placeholder(engine,len(values)+1)^ ++
                (if (cursor==null) " OFFSET " ++ placeholder(engine,len(values)+2)^ else ""));
        {sql:sql,params:if (whole) values else if (cursor==null) [*values,limit,offset] else [*values,limit],
            key_ordered:len(primary_key)>0,keyset:cursor!=null}
        }
    }
}
