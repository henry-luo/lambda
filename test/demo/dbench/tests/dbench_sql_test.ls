import sql: ~~.mod_sql

let filters=[{column:"name",op:"=",value:"O'Reilly"},{column:"value",op:"<>",value:null}]
let order=[{column:"name",direction:"DESC"}]
let pg=sql.table_page("postgresql","odd\"table",["id","name","value"],filters,order,["id"],512,256,"public")^
let mysql=sql.table_page("mysql","a`b",["id","name"],[{column:"name",op:"LIKE",value:"%x%"}],[],["id"],0,50)^
let sqlite=sql.table_page("sqlite","sample",["id"],[],[],[],0,10)^
let bad_column=sql.table_page("sqlite","sample",["id"],[{column:"id; DROP TABLE sample",op:"=",value:1}]) ^ {null}
let bad_op=sql.table_page("sqlite","sample",["id"],[{column:"id",op:"= 1 OR",value:1}]) ^ {null}
let bad_null=sql.table_page("sqlite","sample",["id"],[{column:"id",op:">",value:null}]) ^ {null}
let bad_limit=sql.table_page("sqlite","sample",["id"],[],[],[],0,1025) ^ {null}
let keyed=sql.table_page("postgresql","items",["a","b","name"],[{column:"name",op:"=",value:"O'Reilly"}],[],["a","b"],256,3,null,[7,"x"])^
let mysql_keyed=sql.table_page("mysql","items",["a","b"],[],[],["a","b"],256,3,null,[7,"x"])^
let descending=sql.table_page("sqlite","items",["a","b"],[],[{column:"b",direction:"DESC"}],["a","b"],256,3,null,["x",7])^
let bad_key=sql.table_page("sqlite","sample",["id"],[],[],["id"],1,2,null,[null]) ^ {null}
let whole=sql.table_page("postgresql","odd\"table",["id","name","value"],filters,order,["id"],0,256,"public",null,true)^
let bad_whole=sql.table_page("sqlite","sample",["id"],[],[],["id"],1,2,null,null,true) ^ {null};
[
    pg.sql=="SELECT \"id\", \"name\", \"value\" FROM \"public\".\"odd\"\"table\" WHERE \"name\" = $1 AND \"value\" IS NOT NULL ORDER BY \"name\" DESC, \"id\" ASC LIMIT $2 OFFSET $3",
    pg.params==["O'Reilly",256,512] and pg.key_ordered,
    mysql.sql=="SELECT `id`, `name` FROM `a``b` WHERE `name` LIKE ? ORDER BY `id` ASC LIMIT ? OFFSET ?",
    mysql.params==["%x%",50,0] and mysql.key_ordered,
    sqlite.sql=="SELECT \"id\" FROM \"sample\" LIMIT ?1 OFFSET ?2" and not sqlite.key_ordered,
    bad_column==null and bad_op==null and bad_null==null and bad_limit==null,
    keyed.sql=="SELECT \"a\", \"b\", \"name\" FROM \"items\" WHERE \"name\" = $1 AND ((\"a\" > $2) OR (\"a\" = $3 AND \"b\" > $4)) ORDER BY \"a\" ASC, \"b\" ASC LIMIT $5",
    keyed.params==["O'Reilly",7,7,"x",3] and keyed.keyset,
    mysql_keyed.params==[7,7,"x",3] and contains(mysql_keyed.sql,"(`a` = ? AND `b` > ?)") and bad_key==null,
    contains(descending.sql,"((\"b\" < ?1) OR (\"b\" = ?2 AND \"a\" > ?3))") and descending.params==["x","x",7,3],
    whole.sql=="SELECT \"id\", \"name\", \"value\" FROM \"public\".\"odd\"\"table\" WHERE \"name\" = $1 AND \"value\" IS NOT NULL ORDER BY \"name\" DESC, \"id\" ASC" and
        whole.params==["O'Reilly"] and bad_whole==null
]
