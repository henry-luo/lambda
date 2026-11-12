// Spreadsheet operations are pure Lambda functions, applied dynamically (S12.3.4v2).
import c: .mod_formula_common
import criteria: .mod_formula_criteria
import lookup: .mod_formula_lookup
import dates: .mod_formula_date
import txt: .mod_formula_text
import maths: .mod_formula_math
import stats: .mod_formula_stats
import finance: .mod_formula_finance
fn unary_plus(a) any^ => c.numeric(a)^
fn unary_minus(a) any^ => -c.numeric(a)^
fn percent(a) any^ => c.numeric(a)^/100
fn add(a,b) any^ => c.numeric(a)^+c.numeric(b)^
fn subtract(a,b) any^ => c.numeric(a)^-c.numeric(b)^
fn multiply(a,b) any^ => c.numeric(a)^*c.numeric(b)^
fn divide(a,b) any^ {
    let x=c.numeric(a)^
    let y=c.numeric(b)^;
    if (y==0) raise error("#DIV/0!") else x/y
}
fn power(a,b) any^ => c.numeric(a)^ ** c.numeric(b)^
fn concat_operator(a,b) any^ => c.text(a)^ ++ c.text(b)^

fn xl_sum(...) any^ => sum(c.numbers(varg())^)
fn xl_product(...) any^ {
    let values=c.numbers(varg())^;
    if (len(values)==0) 0 else product_values(values,0,1)^
}
fn product_values(values,i,result) any^ => if (i>=len(values)) result else product_values(values,i+1,result*values[i])^
fn xl_average(...) any^ { let values=c.numbers(varg())^; if (len(values)==0) raise error("#DIV/0!") else sum(values)/len(values) }
fn xl_min(...) any^ { let values=c.numbers(varg())^; if (len(values)==0) 0 else min(values) }
fn xl_max(...) any^ { let values=c.numbers(varg())^; if (len(values)==0) 0 else max(values) }
fn xl_count(...) any^ => len(c.numbers(varg(),true)^)
fn xl_counta(...) any^ { let values=c.flatten(varg())^; len([for (value in values where value!=null and value!="") value]) }
fn xl_countblank(input_range) => len([for (value in c.flatten([input_range]) where value==null or value=="") value])
fn xl_median(...) any^ {
    let values=sort(c.numbers(varg())^)
    let n=len(values);
    if (n==0) raise error("#NUM!") else if (n%2==1) values[n div 2] else (values[n div 2-1]+values[n div 2])/2
}
fn xl_abs(a) any^ => abs(c.numeric(a)^)
fn xl_int(a) any^ => floor(c.numeric(a)^)
fn xl_sign(a) any^ { let x=c.numeric(a)^; if (x<0) -1 else if (x>0) 1 else 0 }
fn xl_mod(a,b) any^ {
    let x=c.numeric(a)^
    let y=c.numeric(b)^;
    if (y==0) raise error("#DIV/0!") else x-y*floor(x/y)
}
fn xl_sqrt(a) any^ { let n=c.numeric(a)^; if (n<0) raise error("#NUM!") else sqrt(n) }
fn round_value(a,digits,mode) any^ {
    let n=c.numeric(a)^
    let d=trunc(c.numeric(digits)^);
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
fn xl_trunc(a,digits=0) any^ => round_value(a,digits,"down")^
fn xl_len(a) any^ => len(c.text(a)^)
fn xl_upper(a) any^ => upper(c.text(a)^)
fn xl_lower(a) any^ => lower(c.text(a)^)
fn xl_trim(a) any^ => join([for (part in ((split(trim(c.text(a)^)," ")) ^ { [] }) where part!="") part]," ")
fn xl_left(a,count=1) any^ { let n=trunc(c.numeric(count)^); if (n<0) raise error("#VALUE!") else slice(c.text(a)^,0,n) }
fn xl_right(a,count=1) any^ {
    let value=c.text(a)^
    let n=trunc(c.numeric(count)^);
    if (n<0) raise error("#VALUE!") else slice(value,max(0,len(value)-n))
}
fn xl_mid(a,start,count) any^ {
    let value=c.text(a)^
    let at=trunc(c.numeric(start)^)
    let n=trunc(c.numeric(count)^);
    if (at<1 or n<0) raise error("#VALUE!") else slice(value,at-1,at-1+n)
}
fn xl_concat(...) any^ { let values=c.flatten(varg())^; join([for (value in values) c.text(value)^],"") }
fn xl_and(...) any^ { let values=c.flatten(varg())^; all([for (value in values) c.truth(value)^]) }
fn xl_or(...) any^ { let values=c.flatten(varg())^; any([for (value in values) c.truth(value)^]) }
fn xl_not(a) any^ => not c.truth(a)^
fn xl_true() => true
fn xl_false() => false
fn xl_isnumber(value) => value is number
fn xl_istext(value) => value is string
fn xl_isblank(value) => value is map and value.kind=="cellref" and value.blank==true
fn xl_na() any^ => raise error("#N/A")
fn xl_iserror(first) { let result=call(first,[]) ^ { ^ }; result is error }
fn xl_isna(first) { let result=call(first,[]) ^ { ^ }; result is error and result.message=="#N/A" }
// Range values retain their two-dimensional shape for lookup functions.
fn is_range(value) => value is map and value.kind=="range"
fn xl_rows(value) => if (is_range(value)) value.rows else 1
fn xl_columns(value) => if (is_range(value)) value.cols else 1
fn xl_index(table,row,column=1) any^ {
    let r=trunc(c.numeric(row)^)
    let c=trunc(c.numeric(column)^);
    if (not is_range(table) or r<1 or c<1) raise error("#VALUE!")
    else if (r>table.rows or c>table.cols) raise error("#REF!")
    else table.values[(r-1)*table.cols+c-1]
}
fn comparable(a,b) => (a is number and b is number) or (a is string and b is string) or (a is bool and b is bool)
fn matches_for_lookup(key,values,mode) any^ => [for (i,item in values where ((comparable(item,key)) ^ { false }))
    if (mode==0) (if (if (key is string) criteria.wildcard_exact(key,item)^ else c.equal(item,key)) i else null)
    else if (mode>0) (if (not c.greater(item,key)) i else null)
    else (if (not c.less(item,key)) i else null)]
fn xl_match(key,lookup,kind=1) any^ {
    let mode=c.numeric(kind)^;
    if (not is_range(lookup) or (lookup.rows>1 and lookup.cols>1) or not contains([-1,0,1],mode)) raise error("#VALUE!")
    else {
        let candidates=matches_for_lookup(key,lookup.values,mode)^
        let matched=[for (candidate in candidates where candidate!=null) candidate];
        if (len(matched)==0) raise error("#N/A") else (if (mode==0) matched[0] else matched[last])+1
    }
}
fn table_lookup(key,table,index,approximate,vertical) any^ {
    let offset=trunc(c.numeric(index)^)
    let approximate_match=c.truth(approximate)^;
    if (not is_range(table) or offset<1) raise error("#VALUE!")
    else if (offset>(if (vertical) table.cols else table.rows)) raise error("#REF!")
    else {
        let length=if (vertical) table.rows else table.cols
        let first=[for (i in 0 to length-1) table.values[if (vertical) i*table.cols else i]]
        let candidates=matches_for_lookup(key,first,if (approximate_match) 1 else 0)^
        let matched=[for (candidate in candidates where candidate!=null) candidate];
        if (len(matched)==0) raise error("#N/A") else {
            let found=if (approximate_match) matched[last] else matched[0];
            table.values[if (vertical) found*table.cols+offset-1 else (offset-1)*table.cols+found]
        }
    }
}
fn xl_vlookup(key,table,column,approximate=true) any^ => table_lookup(key,table,column,approximate,true)^
fn xl_hlookup(key,table,row,approximate=true) any^ => table_lookup(key,table,row,approximate,false)^
fn xl_sumproduct(...) any^ {
    let lists=[for (arg in varg()) c.range_values(arg)]
    let n=len(lists[0])^;
    let args=varg()
    let first=args[0]
    if (n==0 or any([for (i,items in lists) len(items)!=n or
        (is_range(first) and is_range(args[i]) and (first.rows!=args[i].rows or first.cols!=args[i].cols))])) raise error("#VALUE!")
    else sum([for (i in 0 to n-1) product_values([for (items in lists)
        if (items[i] is number) items[i] else 0],0,1)^])
}
// Branch arguments are zero-argument closures; only the selected one executes.
fn xl_if(condition,yes,no) any^ {
    let result=call(if (c.truth(condition)^) yes else no,[])^;
    if (result==null) 0 else result
}
fn ifs_select(args,index) any^ {
    if (index>=len(args)) raise error("#N/A") else {
        let condition=call(args[index],[])^;
        if (c.truth(if (condition is map and condition.kind=="cellref") condition.value else condition)^)
            call(args[index+1],[])^ else ifs_select(args,index+2)^
    }
}
fn xl_ifs(...) any^ { let args=varg(); if (len(args)%2!=0) raise error("#VALUE!") else ifs_select(args,0)^ }
fn xl_choose(index,...) any^ {
    let choices=varg()
    let n=trunc(c.numeric(index)^);
    if (n<1 or n>len(choices)) raise error("#VALUE!") else call(choices[n-1],[])^
}
fn switch_select(lookup,args,index) any^ {
    if (index>=len(args)) raise error("#N/A")
    else if (index==len(args)-1) call(args[index],[])^
    else {
        let candidate=call(args[index],[])^
        let item=if (candidate is map and candidate.kind=="cellref") candidate.value else candidate;
        if (c.equal(lookup,item)) call(args[index+1],[])^ else switch_select(lookup,args,index+2)^
    }
}
fn xl_switch(lookup,...) any^ => switch_select(lookup,varg(),0)^
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
    SQRT:{target:xl_sqrt,min:1,max:1}, TRUNC:{target:xl_trunc,min:1,max:2},
    PI:{target:maths.pi,min:0,max:0}, EXP:{target:maths.exp,min:1,max:1}, LN:{target:maths.ln,min:1,max:1},
    LOG:{target:maths.log,min:1,max:2}, LOG10:{target:maths.log10,min:1,max:1},
    SIN:{target:maths.sin,min:1,max:1}, COS:{target:maths.cos,min:1,max:1}, TAN:{target:maths.tan,min:1,max:1},
    ASIN:{target:maths.asin,min:1,max:1}, ACOS:{target:maths.acos,min:1,max:1}, ATAN:{target:maths.atan,min:1,max:1},
    ATAN2:{target:maths.atan2,min:2,max:2}, DEGREES:{target:maths.degrees,min:1,max:1}, RADIANS:{target:maths.radians,min:1,max:1},
    QUOTIENT:{target:maths.quotient,min:2,max:2}, EVEN:{target:maths.even,min:1,max:1}, ODD:{target:maths.odd,min:1,max:1},
    MROUND:{target:maths.mround,min:2,max:2}, FACT:{target:maths.fact,min:1,max:1}, COMBIN:{target:maths.combin,min:2,max:2},
    SUMSQ:{references:true,target:maths.sumsq,min:1,max:MAX_ARGS},
    ['STDEV.S']:{references:true,target:stats.stdev_s,min:1,max:MAX_ARGS}, STDEV:{references:true,target:stats.stdev_s,min:1,max:MAX_ARGS},
    ['STDEV.P']:{references:true,target:stats.stdev_p,min:1,max:MAX_ARGS}, STDEVP:{references:true,target:stats.stdev_p,min:1,max:MAX_ARGS},
    ['VAR.S']:{references:true,target:stats.var_s,min:1,max:MAX_ARGS}, VAR:{references:true,target:stats.var_s,min:1,max:MAX_ARGS},
    ['VAR.P']:{references:true,target:stats.var_p,min:1,max:MAX_ARGS}, VARP:{references:true,target:stats.var_p,min:1,max:MAX_ARGS},
    LARGE:{target:stats.large,min:2,max:2}, SMALL:{target:stats.small,min:2,max:2},
    ['PERCENTILE.INC']:{target:stats.percentile,min:2,max:2}, PERCENTILE:{target:stats.percentile,min:2,max:2},
    ['QUARTILE.INC']:{target:stats.quartile,min:2,max:2}, QUARTILE:{target:stats.quartile,min:2,max:2},
    ['RANK.EQ']:{target:stats.rank,min:2,max:3}, RANK:{target:stats.rank,min:2,max:3},
    PMT:{target:finance.pmt,min:3,max:5}, PV:{target:finance.pv,min:3,max:5}, FV:{target:finance.fv,min:3,max:5},
    NPER:{target:finance.nper,min:3,max:5}, NPV:{references:true,target:finance.npv,min:2,max:MAX_ARGS}, ROUND:{target:xl_round,min:2,max:2}, ROUNDUP:{target:xl_roundup,min:2,max:2},
    ROUNDDOWN:{target:xl_rounddown,min:2,max:2}, LEN:{target:xl_len,min:1,max:1}, UPPER:{target:xl_upper,min:1,max:1},
    LOWER:{target:xl_lower,min:1,max:1}, TRIM:{target:xl_trim,min:1,max:1}, LEFT:{target:xl_left,min:1,max:2},
    RIGHT:{target:xl_right,min:1,max:2}, MID:{target:xl_mid,min:3,max:3}, CONCAT:{target:xl_concat,min:1,max:MAX_ARGS},
    CONCATENATE:{target:xl_concat,min:1,max:MAX_ARGS}, TEXTJOIN:{target:txt.textjoin,min:3,max:MAX_ARGS},
    EXACT:{target:txt.exact,min:2,max:2}, VALUE:{target:txt.value,min:1,max:1}, REPT:{target:txt.rept,min:2,max:2},
    CLEAN:{target:txt.clean,min:1,max:1}, REPLACE:{target:txt.replace_text,min:4,max:4}, SUBSTITUTE:{target:txt.substitute,min:3,max:4},
    FIND:{target:txt.find_text,min:2,max:3}, SEARCH:{target:txt.search,min:2,max:3}, AND:{target:xl_and,min:1,max:MAX_ARGS}, OR:{target:xl_or,min:1,max:MAX_ARGS},
    NOT:{target:xl_not,min:1,max:1}, TRUE:{target:xl_true,min:0,max:0}, FALSE:{target:xl_false,min:0,max:0},
    ROWS:{target:xl_rows,min:1,max:1}, COLUMNS:{target:xl_columns,min:1,max:1},
    XLOOKUP:{target:lookup.xlookup,min:3,max:6,lazy:"xlookup"}, XMATCH:{target:lookup.xmatch,min:2,max:4},
    INDEX:{target:xl_index,min:2,max:3}, MATCH:{target:xl_match,min:2,max:3}, VLOOKUP:{target:xl_vlookup,min:3,max:4}, HLOOKUP:{target:xl_hlookup,min:3,max:4},
    COUNTIF:{target:criteria.countif,min:2,max:2}, SUMIF:{target:criteria.sumif,min:2,max:3},
    AVERAGEIF:{target:criteria.averageif,min:2,max:3}, COUNTIFS:{target:criteria.countifs,min:2,max:MAX_ARGS},
    SUMIFS:{target:criteria.sumifs,min:3,max:MAX_ARGS}, AVERAGEIFS:{target:criteria.averageifs,min:3,max:MAX_ARGS},
    MINIFS:{target:criteria.minifs,min:3,max:MAX_ARGS}, MAXIFS:{target:criteria.maxifs,min:3,max:MAX_ARGS}, SUMPRODUCT:{references:true,target:xl_sumproduct,min:1,max:MAX_ARGS},
    DATE:{target:dates.date_value,min:3,max:3,date_context:true}, YEAR:{target:dates.year,min:1,max:1,date_context:true},
    MONTH:{target:dates.month,min:1,max:1,date_context:true}, DAY:{target:dates.day,min:1,max:1,date_context:true},
    WEEKDAY:{target:dates.weekday,min:1,max:2,date_context:true}, EDATE:{target:dates.edate,min:2,max:2,date_context:true},
    EOMONTH:{target:dates.eomonth,min:2,max:2,date_context:true}, DAYS:{target:dates.days,min:2,max:2},
    DATEVALUE:{target:dates.datevalue,min:1,max:1,date_context:true}, TIMEVALUE:{target:dates.timevalue,min:1,max:1},
    TIME:{target:dates.time_value,min:3,max:3}, HOUR:{target:dates.hour,min:1,max:1}, MINUTE:{target:dates.minute,min:1,max:1}, SECOND:{target:dates.second,min:1,max:1},
    IF:{target:xl_if,min:2,max:3,lazy:"if"}, IFS:{target:xl_ifs,min:2,max:MAX_ARGS,lazy:"thunks"},
    CHOOSE:{target:xl_choose,min:2,max:MAX_ARGS,lazy:"selector"}, SWITCH:{target:xl_switch,min:3,max:MAX_ARGS,lazy:"selector"}, IFERROR:{target:xl_iferror,min:2,max:2,lazy:"iferror"},
    IFNA:{target:xl_ifna,min:2,max:2,lazy:"iferror"}, NA:{target:xl_na,min:0,max:0},
    ISNUMBER:{target:xl_isnumber,min:1,max:1}, ISTEXT:{target:xl_istext,min:1,max:1}, ISBLANK:{references:true,target:xl_isblank,min:1,max:1},
    ISERROR:{target:xl_iserror,min:1,max:1,lazy:"inspect"}, ISNA:{target:xl_isna,min:1,max:1,lazy:"inspect"},
    ['+']:{target:add,min:2,max:2}, ['-']:{target:subtract,min:2,max:2}, ['*']:{target:multiply,min:2,max:2},
    ['/']:{target:divide,min:2,max:2}, ['^']:{target:power,min:2,max:2}, ['%']:{target:percent,min:1,max:1},
    ['u+']:{target:unary_plus,min:1,max:1}, ['u-']:{target:unary_minus,min:1,max:1}, ['&']:{target:concat_operator,min:2,max:2},
    ['=']:{target:c.equal,min:2,max:2}, ['<>']:{target:c.not_equal,min:2,max:2}, ['<']:{target:c.less,min:2,max:2},
    ['>']:{target:c.greater,min:2,max:2}, ['<=']:{target:c.less_equal,min:2,max:2}, ['>=']:{target:c.greater_equal,min:2,max:2}
}
