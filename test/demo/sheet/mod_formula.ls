// bounded spreadsheet expressions; never execute cell contents as Lambda code.
let LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
fn letter(ch) => ch!="" and contains(LETTERS,upper(ch))
fn digit(ch) => ch!="" and contains("0123456789",ch)
pub fn column(col) => if (col<26) slice(LETTERS,col,col+1) else column(int(floor(col/26))-1) ++ column(col%26)
pub fn address(row,col) => column(col) ++ string(row+1)
fn column_index(text,i=0,value=0) => if (i==len(text)) value-1
    else column_index(text,i+1,value*26+index_of(LETTERS,slice(text,i,i+1))+1)
fn letters_end(text,at=0) => if (letter(slice(text,at,at+1))) letters_end(text,at+1) else at
pub fn position(text) map^ {
    let clean = replace(upper(trim(text)),"$","")
    let at = letters_end(clean)
    let tail = slice(clean,at)
    if (at==0 or tail=="" or not all([for (ch in split(tail,"")) digit(ch)])) raise error("#REF!")
    else {
        let row = int(tail)^-1
        let col = column_index(slice(clean,0,at))
        if (row<0 or row>=1048576 or col<0 or col>=16384) raise error("#REF!") else {row:row,col:col}
    }
}
fn word_end(source,at,numeric) => if (at>=len(source)) at else {
    let ch = slice(source,at,at+1);
    if (digit(ch) or (if (numeric) ch=="." else letter(ch) or ch=="$")) word_end(source,at+1,numeric) else at
}
fn tokens(source,at=0) array^ {
    if (len(source)>512) raise error("#VALUE!")
    else if (at>=len(source)) []
    else {
        let ch = slice(source,at,at+1)
        if (contains(" \n\t\r",ch)) tokens(source,at+1)^
        else if (contains("+-*/^%():,",ch)) [ch,*tokens(source,at+1)^]
        else if (digit(ch) or ch=="." or letter(ch) or ch=="$") {
            let end = word_end(source,at+1,digit(ch) or ch==".");
            [upper(slice(source,at,end)),*tokens(source,end)^]
        }
        else raise error("#VALUE!")
    }
}
pub fn cell(sheet,row,col) => [for (c in sheet.cells where c.row==row and c.col==col) c][0]
fn numeric(value) any^ => if (value is number) value else if (value==null or value=="") 0 else raise error("#VALUE!")
fn reference(sheet,key,path) any^ {
    let p = position(key)^
    if (p.row>=sheet.rows or p.col>=sheet.cols) raise error("#REF!") else value(sheet,p.row,p.col,path)^
}
fn aggregate(id,args) any^ {
    let values = [for (arg in args, v in (if (arg is array) arg else [arg]) where v is number) v]
    if (id=="SUM") sum(values)
    else if (id=="COUNT") len(values)
    else if (id=="AVERAGE") (if (len(values)==0) raise error("#DIV/0!") else sum(values)/len(values))
    else if (id=="MIN") (if (len(values)==0) 0 else min(values))
    else if (id=="MAX") (if (len(values)==0) 0 else max(values))
    else raise error("#NAME?")
}
fn arguments(sheet,t,at,path,depth,args=[]) map^ {
    if (t[at]==")") {value:args,at:at+1}
    else {
        let part = expression(sheet,t,at,path,depth+1)^
        if (t[part.at]==",") arguments(sheet,t,part.at+1,path,depth,[*args,part.value])^
        else if (t[part.at]==")") {value:[*args,part.value],at:part.at+1}
        else raise error("#VALUE!")
    }
}
fn primary(sheet,t,at,path,depth) map^ {
    if (depth>64 or at>=len(t)) raise error("#VALUE!")
    else if (t[at]=="+" or t[at]=="-") {
        let part = primary(sheet,t,at+1,path,depth+1)^;
        {value:numeric(part.value)^*(if (t[at]=="-") -1 else 1),at:part.at}
    }
    else if (t[at]=="(") {
        let part = expression(sheet,t,at+1,path,depth+1)^;
        if (t[part.at]!=")") raise error("#VALUE!") else {*:part,at:part.at+1}
    }
    else if (t[at+1]=="(") {
        let args = arguments(sheet,t,at+2,path,depth+1)^;
        {value:aggregate(t[at],args.value)^,at:args.at}
    }
    else if (t[at+1]==":") {
        let start = position(t[at])^
        let end = position(t[at+2])^
        if (max(start.row,end.row)>=sheet.rows or max(start.col,end.col)>=sheet.cols) raise error("#REF!")
        else {value:[for (r in min(start.row,end.row) to max(start.row,end.row),
            c in min(start.col,end.col) to max(start.col,end.col)) value(sheet,r,c,path)^],at:at+3}
    }
    else if (letter(slice(t[at],0,1)) or starts_with(t[at],"$"))
        {value:reference(sheet,t[at],path)^,at:at+1}
    else {value:float(t[at])^,at:at+1}
}
fn power(sheet,t,at,path,depth) map^ {
    let left = primary(sheet,t,at,path,depth+1)^
    if (t[left.at]=="%") {value:numeric(left.value)^/100,at:left.at+1}
    else if (t[left.at]=="^") {
        let right = power(sheet,t,left.at+1,path,depth+1)^;
        {value:numeric(left.value)^ ** numeric(right.value)^,at:right.at}
    }
    else left
}
fn product_tail(sheet,t,left,path,depth) map^ {
    let op = t[left.at]
    if (op!="*" and op!="/") left
    else {
        let right = power(sheet,t,left.at+1,path,depth+1)^
        let a = numeric(left.value)^
        let b = numeric(right.value)^
        if (op=="/" and b==0) raise error("#DIV/0!")
        else product_tail(sheet,t,{value:if (op=="*") a*b else a/b,at:right.at},path,depth+1)^
    }
}
fn product(sheet,t,at,path,depth) map^ => product_tail(sheet,t,power(sheet,t,at,path,depth+1)^,path,depth+1)^
fn sum_tail(sheet,t,left,path,depth) map^ {
    let op = t[left.at]
    if (op!="+" and op!="-") left
    else {
        let right = product(sheet,t,left.at+1,path,depth+1)^
        let a = numeric(left.value)^
        let b = numeric(right.value)^;
        sum_tail(sheet,t,{value:if (op=="+") a+b else a-b,at:right.at},path,depth+1)^
    }
}
fn expression(sheet,t,at,path,depth) map^ => sum_tail(sheet,t,product(sheet,t,at,path,depth+1)^,path,depth+1)^
pub fn value(sheet,row,col,path=[]) any^ {
    let key = address(row,col)
    let raw = cell(sheet,row,col).raw or ""
    if (contains(path,key) or len(path)>64) raise error("#CYCLE!")
    else if (starts_with(raw,"=")) {
        let t = tokens(slice(raw,1))^
        let result = expression(sheet,t,0,[*path,key],0)^
        if (result.at!=len(t) or result.value is array) raise error("#VALUE!")
        else if (result.value is nan or result.value is inf or result.value == -inf) raise error("#NUM!")
        else result.value
    }
    else if (starts_with(raw,"'")) slice(raw,1)
    else if (trim(raw)=="") ""
    else { let parsed=float(raw) ^ {raw}; if (parsed is nan or parsed is inf or parsed == -inf) raw else parsed }
}
pub fn display(sheet,row,col) => value(sheet,row,col) ^ {if (starts_with(string(^.message),"#")) ^.message else "#VALUE!"}
