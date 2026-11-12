// Formula syntax compiles into callable Lambda closures (S12.3.4v2).
import ops: .mod_formula_functions
let LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
fn letter(ch) any^ => ch!="" and contains(LETTERS,upper(ch))
fn digit(ch) any^ => ch!="" and contains("0123456789",ch)
pub fn column(col) any^ => if (col<26) slice(LETTERS,col,col+1) else column(int(floor(col/26))-1) ++ column(col%26)
pub fn address(row,col) any^ => column(col) ++ string(row+1)
fn column_index(text,i=0,value=0) any^ => if (i==len(text)) value-1
    else column_index(text,i+1,value*26+index_of(LETTERS,slice(text,i,i+1))+1)
fn letters_end(text,at=0) any^ => if (letter(slice(text,at,at+1))) letters_end(text,at+1) else at
pub fn position(text) map^ {
    let clean = replace(upper(trim(text)),"$","")
    let at = letters_end(clean)
    let tail = slice(clean,at)
    if (at==0 or tail=="" or not all([for (ch in ((split(tail,"")) ^ { [] })) digit(ch)])) raise error("#REF!")
    else {
        let row = int(tail)^-1
        let col = column_index(slice(clean,0,at))
        if (row<0 or row>=1048576 or col<0 or col>=16384) raise error("#REF!") else {row:row,col:col}
    }
}
// A prepared sheet supplies an address index so visible cells and references avoid scans.
pub fn indexed_cell(sheet,key) => sheet.cell_index[key]
pub fn cell(sheet,row,col) any^ => if (sheet.cell_index!=null) indexed_cell(sheet,address(row,col)^)
    else [for (c in sheet.cells where c.row==row and c.col==col) c][0]

// Cells and ranges stay deferred until the compiled closure is called.
fn reference(sheet,key,path) any^ {
    let p=position(key)^
    if (p.row>=sheet.rows or p.col>=sheet.cols) raise error("#REF!") else value(sheet,p.row,p.col,path)^
}
fn range_value(sheet,start,end,path) any^ {
    let a=position(start)^
    let b=position(end)^
    if (max(a.row,b.row)>=sheet.rows or max(a.col,b.col)>=sheet.cols) raise error("#REF!")
    else {
        let rows=abs(a.row-b.row)+1
        let cols=abs(a.col-b.col)+1;
        if (rows*cols>1000000) raise error("#VALUE!") else {kind:"range",rows:rows,cols:cols,
            values:[for (r in min(a.row,b.row) to max(a.row,b.row), c in min(a.col,b.col) to max(a.col,b.col)) value(sheet,r,c,path)^]}
    }
}
fn constant_plan(v) => (sheet,path) => v
fn reference_plan(key) => (sheet,path) any^ => {kind:"cellref",value:reference(sheet,key,path)^}
fn range_plan(start,end) => (sheet,path) any^ => range_value(sheet,start,end,path)^
fn thunk(plan,sheet,path) => () any^ => call(plan,[sheet,path])^
fn scalar(value) => if (value is map and value.kind=="cellref") value.value else value
fn invoke(name,plans,sheet,path) any^ {
    let entry=ops.FUNCTIONS[name];
    if (entry==null) raise error("#NAME?")
    else if (len(plans)<entry.min or len(plans)>entry.max) raise error("#VALUE!")
    else if (entry.lazy=="if") {
        let condition=scalar(call(plans[0],[sheet,path])^)
        let yes=thunk(plans[1],sheet,path)
        let no=if (len(plans)>2) thunk(plans[2],sheet,path) else (() => false);
        call(entry.target,[condition,yes,no])^
    }
    else if (entry.lazy=="iferror")
        call(entry.target,[thunk(plans[0],sheet,path),thunk(plans[1],sheet,path)])^
    else if (entry.lazy=="inspect") call(entry.target,[thunk(plans[0],sheet,path)])^
    else {
        let args=[for (plan in plans) call(plan,[sheet,path])^];
        call(entry.target,if (entry.references==true) args else [for (arg in args) scalar(arg)])^
    }
}
fn call_plan(name,plans) => (sheet,path) any^ => invoke(name,plans,sheet,path)^

fn digits_end(source,at) any^ => if (at<len(source) and digit(slice(source,at,at+1))) digits_end(source,at+1)^ else at
fn word_end(source,at,numeric) any^ => if (at>=len(source)) at else {
    let ch=slice(source,at,at+1);
    if (digit(ch) or (if (numeric) ch=="." else letter(ch) or ch=="$" or ch=="_")) word_end(source,at+1,numeric)^
    else if (numeric and upper(ch)=="E") {
        let sign=if (contains(["+","-"],slice(source,at+1,at+2))) 1 else 0
        let first=at+1+sign
        let end=digits_end(source,first)^;
        if (end==first) at else end
    } else at
}
fn quoted(source,at,text="") map^ {
    if (at>=len(source)) raise error("#VALUE!") else {
        let ch=slice(source,at,at+1);
        if (ch=="\"") {
            if (slice(source,at+1,at+2)=="\"") quoted(source,at+2,text ++ "\"")^
            else {value:text,at:at+1}
        } else quoted(source,at+1,text ++ ch)^
    }
}
fn tokens(source,at=0) array^ {
    if (len(source)>512) raise error("#VALUE!")
    else if (at>=len(source)) []
    else {
        let ch=slice(source,at,at+1)
        let pair=slice(source,at,at+2);
        if (contains(" \n\t\r",ch)) tokens(source,at+1)^
        else if (ch=="\"") {
            let result=quoted(source,at+1)^;
            [{literal:result.value},*tokens(source,result.at)^]
        }
        else if (contains(["<=",">=","<>"],pair)) [pair,*tokens(source,at+2)^]
        else if (contains("+-*/^%():,=<> &",ch)) [ch,*tokens(source,at+1)^]
        else if (digit(ch) or ch=="." or letter(ch) or ch=="$") {
            let end=word_end(source,at+1,digit(ch) or ch==".");
            [upper(slice(source,at,end)),*tokens(source,end)^]
        }
        else raise error("#VALUE!")
    }
}
// Each parser production constructs closures; execution needs no formula AST walker.
fn arguments(t,at,depth,args=[]) map^ {
    if (t[at]==")") {plans:args,at:at+1}
    else {
        let part=expression(t,at,depth+1)^
        if (t[part.at]==",") arguments(t,part.at+1,depth,[*args,part.plan])^
        else if (t[part.at]==")") {plans:[*args,part.plan],at:part.at+1}
        else raise error("#VALUE!")
    }
}
fn primary(t,at,depth) map^ {
    if (depth>64 or at>=len(t)) raise error("#VALUE!")
    else if (t[at]=="+" or t[at]=="-") {
        let part=primary(t,at+1,depth+1)^;
        {plan:call_plan("u" ++ t[at],[part.plan]),at:part.at}
    }
    else if (t[at]=="(") {
        let part=expression(t,at+1,depth+1)^;
        if (t[part.at]!=")") raise error("#VALUE!") else {*:part,at:part.at+1}
    }
    else if (t[at] is map) {plan:constant_plan(t[at].literal),at:at+1}
    else if (t[at+1]=="(") {
        let args=arguments(t,at+2,depth+1)^;
        {plan:call_plan(t[at],args.plans),at:args.at}
    }
    else if (t[at+1]==":") {
        let start=t[at]
        let end=t[at+2];
        // Validate both addresses at compile time without resolving their cells.
        if (end==null) raise error("#REF!") else {
            let a=position(start)^
            let b=position(end)^;
            {plan:range_plan(start,end),at:at+3}
        }
    }
    else if (t[at]=="TRUE" or t[at]=="FALSE") {plan:constant_plan(t[at]=="TRUE"),at:at+1}
    else if (letter(slice(t[at],0,1)) or starts_with(t[at],"$"))
        {plan:reference_plan(t[at]),at:at+1}
    else {plan:constant_plan(float(t[at]) ^ {raise error("#VALUE!")}),at:at+1}
}
fn percent_tail(t,left,depth) map^ => if (depth>64) raise error("#VALUE!")
    else if (t[left.at]=="%") percent_tail(t,{plan:call_plan("%",[left.plan]),at:left.at+1},depth+1)^
    else left
fn power(t,at,depth) map^ {
    let left=percent_tail(t,primary(t,at,depth+1)^,depth+1)^
    if (t[left.at]=="^") {
        let right=power(t,left.at+1,depth+1)^;
        {plan:call_plan("^",[left.plan,right.plan]),at:right.at}
    }
    else left
}
fn product_tail(t,left,depth) map^ {
    let op=t[left.at]
    if (op!="*" and op!="/") left
    else { let right=power(t,left.at+1,depth+1)^; product_tail(t,{plan:call_plan(op,[left.plan,right.plan]),at:right.at},depth+1)^ }
}
fn product(t,at,depth) map^ => product_tail(t,power(t,at,depth+1)^,depth+1)^
fn sum_tail(t,left,depth) map^ {
    let op=t[left.at]
    if (op!="+" and op!="-") left
    else { let right=product(t,left.at+1,depth+1)^; sum_tail(t,{plan:call_plan(op,[left.plan,right.plan]),at:right.at},depth+1)^ }
}
fn summation(t,at,depth) map^ => sum_tail(t,product(t,at,depth+1)^,depth+1)^
fn concat_tail(t,left,depth) map^ {
    if (t[left.at]!="&") left
    else { let right=summation(t,left.at+1,depth+1)^; concat_tail(t,{plan:call_plan("&",[left.plan,right.plan]),at:right.at},depth+1)^ }
}
fn concatenation(t,at,depth) map^ => concat_tail(t,summation(t,at,depth+1)^,depth+1)^
fn expression(t,at,depth) map^ {
    let left=concatenation(t,at,depth+1)^
    let op=t[left.at];
    if (not contains(["=","<>","<",">","<=",">="],op)) left
    else { let right=concatenation(t,left.at+1,depth+1)^; {plan:call_plan(op,[left.plan,right.plan]),at:right.at} }
}
fn evaluate(sheet,raw,path) any^ {
    let t=tokens(slice(raw,1))^
    let result=expression(t,0,0)^
    if (result.at!=len(t)) raise error("#VALUE!") else {
        let calculated=scalar(call(result.plan,[sheet,path])^);
        if (calculated is array or (calculated is map and calculated.kind=="range")) raise error("#VALUE!")
        else if (calculated is nan or calculated is inf or calculated == -inf) raise error("#NUM!")
        else calculated
    }
}
pub fn value(sheet,row,col,path=[]) any^ {
    let key = address(row,col)
    let raw = cell(sheet,row,col).raw or ""
    if (contains(path,key) or len(path)>64) raise error("#CYCLE!")
    else if (starts_with(raw,"=")) {
        let result = evaluate(sheet,raw,[*path,key]) ^ { ^ };
        // Saved XLSX results are a display fallback until the workbook is edited.
        if (result is error and cell(sheet,row,col).has_cached==true and
            (result.message=="#NAME?" or result.message=="#VALUE!" or result.message=="#REF!")) cell(sheet,row,col).cached
        else if (result is error) raise result else result
    }
    else if (starts_with(raw,"'")) slice(raw,1)
    else if (trim(raw)=="") ""
    else { let parsed=float(raw) ^ {raw}; if (parsed is nan or parsed is inf or parsed == -inf) raw else parsed }
}
pub fn display(sheet,row,col) any^ => value(sheet,row,col) ^ {if (starts_with(string(^.message),"#")) ^.message else "#VALUE!"}
