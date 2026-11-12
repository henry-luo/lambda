// Excel criteria share case-insensitive comparisons and wildcard matching.
import c: .mod_formula_common

// Greedy matching keeps one remembered star; escaped wildcards are literal.
pub fn wildcard(pattern,value,prefix=false) bool^ {
    let pat=lower(pattern)^
    let input=lower(value)^
    let plen=len(pat)^
    let vlen=len(input)^
    var pi=0
    var vi=0
    var star= -1
    var consumed=0
    var failed=false
    while ((vi<vlen and not failed and not (prefix and pi==plen))^) {
        let ch=if (pi<plen) slice(pat,pi,pi+1)^ else ""
        let next=if (pi+1<plen) slice(pat,pi+1,pi+2)^ else ""
        let escaped=ch=="~" and contains(["*","?","~"],next)^
        let literal=if (escaped) next else ch
        if ((not escaped and ch=="*")^) { star=pi; consumed=vi; pi=pi+1 }
        else if (((not escaped and ch=="?") or (literal!="" and literal==slice(input,vi,vi+1)^))^) {
            pi=pi+(if (escaped) 2 else 1); vi=vi+1
        }
        else if ((star>=0)^) { consumed=consumed+1; vi=consumed; pi=star+1 }
        else { failed=true }
    }
    while ((pi<plen and slice(pat,pi,pi+1)^=="*")^) { pi=pi+1 }
    return not failed and (prefix or vi==vlen) and pi==plen
}
pub fn wildcard_exact(pattern,value) any^ => if (value is string) wildcard(pattern,value)^ else false
pub fn compile(condition) any^ {
    if (not (condition is string)) {op:"=",target:condition,pattern:false}
    else {
        let op=if (starts_with(condition,">=") or starts_with(condition,"<=") or starts_with(condition,"<>")) slice(condition,0,2)
            else if (starts_with(condition,">") or starts_with(condition,"<") or starts_with(condition,"=")) slice(condition,0,1) else "="
        let operand=if (op=="=" and not starts_with(condition,"=")) condition else slice(condition,len(op))
        let first=slice(operand,0,1)
        let target=if (first!="" and contains("0123456789.-+",first)) (float(operand) ^ {operand}) else operand;
        {op:op,target:target,pattern:target is string and (contains(target,"*") or contains(target,"?") or contains(target,"~"))}
    }
}
fn comparable(a,b) => (a is number and b is number) or (a is string and b is string) or (a is bool and b is bool)
pub fn accepts(value,rule) any^ {
    let op=rule.op
    let target=rule.target;
    if (op=="=" or op=="<>") {
        let same=if (rule.pattern) wildcard_exact(target,value)^ else c.equal(value,target);
        if (op=="=") same else not same
    }
    else if (not comparable(value,target)) false
    else if (op==">") c.greater(value,target)
    else if (op=="<") c.less(value,target)
    else if (op==">=") not c.less(value,target) else not c.greater(value,target)
}
fn same_shape(a,b) any^ => if (c.is_array_value(a) and c.is_array_value(b))
    a.rows==b.rows and a.cols==b.cols else len(c.range_values(a))==len(c.range_values(b))
// A single aggregation path enforces shape and error semantics for every *IF(S) function.
fn aggregate(values,pairs,mode) any^ {
    let input=c.range_values(values)
    let rules=[for (pair in pairs) {values:c.range_values(pair.range),rule:compile(pair.condition)^}]
    if (any([for (pair in pairs) ((not same_shape(values,pair.range)) ^ { true })])) raise error("#VALUE!")
    else {
        let tagged=[for (i,item in input) {value:item,matched:all([for (rule in rules) accepts(rule.values[i],rule.rule)^])^}]
        let selected=[for (item in tagged where item.matched) item.value]
        let numbers=[for (value in selected where value is number) value];
        if (mode=="count") len(selected)
        else if (mode=="sum") sum(numbers)
        else if (mode=="average") (if (len(numbers)==0) raise error("#DIV/0!") else sum(numbers)/len(numbers))
        else if (len(numbers)==0) 0
        else if (mode=="min") min(numbers) else max(numbers)
    }
}
fn paired(args,offset) => [for (i,item in args where i>=offset and (i-offset)%2==0) {range:item,condition:args[i+1]}]
pub fn countif(input_range,condition) any^ => aggregate(input_range,[{range:input_range,condition:condition}],"count")^
pub fn sumif(input_range,condition,sum_range=null) any^ => aggregate(if (sum_range==null) input_range else sum_range,[{range:input_range,condition:condition}],"sum")^
pub fn averageif(input_range,condition,average_range=null) any^ => aggregate(if (average_range==null) input_range else average_range,[{range:input_range,condition:condition}],"average")^
pub fn countifs(...) any^ {
    let args=varg();
    if (len(args)%2!=0) raise error("#VALUE!") else aggregate(args[0],paired(args,0),"count")^
}
fn many(args,mode) any^ => if (len(args)<3 or len(args)%2!=1) raise error("#VALUE!") else aggregate(args[0],paired(args,1),mode)^
pub fn sumifs(...) any^ => many(varg(),"sum")^
pub fn averageifs(...) any^ => many(varg(),"average")^
pub fn minifs(...) any^ => many(varg(),"min")^
pub fn maxifs(...) any^ => many(varg(),"max")^
