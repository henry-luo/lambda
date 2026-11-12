// Imported cell-value rules layer differential styles in Excel priority order.
fn matches(rule,value) any^ {
    let op=rule.operator;
    if (op=="containsBlanks" or op=="notContainsBlanks") {
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
    let a=rule.values[0]
    let b=rule.values[1];
    if (rule.operator=="equal") value==a
    else if (rule.operator=="notEqual") value!=a
    else if (rule.operator=="greaterThan") value>a
    else if (rule.operator=="greaterThanOrEqual") value>=a
    else if (rule.operator=="lessThan") value<a
    else if (rule.operator=="lessThanOrEqual") value<=a
    else if (rule.operator=="between") value>=a and value<=b
    else if (rule.operator=="notBetween") value<a or value>b
    else false
    }
}
fn within(rule,row,col) => any([for (rect in rule.ranges)
    row>=rect.top and row<=rect.bottom and col>=rect.left and col<=rect.right])
fn apply_rules(rules,row,col,value,index,result) any^ {
    if (index>=len(rules)) result else {
        let rule=rules[index];
        if (within(rule,row,col) and matches(rule,value)^) {
            let next={*:rule.style,*:result};
            if (rule.stop) next else apply_rules(rules,row,col,value,index+1,next)^
        } else apply_rules(rules,row,col,value,index+1,result)^
    }
}
pub fn style(sheet,row,col,value,base) any^ {
    let conditional=apply_rules(sheet.conditional_rules or [],row,col,value,0,{})^;
    {*:base,*:conditional}
}
