// Imported rules layer differential styles in Excel priority order.
import f: .mod_formula

fn matches_value(rule,value,evaluated) any^ {
    let op=rule.operator;
    if (op=="expression") {
        let first=evaluated[0];
        if (first is bool) first else if (first is number) first!=0 else false
    }
    else if (op=="containsBlanks" or op=="notContainsBlanks") {
        let empty=value==null or (value is string and trim(value)=="");
        if (op=="containsBlanks") empty else not empty
    }
    else if (op in ["containsText","notContainsText","beginsWith","endsWith"]) {
        let text=lower(if (value==null) "" else string(value))
        let needle=lower(rule.values[0]);
        if (op=="containsText") contains(text,needle)
        else if (op=="notContainsText") not contains(text,needle)
        else if (op=="beginsWith") starts_with(text,needle) else ends_with(text,needle)
    }
    else if (not (value is number)) false else {
        let a=evaluated[0]
        let b=evaluated[1];
        if (not (a is number) or (op in ["between","notBetween"] and not (b is number))) false
        else if (op=="equal") value==a
        else if (op=="notEqual") value!=a
        else if (op=="greaterThan") value>a
        else if (op=="greaterThanOrEqual") value>=a
        else if (op=="lessThan") value<a
        else if (op=="lessThanOrEqual") value<=a
        else if (op=="between") value>=a and value<=b
        else if (op=="notBetween") value<a or value>b
        else false
    }
}
function matches(rule,value,sheet,row,col,samples) any^ {
    let op=rule.operator
    let formulas=if (op=="expression") [rule.formulas[0]] else
        if (value is number and op in ["equal","notEqual","greaterThan","greaterThanOrEqual","lessThan",
            "lessThanOrEqual","between","notBetween"]) rule.formulas else null
    if (formulas==null) { return {matched:matches_value(rule,value,rule.values)^,samples:samples} }
    var current=samples
    var evaluated=fill(len(formulas)^,null)
    var i=0
    while (i<len(formulas)^) {
        let result=f.conditional_result(sheet,formulas[i],rule.anchor,row,col,current)^
        current=result.samples
        evaluated[i]=result.value
        i=i+1
    }
    return {matched:matches_value(rule,value,evaluated)^,samples:current}
}
fn within(rule,row,col) => any([for (rect in rule.ranges)
    row>=rect.top and row<=rect.bottom and col>=rect.left and col<=rect.right])
function apply_rules(rules,sheet,row,col,value,index,result,samples) any^ {
    if (index>=len(rules)) {style:result,samples:samples} else {
        let rule=rules[index]
        let test=if (within(rule,row,col)) matches(rule,value,sheet,row,col,samples)^ else {matched:false,samples:samples};
        if (test.matched) {
            let next={*:rule.style,*:result};
            if (rule.stop) {style:next,samples:test.samples} else apply_rules(rules,sheet,row,col,value,index+1,next,test.samples)^
        } else apply_rules(rules,sheet,row,col,value,index+1,result,test.samples)^
    }
}
pub function style(sheet,row,col,value,base) any^ {
    let result=apply_rules(sheet.conditional_rules or [],sheet,row,col,value,0,{},sheet.samples)^;
    {*:base,*:result.style}
}
// Keep worksheet dependencies consistent across all visible rules in this render.
pub function styles(sheet,anchors,default_style) any^ {
    let entries=[for (key,coords in anchors) {address:string(key)^,coords:coords}]
    var current=sheet.samples
    var computed=fill(len(entries),null)
    var i=0
    while (i<len(entries)) {
        let entry=entries[i]
        let cell=f.indexed_cell(sheet,entry.address)
        let value=sheet.calculated_values[entry.address]
        let result=apply_rules(sheet.conditional_rules, sheet,entry.coords[0],entry.coords[1],
            if (value==null) "" else value,0,{},current)^
        current=result.samples
        computed[i]={*:(cell.style or default_style),*:result.style}
        i=i+1
    }
    return map([for (i,entry in entries) *[entry.address,computed[i]]])
}
