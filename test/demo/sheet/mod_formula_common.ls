// Array values and worksheet ranges share a rectangular shape.
pub fn is_array_value(value) => value is map and (value.kind=="range" or value.kind=="array")
// Per-cell errors survive inside arrays until a consuming operation inspects them.
pub fn cell_error(value) => value is map and value.kind=="excel_error"
pub fn checked(value) any^ => if (cell_error(value)) raise error(value.message) else value
// Shared spreadsheet value coercion and comparisons.
pub fn numeric(value) any^ {
    if (cell_error(value)) raise error(value.message)
    else if (value is number) value
    else if (value is bool) (if (value) 1 else 0)
    else if (value==null or value=="") 0
    else if (value is string) (float(value) ^ {raise error("#VALUE!")})
    else raise error("#VALUE!")
}
pub fn truth(value) any^ {
    if (cell_error(value)) raise error(value.message)
    else if (value is bool) value else if (value is number) value!=0
    else if (value==null or value=="") false
    else if (value is string and upper(value)=="TRUE") true
    else if (value is string and upper(value)=="FALSE") false
    else raise error("#VALUE!")
}
pub fn text(value) any^ => if (cell_error(value)) raise error(value.message) else if (value==null) "" else if (value is bool) (if (value) "TRUE" else "FALSE") else string(value)
pub fn range_values(arg) => if (is_array_value(arg)) arg.values else if (arg is array) arg
    else if (arg is map and arg.kind=="cellref") [arg.value] else [arg]
pub fn flatten(args) => [for (arg in args, value in ((range_values(arg)) ^ { [] })) value]
// Excel aggregates ignore text and booleans in references but coerce direct arguments.
pub fn argument_numbers(arg,ignore_text) any^ {
    if (arg is map or arg is array) {
        let values=range_values(arg)
        let errors=[for (value in values where cell_error(value)^) value];
        if (len(errors)>0) raise error(errors[0].message) else [for (value in values where value is number) value]
    }
    else if (arg is number) [arg]
    else if (arg is bool) [if (arg) 1 else 0]
    else if (arg is string) {
        let converted=float(arg) ^ { ^ };
        if (converted is error) (if (ignore_text) [] else raise error("#VALUE!")) else [converted]
    }
    else []
}
pub fn numbers(args,ignore_text=false) any^ {
    let parts=[for (arg in args) argument_numbers(arg,ignore_text)^];
    [for (part in parts,value in part) value]
}
pub fn equal(a,b) => if (a is string and b is string) lower(a)==lower(b) else a==b
pub fn not_equal(a,b) => not equal(a,b)
pub fn less(a,b) => if (a is string and b is string) lower(a)<lower(b) else a<b
pub fn greater(a,b) => if (a is string and b is string) lower(a)>lower(b) else a>b
pub fn less_equal(a,b) => not greater(a,b)
pub fn greater_equal(a,b) => not less(a,b)


// Shared fixed-point formatting keeps cell and TEXT rounding consistent.
pub fn fixed(value,places=2) any^ {
    let scale=10**places
    let rounded=int(round(abs(value)*scale))
    let whole=int(floor(rounded/scale))
    let fraction=string(rounded%scale);
    (if (value<0) "-" else "") ++ string(whole) ++
        (if (places==0) "" else "." ++ join(fill(max(0,places-len(fraction)),"0"),"") ++ fraction)
}
