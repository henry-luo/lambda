// Dynamic-array functions return a bounded, row-major rectangle.
import c: .mod_formula_common
fn shaped(value) => if (c.is_array_value(value)) value
    else {kind:"array",rows:1,cols:1,values:[value]}
fn rectangle(rows,cols,values) => {kind:"array",rows:rows,cols:cols,values:values}
fn checked_dimension(value) any^ { let n=trunc(c.numeric(value)^); if (n<1 or n>1000000) raise error("#CALC!") else n }
pub fn sequence(rows,cols=1,start=1,step=1) any^ {
    let r=checked_dimension(rows)^
    let c=checked_dimension(if (cols==null) 1 else cols)^
    let first=c.numeric(if (start==null) 1 else start)^
    let delta=c.numeric(if (step==null) 1 else step)^;
    if (r*c>1000000) raise error("#CALC!") else rectangle(r,c,[for (i in 0 to r*c-1) first+i*delta])
}
pub fn transpose(input) any^ {
    let a=shaped(input);
    rectangle(a.cols,a.rows,[for (r in 0 to a.cols-1, col in 0 to a.rows-1) a.values[col*a.cols+r]])
}
pub fn filter(input,include,if_empty=null) any^ {
    let a=shaped(input)
    let mask=shaped(include)
    let row_mode=mask.rows==a.rows and mask.cols==1
    let col_mode=mask.rows==1 and mask.cols==a.cols;
    if (not row_mode and not col_mode) raise error("#VALUE!") else {
        let selected=[for (i,item in mask.values where c.truth(item)^) i];
        if (len(selected)==0) (if (if_empty==null) raise error("#CALC!") else if_empty)
        else if (row_mode) rectangle(len(selected),a.cols,[for (r in selected,col in 0 to a.cols-1) a.values[r*a.cols+col]])
        else rectangle(a.rows,len(selected),[for (r in 0 to a.rows-1,col in selected) a.values[r*a.cols+col]])
    }
}
pub fn sort_array(input,sort_index=1,sort_order=1,by_col=false) any^ {
    let a=shaped(input)
    let index=trunc(c.numeric(if (sort_index==null) 1 else sort_index)^)
    let direction=trunc(c.numeric(if (sort_order==null) 1 else sort_order)^)
    let columns=c.truth(if (by_col==null) false else by_col)^;
    if (not contains([-1,1],direction) or index<1 or index>(if (columns) a.rows else a.cols)) raise error("#VALUE!") else {
        let indices=if (columns) 0 to a.cols-1 else 0 to a.rows-1
        let order=sort(indices,{dir:if (direction== -1) 'desc' else 'asc',by:(i) =>
            a.values[if (columns) (index-1)*a.cols+i else i*a.cols+index-1]});
        if (columns) rectangle(a.rows,a.cols,[for (r in 0 to a.rows-1,col in order) a.values[r*a.cols+col]])
        else rectangle(a.rows,a.cols,[for (r in order,col in 0 to a.cols-1) a.values[r*a.cols+col]])
    }
}
pub fn unique(input,by_col=false,exactly_once=false) any^ {
    let a=shaped(input)
    let columns=c.truth(if (by_col==null) false else by_col)^
    let once=c.truth(if (exactly_once==null) false else exactly_once)^
    let count=if (columns) a.cols else a.rows
    let units=[for (i in 0 to count-1) {index:i,items:if (columns)
        [for (r in 0 to a.rows-1) a.values[r*a.cols+i]] else
        [for (col in 0 to a.cols-1) a.values[i*a.cols+col]]}]
    let keyed=[for (unit in units) {*:unit,key:[for (item in unit.items) if (item is string) lower(item) else item]}]
    let groups=[for (unit in keyed group by unit.key as key into members) {first:members[0].index,count:len(members)}]
    let selected=sort([for (group in groups where not once or group.count==1) group.first]);
    if (len(selected)==0) raise error("#CALC!")
    else if (columns) rectangle(a.rows,len(selected),[for (r in 0 to a.rows-1,col in selected) a.values[r*a.cols+col]])
    else rectangle(len(selected),a.cols,[for (r in selected,col in 0 to a.cols-1) a.values[r*a.cols+col]])
}
// Locate delimiters in folded text while slicing the original spelling.
fn split_text(value,delimiter,mode,at=0) any^ {
    let remainder=slice(value,at)
    let found=index_of(if (mode==1) lower(remainder) else remainder,
        if (mode==1) lower(delimiter) else delimiter);
    if (found==null) [remainder] else [slice(remainder,0,found),*split_text(value,delimiter,mode,at+found+len(delimiter))^]
}
pub fn textsplit(input,col_delimiter,row_delimiter=null,ignore_empty=false,match_mode=0,pad_with=null) any^ {
    let value=c.text(input)^
    let col=if (col_delimiter==null) null else c.text(col_delimiter)^
    let row=if (row_delimiter==null) null else c.text(row_delimiter)^
    let ignore=c.truth(if (ignore_empty==null) false else ignore_empty)^
    let mode=trunc(c.numeric(if (match_mode==null) 0 else match_mode)^);
    if ((col==null and row==null) or col=="" or row=="" or not contains([0,1],mode)) raise error("#VALUE!") else {
        let lines=if (row==null) [value] else split_text(value,row,mode)^
        let grid=[for (line in lines where not ignore or line!="") if (col==null) [line] else
            [for (piece in split_text(line,col,mode)^ where not ignore or piece!="") piece]]
        let columns=max([for (line in grid) len(line)]);
        if (len(grid)==0 or columns==0 or len(grid)*columns>1000000) raise error("#CALC!")
        else rectangle(len(grid),columns,[for (line in grid,col_index in 0 to columns-1)
            if (col_index<len(line)) line[col_index] else if (pad_with==null) {kind:"excel_error",message:"#N/A"} else pad_with])
    }
}
