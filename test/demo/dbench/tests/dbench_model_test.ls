import m: ~~.mod_model

let schema=[["items",false,"id","INTEGER",false,1],["items",false,"name","TEXT",true,0],
    ["names",true,"name","TEXT",true,0]]
let st={*:m.initial("sample.sqlite"),schema:schema,generation:2,token:7,filter_column:"name",filter_value:"O'Reilly"}
let request=m.table_request(st,"items",256)^
let stale=m.accept_batch(st,{generation:1,token:7,op:"page",columns:[],rows:[[4]],done:true,offset:0})^
let fresh=m.accept_batch(st,{generation:2,token:7,op:"page",columns:[["id","integer"]],rows:[[4]],done:true,offset:256})^
let more=m.accept_batch(fresh,{generation:2,token:7,op:"more",columns:[["id","integer"]],rows:[[5]],done:true,offset:256})^
let source={*:fresh,columns:[["id","integer"],["name","string"]],rows:[[1,"hello\tworld"],[2,"quote\"x"]]}
let selected=m.select(m.select(source,0,0)^,1,1,true)^
let compound=[["pairs",false,"a","INTEGER",false,2],["pairs",false,"b","TEXT",false,1]]
let keyed={*:m.initial(""),schema:compound,table:"pairs",columns:[["a","integer"],["b","string"]],rows:[[1,"a"],[2,"b"]]}
let remembered=m.remember_cursor(keyed,256,["b",2])^;
let window={*:fresh,mode:"query",spooled:true,total_rows:10000,row_base:5000,rows:[[5],[6]]};
let full={*:m.initial(""),mode:"query",spooled:true,total_rows:2048,row_base:0,
    rows:[for (i in 0 to 2047) [i]],bytes:32768};
let grown=m.accept_batch(full,{generation:0,token:0,op:"more",columns:[["id","integer"]],rows:[[2048]],done:false})^;
[
    m.objects(schema)==["items","names"],
    len(m.table_columns(schema,"items"))==2 and m.table_kind(schema,"names")=="view",
    request.sql=="SELECT \"id\", \"name\" FROM \"items\" WHERE \"name\" = ?1 ORDER BY \"id\" ASC LIMIT ?2 OFFSET ?3",
    request.params==["O'Reilly",256,256],
    m.table_request({*:st,filter_input_column:"id",filter_input_value:"9"},"items",256)^==request,
    stale.rows==[] and fresh.rows==[[4]] and fresh.offset==256,
    more.rows==[[4],[5]] and more.done,
    m.result_size([["abc",binary("xy"),null]])==52,
    m.bounds(selected)=={top:0,bottom:1,left:0,right:1},
    m.copy_text(selected)^=="1\t\"hello\tworld\"\n2\t\"quote\"\"x\"",
    m.next_cursor(keyed)^==["b",2] and remembered["256"]==["b",2],
    m.next_cursor({*:keyed,sort_column:"a"})^==[2,"b"] and
        m.next_cursor({*:keyed,rows:[[2,null]]})^==null,
    m.next_cursor({*:keyed,sort_column:"a",schema:[["pairs",false,"a","INTEGER",true,2],
        ["pairs",false,"b","TEXT",false,1]]})^==null,
    m.next_cursor({*:keyed,rows:[[2,join([for (i in 1 to 1025) "x"],"")^]]})^==null,
    m.row_count(window)^==10000 and m.row_cached(window,5000)^ and not m.row_cached(window,4999)^,
    m.row_value(window,5001)^==[6] and m.row_value(window,0)^==null,
    m.select(window,9000,0)^.row==9000,
    grown.total_rows==2049 and grown.row_base==2048 and grown.rows==[[2048]]
]
