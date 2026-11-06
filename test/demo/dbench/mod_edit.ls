// S12.4.4v4: review plans first; one lexical SQLite transaction applies them.
import sqlite: lambda.io.sqlite
import sql: .mod_sql
import bytes_codec: lambda.security.tuf.metadata

pub let MAX_EDITOR_TEXT=65536
pub let MAX_EDITOR_BINARY=4096

pub fn sqlite_value(value) => value==null or value is string or value is binary or value is bool or
    (value is number and not (value is nan or value is inf or value == -inf))

pub fn editor_value(value) map^ {
    if (value is binary) {
        if (len(value)>MAX_EDITOR_BINARY) raise error("DBench: binary value is too large to edit")
        else {mode:"hex",text:bytes_codec.hex(value)^}
    } else if (value is string) {
        if (len(value)>MAX_EDITOR_TEXT) raise error("DBench: text value is too large to edit")
        else {mode:"text",text:value}
    } else if (sqlite_value(value)) {mode:"json",text:format(value,'json')^}
    else raise error("DBench: this value type cannot be edited")
}

pub fn binary_preview(value) string^ {
    let length=len(value)^
    let shown=if (length>32) slice(value,0,32)^ else value;
    "0x" ++ bytes_codec.hex(shown)^ ++ (if (length>32) "… (" ++ string(length) ++ " bytes)" else "")
}

pub fn parse_value(mode,text) any^ {
    if (not (text is string) or len(text)>MAX_EDITOR_TEXT) raise error("DBench: edit value exceeds the size limit")
    else if (mode=="text") text
    else if (mode=="hex") {
        if (len(text)>2*MAX_EDITOR_BINARY) raise error("DBench: binary value is too large to edit")
        else bytes_codec.unhex(text) ^ {raise error("DBench: enter an even number of hexadecimal digits")}
    } else if (mode=="json") {
        let value: any^=parse(text,'json')
        if (value is error or not sqlite_value(value) or value is binary)
            raise error("DBench: enter a JSON scalar")
        else value
    } else raise error("DBench: unknown value format")
}

fn guard(table,fields,row) map^ {
    let names=[for (field in fields) field.name]
    let keys=[for (i,field in fields where field.primary>0) i]
    if (not (row is array) or len(row)!=len(fields) or len(fields)>511 or len(keys)==0 or
        any([for (i in keys) row[i]==null]) or len(unique(names))!=len(names))
        raise error("DBench: this row has no usable primary key")
    else {
        let source=sql.ident("sqlite",table)^
        // Binary collation and typeof preserve distinctions hidden by affinity/collation.
        let predicate=join([for (field_name in names) {
            let quoted=sql.ident("sqlite",field_name)^;
            "(" ++ quoted ++ " IS ? COLLATE BINARY AND typeof(" ++ quoted ++ ") = typeof(?))"
        }]," AND ")^
        let expected=[for (item in row) *[item,item]];
        {table:table,key:[for (i in keys) row[i]],source:source,predicate:predicate,
            verify_sql:"SELECT COUNT(*) FROM " ++ source ++ " WHERE " ++ predicate,verify_params:expected}
    }
}

fn update_plan(base,edits) map^ {
    if (len(edits)+len(base.verify_params)>1024) raise error("DBench: too many values in this row update")
    else {
        let assignments=join([for (change in edits) sql.ident("sqlite",change.column)^ ++ " = ?"],", ")^;
        {*:base,action:"update",edits:edits,
            statement_sql:"UPDATE " ++ base.source ++ " SET " ++ assignments ++ " WHERE " ++ base.predicate,
            statement_params:[*[for (change in edits) change.value],*base.verify_params]}
    }
}

// Key values identify a row and are not changed by a cell update.
pub fn plan(table,fields,row,column,value) map^ {
    if (column<0 or column>=len(fields) or fields[column].primary>0 or not sqlite_value(value))
        raise error("DBench: this value is not eligible for editing")
    else {
        let base=guard(table,fields,row)^;
        update_plan(base,[{column:fields[column].name,old:row[column],value:value}])^
    }
}

// One original snapshot supplies the optimistic predicate for every edited cell.
pub fn merge_updates(previous,next) map^ {
    if (previous.action!="update" or next.action!="update" or previous.table!=next.table or
            previous.key!=next.key or len(previous.verify_params)!=len(next.verify_params) or
            any([for (i,value in previous.verify_params) type(value)!=type(next.verify_params[i]) or value!=next.verify_params[i]]))
        raise error("DBench: the staged row no longer matches; apply or revert it first")
    else {
        let fresh=next.edits[0]
        let edits=[*[for (change in previous.edits where change.column!=fresh.column) change],fresh];
        update_plan(previous,edits)^
    }
}

pub fn delete_plan(table,fields,row) map^ {
    let base=guard(table,fields,row)^;
    {*:base,action:"delete",column:"",old:"",value:"",
        statement_sql:"DELETE FROM " ++ base.source ++ " WHERE " ++ base.predicate,
        statement_params:base.verify_params}
}

// Inserts require an explicit complete key; omitted non-key columns use defaults.
pub fn insert_plan(table,fields,values) map^ {
    if (not (values is map) or len(fields)==0 or len(fields)>1024)
        raise error("DBench: enter a JSON object of column values")
    else {
        let supplied=[for (key,value in values) {name:string(key),value:value}]
        let names=[for (field in fields) field.name]
        let keys=[for (field in fields where field.primary>0) field.name]
        let input_names=[for (entry in supplied) entry.name]
        if (len(keys)==0 or len(supplied)==0 or any([for (key in keys) not (key in input_names) or values[key]==null]) or
            any([for (entry in supplied) not (entry.name in names) or
                not sqlite_value(entry.value)]))
            raise error("DBench: insert needs known columns, scalar values and a complete primary key")
        else {
            let source=sql.ident("sqlite",table)^
            let ordered=[for (field in fields where field.name in input_names) {name:field.name,value:values[field.name]}]
            let columns=join([for (entry in ordered) sql.ident("sqlite",entry.name)^],", ")^
            let placeholders=join([for (entry in ordered) "?"],", ")^
            let params=[for (entry in ordered) entry.value];
            {table:table,key:[for (key in keys) values[key]],action:"insert",column:"",old:"",value:"",
                verify_sql:null,verify_params:[],statement_sql:"INSERT INTO " ++ source ++ " (" ++ columns ++ ") VALUES (" ++ placeholders ++ ")",
                statement_params:params}
        }
    }
}

pub pn apply_staged(path,plans) any^ {
    if (not (plans is array) or len(plans)==0 or len(plans)>256)
        raise error("DBench: no valid staged changes")
    open tx = (sqlite.existing(path)^) {
        for (change in plans) {
            if (change.action!="insert") {
                let matching=sqlite.query(tx,change.verify_sql,change.verify_params)^
                if (matching!=[[1]]) raise error("DBench: row changed or is no longer unique")
            }
            sqlite.execute(tx,change.statement_sql,change.statement_params)^
            let affected=sqlite.query(tx,"SELECT changes()")^
            if (affected!=[[1]]) raise error("DBench: change did not affect exactly one row")
        }
    }
    return null
}

// S12.4.4v4: an explicitly reviewed statement has its own lexical transaction.
pub pn execute_sql(path,statement,params) any^ {
    open tx = (sqlite.existing(path)^) {
        sqlite.execute(tx,statement,params)^
    }
    return null
}
