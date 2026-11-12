// Spreadsheet operations are pure Lambda functions, applied dynamically (S12.3.4v2).
fn numeric(value) any^ {
    if (value is number) value
    else if (value is bool) (if (value) 1 else 0)
    else if (value==null or value=="") 0
    else if (value is string) (float(value) ^ {raise error("#VALUE!")})
    else raise error("#VALUE!")
}
fn truth(value) any^ {
    if (value is bool) value else if (value is number) value!=0
    else if (value==null or value=="") false
    else if (value is string and upper(value)=="TRUE") true
    else if (value is string and upper(value)=="FALSE") false
    else raise error("#VALUE!")
}
fn text(value) any^ => if (value==null) "" else if (value is bool) (if (value) "TRUE" else "FALSE") else string(value)
fn range_values(arg) => if (arg is map and arg.kind=="range") arg.values else if (arg is array) arg
    else if (arg is map and arg.kind=="cellref") [arg.value] else [arg]
fn flatten(args) => [for (arg in args, value in ((range_values(arg)) ^ { [] })) value]
// Excel aggregates ignore text and booleans in references but coerce direct arguments.
fn argument_numbers(arg,ignore_text) any^ {
    if (arg is map or arg is array) [for (value in ((range_values(arg)) ^ { [] }) where value is number) value]
    else if (arg is number) [arg]
    else if (arg is bool) [if (arg) 1 else 0]
    else if (arg is string) {
        let converted=float(arg) ^ { ^ };
        if (converted is error) (if (ignore_text) [] else raise error("#VALUE!")) else [converted]
    }
    else []
}
fn numbers(args,ignore_text=false) any^ {
    let parts=[for (arg in args) argument_numbers(arg,ignore_text)^];
    [for (part in parts,value in part) value]
}
fn unary_plus(a) any^ => numeric(a)^
fn unary_minus(a) any^ => -numeric(a)^
fn percent(a) any^ => numeric(a)^/100
fn add(a,b) any^ => numeric(a)^+numeric(b)^
fn subtract(a,b) any^ => numeric(a)^-numeric(b)^
fn multiply(a,b) any^ => numeric(a)^*numeric(b)^
fn divide(a,b) any^ {
    let x=numeric(a)^
    let y=numeric(b)^;
    if (y==0) raise error("#DIV/0!") else x/y
}
fn power(a,b) any^ => numeric(a)^ ** numeric(b)^
fn concat_operator(a,b) any^ => text(a)^ ++ text(b)^
fn equal(a,b) => if (a is string and b is string) lower(a)==lower(b) else a==b
fn not_equal(a,b) => not equal(a,b)
fn less(a,b) => if (a is string and b is string) lower(a)<lower(b) else a<b
fn greater(a,b) => if (a is string and b is string) lower(a)>lower(b) else a>b
fn less_equal(a,b) => not greater(a,b)
fn greater_equal(a,b) => not less(a,b)

fn xl_sum(...) any^ => sum(numbers(varg())^)
fn xl_product(...) any^ {
    let values=numbers(varg())^;
    if (len(values)==0) 0 else product_values(values,0,1)^
}
fn product_values(values,i,result) any^ => if (i>=len(values)) result else product_values(values,i+1,result*values[i])^
fn xl_average(...) any^ { let values=numbers(varg())^; if (len(values)==0) raise error("#DIV/0!") else sum(values)/len(values) }
fn xl_min(...) any^ { let values=numbers(varg())^; if (len(values)==0) 0 else min(values) }
fn xl_max(...) any^ { let values=numbers(varg())^; if (len(values)==0) 0 else max(values) }
fn xl_count(...) any^ => len(numbers(varg(),true)^)
fn xl_counta(...) any^ { let values=flatten(varg())^; len([for (value in values where value!=null and value!="") value]) }
fn xl_countblank(input_range) => len([for (value in flatten([input_range]) where value==null or value=="") value])
fn xl_median(...) any^ {
    let values=sort(numbers(varg())^)
    let n=len(values);
    if (n==0) raise error("#NUM!") else if (n%2==1) values[n div 2] else (values[n div 2-1]+values[n div 2])/2
}
fn xl_abs(a) any^ => abs(numeric(a)^)
fn xl_int(a) any^ => floor(numeric(a)^)
fn xl_sign(a) any^ { let x=numeric(a)^; if (x<0) -1 else if (x>0) 1 else 0 }
fn xl_mod(a,b) any^ {
    let x=numeric(a)^
    let y=numeric(b)^;
    if (y==0) raise error("#DIV/0!") else x-y*floor(x/y)
}
fn xl_sqrt(a) any^ { let n=numeric(a)^; if (n<0) raise error("#NUM!") else sqrt(n) }
fn round_value(a,digits,mode) any^ {
    let n=numeric(a)^
    let d=trunc(numeric(digits)^);
    if (d>15 or d< -15) raise error("#NUM!") else {
        let scale=10.0 ** d
        let absolute=abs(n)*scale
        let rounded=if (mode=="up") ceil(absolute) else if (mode=="down") floor(absolute) else floor(absolute+0.5);
        (if (n<0) -rounded else rounded)/scale
    }
}
fn xl_round(a,digits) any^ => round_value(a,digits,"round")^
fn xl_roundup(a,digits) any^ => round_value(a,digits,"up")^
fn xl_rounddown(a,digits) any^ => round_value(a,digits,"down")^
fn xl_len(a) any^ => len(text(a)^)
fn xl_upper(a) any^ => upper(text(a)^)
fn xl_lower(a) any^ => lower(text(a)^)
fn xl_trim(a) any^ => join([for (part in ((split(trim(text(a)^)," ")) ^ { [] }) where part!="") part]," ")
fn xl_left(a,count=1) any^ { let n=trunc(numeric(count)^); if (n<0) raise error("#VALUE!") else slice(text(a)^,0,n) }
fn xl_right(a,count=1) any^ {
    let value=text(a)^
    let n=trunc(numeric(count)^);
    if (n<0) raise error("#VALUE!") else slice(value,max(0,len(value)-n))
}
fn xl_mid(a,start,count) any^ {
    let value=text(a)^
    let at=trunc(numeric(start)^)
    let n=trunc(numeric(count)^);
    if (at<1 or n<0) raise error("#VALUE!") else slice(value,at-1,at-1+n)
}
fn xl_concat(...) any^ { let values=flatten(varg())^; join([for (value in values) text(value)^],"") }
fn xl_and(...) any^ { let values=flatten(varg())^; all([for (value in values) truth(value)^]) }
fn xl_or(...) any^ { let values=flatten(varg())^; any([for (value in values) truth(value)^]) }
fn xl_not(a) any^ => not truth(a)^
fn xl_true() => true
fn xl_false() => false
fn xl_isnumber(value) => value is number
fn xl_istext(value) => value is string
fn xl_isblank(value) => value==null or value==""
fn xl_na() any^ => raise error("#N/A")
fn xl_iserror(first) { let result=call(first,[]) ^ { ^ }; result is error }
fn xl_isna(first) { let result=call(first,[]) ^ { ^ }; result is error and result.message=="#N/A" }
// Range values retain their two-dimensional shape for lookup functions.
fn is_range(value) => value is map and value.kind=="range"
fn xl_rows(value) => if (is_range(value)) value.rows else 1
fn xl_columns(value) => if (is_range(value)) value.cols else 1
fn xl_index(table,row,column=1) any^ {
    let r=trunc(numeric(row)^)
    let c=trunc(numeric(column)^);
    if (not is_range(table) or r<1 or c<1) raise error("#VALUE!")
    else if (r>table.rows or c>table.cols) raise error("#REF!")
    else table.values[(r-1)*table.cols+c-1]
}
fn comparable(a,b) => (a is number and b is number) or (a is string and b is string) or (a is bool and b is bool)
fn matches_for_lookup(key,values,mode) => [for (i,item in values where ((comparable(item,key)) ^ { false }))
    if (mode==0) (if (equal(item,key)) i else null)
    else if (mode>0) (if (not greater(item,key)) i else null)
    else (if (not less(item,key)) i else null)]
fn xl_match(key,lookup,kind=1) any^ {
    let mode=numeric(kind)^;
    if (not is_range(lookup) or (lookup.rows>1 and lookup.cols>1) or not contains([-1,0,1],mode)) raise error("#VALUE!")
    else {
        let candidates=matches_for_lookup(key,lookup.values,mode)
        let matched=[for (candidate in candidates where candidate!=null) candidate];
        if (len(matched)==0) raise error("#N/A") else (if (mode==0) matched[0] else matched[last])+1
    }
}
fn table_lookup(key,table,index,approximate,vertical) any^ {
    let offset=trunc(numeric(index)^)
    let approximate_match=truth(approximate)^;
    if (not is_range(table) or offset<1) raise error("#VALUE!")
    else if (offset>(if (vertical) table.cols else table.rows)) raise error("#REF!")
    else {
        let length=if (vertical) table.rows else table.cols
        let first=[for (i in 0 to length-1) table.values[if (vertical) i*table.cols else i]]
        let candidates=matches_for_lookup(key,first,if (approximate_match) 1 else 0)
        let matched=[for (candidate in candidates where candidate!=null) candidate];
        if (len(matched)==0) raise error("#N/A") else {
            let found=if (approximate_match) matched[last] else matched[0];
            table.values[if (vertical) found*table.cols+offset-1 else (offset-1)*table.cols+found]
        }
    }
}
fn xl_vlookup(key,table,column,approximate=true) any^ => table_lookup(key,table,column,approximate,true)^
fn xl_hlookup(key,table,row,approximate=true) any^ => table_lookup(key,table,row,approximate,false)^
fn criterion(value,condition) any^ {
    if (not (condition is string)) equal(value,condition)
    else if (contains(condition,"*") or contains(condition,"?") or contains(condition,"~"))
        raise error("#VALUE!")
    else {
        let op=if (starts_with(condition,">=") or starts_with(condition,"<=") or starts_with(condition,"<>")) slice(condition,0,2)
            else if (starts_with(condition,">") or starts_with(condition,"<") or starts_with(condition,"=")) slice(condition,0,1) else "="
        let operand=if (op=="=" and not starts_with(condition,"=")) condition else slice(condition,len(op))
        let first=slice(operand,0,1)
        let target=if (first!="" and contains("0123456789.-+",first)) (float(operand) ^ {operand}) else operand;
        if (op=="=") equal(value,target)
        else if (op=="<>") not equal(value,target)
        else if (not comparable(value,target)) false
        else if (op==">") greater(value,target)
        else if (op=="<") less(value,target)
        else if (op==">=") not less(value,target) else not greater(value,target)
    }
}
fn xl_countif(input_range,condition) any^ {
    let values=range_values(input_range)
    let matches=[for (value in values) criterion(value,condition)^];
    len([for (matched in matches where matched) matched])
}
fn xl_sumif(input_range,condition,sum_range=null) any^ {
    let keys=range_values(input_range)
    let vals=if (sum_range==null) keys else range_values(sum_range);
    if (len(keys)!=len(vals) or (is_range(input_range) and is_range(sum_range) and
        (input_range.rows!=sum_range.rows or input_range.cols!=sum_range.cols))) raise error("#VALUE!") else {
        let matches=[for (key in keys) criterion(key,condition)^];
        sum([for (i,matched in matches where matched and vals[i] is number) vals[i]])
    }
}
fn xl_sumproduct(...) any^ {
    let lists=[for (arg in varg()) range_values(arg)]
    let n=len(lists[0])^;
    let args=varg()
    let first=args[0]
    if (n==0 or any([for (i,items in lists) len(items)!=n or
        (is_range(first) and is_range(args[i]) and (first.rows!=args[i].rows or first.cols!=args[i].cols))])) raise error("#VALUE!")
    else sum([for (i in 0 to n-1) product_values([for (items in lists)
        if (items[i] is number) items[i] else 0],0,1)^])
}
// Branch arguments are zero-argument closures; only the selected one executes.
fn xl_if(condition,yes,no) any^ => call(if (truth(condition)^) yes else no,[])^
fn xl_iferror(first,fallback) any^ {
    let result=call(first,[]) ^ { ^ };
    if (result is error) call(fallback,[])^ else result
}
fn xl_ifna(first,fallback) any^ {
    let result=call(first,[]) ^ { ^ };
    if (result is error and result.message=="#N/A") call(fallback,[])^
    else if (result is error) raise result else result
}

// S12.3.1 limits dynamic calls to 16 arguments; report spreadsheet errors before dispatch.
let MAX_ARGS=16
pub let FUNCTIONS={
    SUM:{references:true,target:xl_sum,min:1,max:MAX_ARGS}, PRODUCT:{references:true,target:xl_product,min:1,max:MAX_ARGS},
    AVERAGE:{references:true,target:xl_average,min:1,max:MAX_ARGS}, MIN:{references:true,target:xl_min,min:1,max:MAX_ARGS}, MAX:{references:true,target:xl_max,min:1,max:MAX_ARGS},
    COUNT:{references:true,target:xl_count,min:1,max:MAX_ARGS}, COUNTA:{target:xl_counta,min:1,max:MAX_ARGS}, COUNTBLANK:{target:xl_countblank,min:1,max:1},
    MEDIAN:{references:true,target:xl_median,min:1,max:MAX_ARGS}, ABS:{target:xl_abs,min:1,max:1}, INT:{target:xl_int,min:1,max:1},
    SIGN:{target:xl_sign,min:1,max:1}, MOD:{target:xl_mod,min:2,max:2}, POWER:{target:power,min:2,max:2},
    SQRT:{target:xl_sqrt,min:1,max:1}, ROUND:{target:xl_round,min:2,max:2}, ROUNDUP:{target:xl_roundup,min:2,max:2},
    ROUNDDOWN:{target:xl_rounddown,min:2,max:2}, LEN:{target:xl_len,min:1,max:1}, UPPER:{target:xl_upper,min:1,max:1},
    LOWER:{target:xl_lower,min:1,max:1}, TRIM:{target:xl_trim,min:1,max:1}, LEFT:{target:xl_left,min:1,max:2},
    RIGHT:{target:xl_right,min:1,max:2}, MID:{target:xl_mid,min:3,max:3}, CONCAT:{target:xl_concat,min:1,max:MAX_ARGS},
    CONCATENATE:{target:xl_concat,min:1,max:MAX_ARGS}, AND:{target:xl_and,min:1,max:MAX_ARGS}, OR:{target:xl_or,min:1,max:MAX_ARGS},
    NOT:{target:xl_not,min:1,max:1}, TRUE:{target:xl_true,min:0,max:0}, FALSE:{target:xl_false,min:0,max:0},
    ROWS:{target:xl_rows,min:1,max:1}, COLUMNS:{target:xl_columns,min:1,max:1},
    INDEX:{target:xl_index,min:2,max:3}, MATCH:{target:xl_match,min:2,max:3}, VLOOKUP:{target:xl_vlookup,min:3,max:4}, HLOOKUP:{target:xl_hlookup,min:3,max:4},
    COUNTIF:{target:xl_countif,min:2,max:2}, SUMIF:{target:xl_sumif,min:2,max:3}, SUMPRODUCT:{references:true,target:xl_sumproduct,min:1,max:MAX_ARGS},
    IF:{target:xl_if,min:2,max:3,lazy:"if"}, IFERROR:{target:xl_iferror,min:2,max:2,lazy:"iferror"},
    IFNA:{target:xl_ifna,min:2,max:2,lazy:"iferror"}, NA:{target:xl_na,min:0,max:0},
    ISNUMBER:{target:xl_isnumber,min:1,max:1}, ISTEXT:{target:xl_istext,min:1,max:1}, ISBLANK:{target:xl_isblank,min:1,max:1},
    ISERROR:{target:xl_iserror,min:1,max:1,lazy:"inspect"}, ISNA:{target:xl_isna,min:1,max:1,lazy:"inspect"},
    ['+']:{target:add,min:2,max:2}, ['-']:{target:subtract,min:2,max:2}, ['*']:{target:multiply,min:2,max:2},
    ['/']:{target:divide,min:2,max:2}, ['^']:{target:power,min:2,max:2}, ['%']:{target:percent,min:1,max:1},
    ['u+']:{target:unary_plus,min:1,max:1}, ['u-']:{target:unary_minus,min:1,max:1}, ['&']:{target:concat_operator,min:2,max:2},
    ['=']:{target:equal,min:2,max:2}, ['<>']:{target:not_equal,min:2,max:2}, ['<']:{target:less,min:2,max:2},
    ['>']:{target:greater,min:2,max:2}, ['<=']:{target:less_equal,min:2,max:2}, ['>=']:{target:greater_equal,min:2,max:2}
}
