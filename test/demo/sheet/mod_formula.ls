// Formula syntax compiles into callable Lambda closures (S12.3.4v2).
import ops: .mod_formula_functions
import c: .mod_formula_common
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
pub fn index_cells(sheet) => map([for (cell in sheet.cells) *[address(cell.row,cell.col),cell]])
pub fn indexed_cell(sheet,key) => sheet.cell_index[key]
pub fn cell(sheet,row,col) any^ => if (sheet.cell_index!=null) indexed_cell(sheet,address(row,col)^)
    else [for (c in sheet.cells where c.row==row and c.col==col) c][0]

// Cells and ranges stay deferred until the compiled closure is called.
fn reference(sheet,key,path) any^ {
    let p=position(key)^
    if (p.row>=sheet.rows or p.col>=sheet.cols) raise error("#REF!") else {
        let source=cell(sheet,p.row,p.col)
        let computed=if (sheet.reference_only==true) null else value(sheet,p.row,p.col,path)^;
        {kind:"cellref",value:computed,blank:source==null or source.raw=="",row:p.row,col:p.col,title:sheet.title}
    }
}
fn range_value(sheet,start,end,path) any^ {
    let a=position(start)^
    let b=position(end)^
    if (max(a.row,b.row)>=sheet.rows or max(a.col,b.col)>=sheet.cols) raise error("#REF!")
    else {
        let rows=abs(a.row-b.row)+1
        let cols=abs(a.col-b.col)+1;
        if (rows*cols>1000000) raise error("#VALUE!") else {kind:"range",rows:rows,cols:cols,row:min(a.row,b.row),col:min(a.col,b.col),title:sheet.title,
            values:if (sheet.reference_only==true) [] else [for (r in min(a.row,b.row) to max(a.row,b.row), c in min(a.col,b.col) to max(a.col,b.col)) value(sheet,r,c,path)^]}
    }
}
// A compiled reference resolves against the current workbook without storing it in saved cells.
fn target_sheet(sheet,title) any^ {
    if (title==null or lower(title)==lower(sheet.title or "")) sheet
    else if (sheet.workbook==null) raise error("#REF!")
    else {
        let matches=[for (other in sheet.workbook.sheets where ((lower(other.title)==lower(title)) ^ {false})) other];
        if (len(matches)!=1) raise error("#REF!") else {
            let target=matches[0]
            let existing=sheet.workbook_indices[target.title];
            {*:target,workbook:sheet.workbook,workbook_indices:sheet.workbook_indices,workbook_spills:sheet.workbook_spills,
                *:(sheet.workbook_spills[target.title] or {}),cell_index:if (existing==null) index_cells(target) else existing}
        }
    }
}
fn constant_plan(v) => (sheet,path) => v
// Conditional formatting translates only relative reference components from its anchor.
fn translated_reference(key,offset) any^ {
    if (offset==null) key else {
        let position_value=position(key)^
        let column_absolute=starts_with(key,"$")
        let bare=if (column_absolute) slice(key,1) else key
        let end=letters_end(bare)^
        let row_absolute=starts_with(slice(bare,end),"$")
        let row=position_value.row+(if (row_absolute) 0 else offset.row)
        let col=position_value.col+(if (column_absolute) 0 else offset.col);
        if (row<0 or row>=1048576 or col<0 or col>=16384) raise error("#REF!") else address(row,col)^
    }
}
fn binding_name(key) => if ((starts_with(key,"_XLPM.")) ^ {false}) (slice(key,6) ^ {key}) else key
fn reference_plan(key,title=null) => (sheet,path) any^ =>
    if (title==null and sheet.formula_bindings[binding_name(key)]!=null) sheet.formula_bindings[binding_name(key)].value
    else reference(target_sheet(sheet,title)^,translated_reference(key,sheet.conditional_offset)^,path)^
fn spill_plan(key,title=null) => (sheet,path) any^ => {
    let target=target_sheet(sheet,title)^
    let translated=translated_reference(key,sheet.conditional_offset)^
    let source=target.spill_ranges[upper(replace(translated,"$",""))];
    if (source==null) raise error("#REF!") else source
}
fn range_plan(start,end,title=null) => (sheet,path) any^ =>
    range_value(target_sheet(sheet,title)^,translated_reference(start,sheet.conditional_offset)^,
        translated_reference(end,sheet.conditional_offset)^,path)^
fn thunk(plan,sheet,path) => () any^ => call(plan,[sheet,path])^
fn scalar(value) => if (value is map and value.kind=="cellref") value.value else value
// Excel broadcasts scalar, row, and column operands across a rectangular result.
fn array_operator(target,args) any^ {
    let left=if (c.is_array_value(args[0])) args[0] else {rows:1,cols:1,values:[args[0]]}
    let right=if (len(args)==1) {rows:1,cols:1,values:[null]} else if (c.is_array_value(args[1])) args[1]
        else {rows:1,cols:1,values:[args[1]]}
    let rows=max(left.rows,right.rows)
    let cols=max(left.cols,right.cols);
    if (rows*cols>1000000 or (left.rows!=1 and left.rows!=rows) or (right.rows!=1 and right.rows!=rows) or
        (left.cols!=1 and left.cols!=cols) or (right.cols!=1 and right.cols!=cols)) raise error("#VALUE!") else {
        let values=[for (r in 0 to rows-1,c in 0 to cols-1,
            let a=left.values[(if (left.rows==1) 0 else r)*left.cols+(if (left.cols==1) 0 else c)],
            let b=right.values[(if (right.rows==1) 0 else r)*right.cols+(if (right.cols==1) 0 else c)])
            if (c.cell_error(a)) a else if (len(args)>1 and c.cell_error(b)) b
            else (call(target,if (len(args)==1) [a] else [a,b]) ^ {{kind:"excel_error",message:^.message}})];
        {kind:"array",rows:rows,cols:cols,values:values}
    }
}
fn invoke(name,plans,sheet,path) any^ {
    let basic=if (starts_with(name,"_XLFN.")) slice(name,6) else name
    let resolved=if (starts_with(basic,"_XLWS.")) slice(basic,6) else basic
    let entry=ops.FUNCTIONS[resolved];
    if (entry==null and sheet.formula_bindings[binding_name(resolved)]==null and
        (sheet.workbook==null or len(sheet.workbook.defined_names or [])==0)) raise error("#NAME?")
    else if (entry==null) {
        let binding=sheet.formula_bindings[binding_name(resolved)]
        let func=if (binding!=null) binding.value else defined_lambda(sheet,resolved,path)^;
        if (func==null or func.kind!="lambda") raise error("#NAME!")
        else invoke_lambda(func,[for (plan in plans) scalar(call(plan,[sheet,path])^)])^
    }
    else if (len(plans)<entry.min or len(plans)>entry.max) raise error("#VALUE!")
    else if (entry.lazy=="subtotal") {
        let code=scalar(call(plans[0],[sheet,path])^)
        let refs=[for (i,plan in plans where i>0) call(plan,[{*:sheet,reference_only:true},path])^]
        let resolver=(ref,skip_hidden,skip_nested,skip_errors) any^ => aggregate_values(sheet,path,ref,skip_hidden,skip_nested,skip_errors)^;
        call(entry.target,[resolver,code,refs])^
    }
    else if (entry.lazy=="aggregate") {
        let code=scalar(call(plans[0],[sheet,path])^)
        let option=scalar(call(plans[1],[sheet,path])^)
        let function_number=trunc(c.numeric(code)^)
        let has_k=function_number>=14 and function_number<=19
        let refs=[for (i,plan in plans where i>=2 and (not has_k or i<len(plans)-1)) {
            let candidate=call(plan,[{*:sheet,reference_only:true},path]) ^ {null};
            if (candidate==null or (candidate is map and candidate.kind=="array"))
                (call(plan,[sheet,path]) ^ {{kind:"excel_error",message:^.message}}) else candidate
        }]
        let k=if (has_k and len(plans)>=4) scalar(call(plans[len(plans)-1],[sheet,path])^) else null
        let resolver=(ref,skip_hidden,skip_nested,skip_errors) any^ => aggregate_values(sheet,path,ref,skip_hidden,skip_nested,skip_errors)^;
        call(entry.target,[resolver,code,option,refs,k])^
    }
    else if (entry.reference_resolver==true) {
        let args=[for (i,plan in plans) call(plan,[if (resolved=="OFFSET" and i==0) {*:sheet,reference_only:true} else sheet,path])^]
        let resolver=(descriptor) any^ => resolve_reference(sheet,path,descriptor)^;
        call(entry.target,[resolver,*args])^
    }
    else if (entry.reference_context==true) {
        let args=[for (plan in plans) call(plan,[if (entry.references==true) {*:sheet,reference_only:true} else sheet,path])^];
        call(entry.target,[{row:sheet.formula_row,col:sheet.formula_col,column_name:column},
            *[for (arg in args) if (entry.references==true) arg else scalar(arg)]])^
    }
    else if (entry.lazy=="if") {
        let condition=scalar(call(plans[0],[sheet,path])^)
        let yes=thunk(plans[1],sheet,path)
        let no=if (len(plans)>2) thunk(plans[2],sheet,path) else (() => false);
        call(entry.target,[condition,yes,no])^
    }
    else if (entry.lazy=="iferror")
        call(entry.target,[thunk(plans[0],sheet,path),thunk(plans[1],sheet,path)])^
    else if (entry.lazy=="inspect") call(entry.target,[thunk(plans[0],sheet,path)])^
    else if (entry.lazy=="thunks") call(entry.target,[for (plan in plans) thunk(plan,sheet,path)])^
    else if (entry.lazy=="selector") {
        let first=scalar(call(plans[0],[sheet,path])^)
        let args=[first,*[for (i,plan in plans where i>0) thunk(plan,sheet,path)]];
        call(entry.target,args)^
    }
    else if (entry.lazy=="xlookup") {
        let args=[for (i,plan in plans) if (i==3) thunk(plan,sheet,path) else scalar(call(plan,[sheet,path])^)];
        call(entry.target,args)^
    }
    else {
        let args=[for (plan in plans) call(plan,[sheet,path])^];
        let normalized=if (entry.references==true) args else [for (arg in args) scalar(arg)];
        if (contains(["+","-","*","/","^","%","u+","u-","&","=","<>","<",">","<=",">="],resolved) and
            any([for (arg in normalized) c.is_array_value(arg)])) array_operator(entry.target,normalized)^
        else call(entry.target,if (entry.date_context==true) [sheet.date1904==true,*normalized] else normalized)^
    }
}
// Filtering happens before evaluation so hidden errors can be ignored.
fn aggregate_values(sheet,path,ref,skip_hidden,skip_nested,skip_errors) any^ {
    if (c.cell_error(ref)) (if (skip_errors) [] else raise error(ref.message))
    else if (ref is map and ref.kind=="array") {
        let errors=[for (item in ref.values where c.cell_error(item)^) item];
        if (len(errors)>0 and not skip_errors) raise error(errors[0].message)
        else [for (item in ref.values where not c.cell_error(item)^) item]
    }
    else if (not (ref is map) or not contains(["cellref","range"],ref.kind)) [ref] else {
        let target=target_sheet(sheet,ref.title)^
        let rows=if (ref.kind=="range") ref.rows else 1
        let cols=if (ref.kind=="range") ref.cols else 1
        let values=[for (r in ref.row to ref.row+rows-1, c in ref.col to ref.col+cols-1,
            let raw=upper(trim(cell(target,r,c).raw or ""))
            where not (skip_hidden and contains(target.hidden_rows or [],r)^) and
                not (skip_nested and (starts_with(raw,"=SUBTOTAL(")^ or starts_with(raw,"=AGGREGATE(")^)))
            (value(target,r,c,path) ^ {^})];
        if (not skip_errors and any([for (v in values) v is error])) raise [for (v in values where v is error) v][0]
        else [for (v in values where not (v is error)) v]
    }
}
// Only cell/range references are accepted from INDIRECT; arbitrary text is never run as code.
fn resolve_reference(sheet,path,descriptor) any^ {
    if (descriptor is string) {
        let t=tokens(descriptor)^
        let qualified=len(t)>=3 and t[1]=="!"
        let title=if (qualified) (if (t[0] is map) t[0].sheet else t[0]) else null
        let at=if (qualified) 2 else 0
        let start=t[at]
        let has_range=t[at+1]==":";
        if (not (start is string) or (has_range and not (t[at+2] is string)) or len(t)!=(at+(if (has_range) 3 else 1))) raise error("#REF!") else {
            let first=position(start)^
            let target=target_sheet(sheet,title)^;
            if (has_range) range_value(target,start,t[at+2],path)^ else reference(target,start,path)^
        }
    } else {
        let target=target_sheet(sheet,descriptor.title)^
        let first=address(descriptor.row,descriptor.col)^
        let final=address(descriptor.row+descriptor.rows-1,descriptor.col+descriptor.cols-1)^;
        if (descriptor.rows==1 and descriptor.cols==1) reference(target,first,path)^
        else range_value(target,first,final,path)^
    }
}
fn call_plan(name,plans) => (sheet,path) any^ => invoke(name,plans,sheet,path)^

fn digits_end(source,at) any^ => if (at<len(source) and digit(slice(source,at,at+1))) digits_end(source,at+1)^ else at
fn word_end(source,at,numeric) any^ => if (at>=len(source)) at else {
    let ch=slice(source,at,at+1);
    if (digit(ch) or (if (numeric) ch=="." else letter(ch) or ch=="$" or ch=="_" or ch==".")) word_end(source,at+1,numeric)^
    else if (numeric and upper(ch)=="E") {
        let sign=if (contains(["+","-"],slice(source,at+1,at+2))) 1 else 0
        let first=at+1+sign
        let end=digits_end(source,first)^;
        if (end==first) at else end
    } else at
}
fn quoted(source,at,delimiter,text="") map^ {
    if (at>=len(source)) raise error("#VALUE!") else {
        let ch=slice(source,at,at+1);
        if (ch==delimiter) {
            if (slice(source,at+1,at+2)==delimiter) quoted(source,at+2,delimiter,text ++ delimiter)^
            else {value:text,at:at+1}
        } else quoted(source,at+1,delimiter,text ++ ch)^
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
            let result=quoted(source,at+1,"\"")^;
            [{literal:result.value},*tokens(source,result.at)^]
        }
        else if (ch=="'") {
            let result=quoted(source,at+1,"'")^;
            [{sheet:result.value},*tokens(source,result.at)^]
        }
        else if (contains(["<=",">=","<>"],pair)) [pair,*tokens(source,at+2)^]
        else if (contains("+-*/^%():,=<> &!#",ch)) [ch,*tokens(source,at+1)^]
        else if (digit(ch) or ch=="." or letter(ch) or ch=="$" or ch=="_") {
            let end=word_end(source,at+1,digit(ch) or ch==".");
            [upper(slice(source,at,end)),*tokens(source,end)^]
        }
        else raise error("#VALUE!")
    }
}
// Each parser production constructs closures; execution needs no formula AST walker.
fn arguments(t,at,depth,args=[],after_comma=false) map^ {
    if (t[at]==")") {plans:if (after_comma) [*args,constant_plan(null)] else args,at:at+1}
    else if (t[at]==",") arguments(t,at+1,depth,[*args,constant_plan(null)],true)^
    else {
        let part=expression(t,at,depth+1)^
        if (t[part.at]==",") arguments(t,part.at+1,depth,[*args,part.plan],true)^
        else if (t[part.at]==")") {plans:[*args,part.plan],at:part.at+1}
        else raise error("#VALUE!")
    }
}
// LET bindings are lexical formula values; nested cells start with a fresh scope.
fn let_run(bindings,body,sheet,path,index=0) any^ {
    if (index>=len(bindings)) call(body,[sheet,path])^ else {
        let binding=bindings[index]
        let value=call(binding.plan,[sheet,path])^
        let context={*:sheet,formula_bindings:{*:(sheet.formula_bindings or {}),*:map([binding.name,{value:value}])}};
        let_run(bindings,body,context,path,index+1)^
    }
}
fn let_plan(bindings,body) => (sheet,path) any^ => {
    let run=() any^ => let_run(bindings,body,sheet,path)^;
    call(ops.FUNCTIONS.LET.target,[run])^
}
fn let_arguments(t,at,depth,bindings=[]) map^ {
    let name=if (t[at] is string) binding_name(t[at]) else t[at]
    let valid=name is string and letter(slice(name,0,1)) and not contains(["TRUE","FALSE"],name) and
        ((position(name) ^ {null})==null);
    if (len(bindings)>=7 or not valid or t[at+1]!=",") raise error("#VALUE!") else {
        let value=expression(t,at+2,depth+1)^
        let next=value.at+1
        let all_bindings=[*bindings,{name:name,plan:value.plan}];
        if (t[value.at]!=",") raise error("#VALUE!")
        else if (t[next+1]==",") let_arguments(t,next,depth+1,all_bindings)^
        else {
            let body=expression(t,next,depth+1)^;
            if (t[body.at]!=")") raise error("#VALUE!") else {plan:let_plan(all_bindings,body.plan),at:body.at+1}
        }
    }
}
// Excel function values capture the lexical formula environment, never source code.
fn invoke_lambda(func,args) any^ {
    if (len(args)!=len(func.params)) raise error("#VALUE!")
    else call(func.target,[{values:args}])^
}
fn lambda_plan(params,body) => (sheet,path) any^ => {
    let target=(packet) any^ => {
        let bindings=map([for (i,param in params) *[param,{value:packet.values[i]}]])
        let context={*:sheet,lambda_depth:(sheet.lambda_depth or 0)+1,formula_bindings:{*:(sheet.formula_bindings or {}),*:bindings}};
        if (context.lambda_depth>64) raise error("#NUM!") else call(body,[context,path])^
    };
    call(ops.FUNCTIONS.LAMBDA.target,[{params:params,target:target}])^
}
fn lambda_arguments(t,at,depth,params=[]) map^ {
    if (depth>64) raise error("#VALUE!")
    else if (t[at+1]==",") {
        let param=if (t[at] is string) binding_name(t[at]) else t[at]
        let valid=param is string and letter(slice(param,0,1)) and not contains(["TRUE","FALSE"],param) and
            ((position(param) ^ {null})==null);
        if (not valid or contains(params,param)) raise error("#VALUE!") else lambda_arguments(t,at+2,depth+1,[*params,param])^
    } else {
        let body=expression(t,at,depth+1)^;
        if (t[body.at]!=")") raise error("#VALUE!") else {plan:lambda_plan(params,body.plan),at:body.at+1}
    }
}
// Workbook and sheet-scoped names remain data until their callable plan is requested.
fn defined_lambda(sheet,name,path) any^ {
    if (sheet.workbook==null) { return null }
    let definitions=sheet.workbook.defined_names or []
    let index=[for (i,item in (sheet.workbook.sheets or []) where lower(item.title)==lower(sheet.title or "")) i][0]
    let matching=[for (entry in definitions where upper(entry.name)==name and (entry.scope==null or entry.scope==index)) entry]
    let scoped=[for (entry in matching where entry.scope!=null) entry]
    let chosen=if (len(scoped)>0) scoped[0] else matching[0];
    if (chosen==null) null else {
        let raw=if (starts_with(chosen.formula,"=")) slice(chosen.formula,1) else chosen.formula
        let t=tokens(raw)^
        let plan=expression(t,0,0)^;
        if (plan.at!=len(t)) raise error("#VALUE!") else call(plan.plan,[sheet,path])^
    }
}
fn apply_plan(plan,params) => (sheet,path) any^ => {
    let func=call(plan,[sheet,path])^
    let args=[for (param in params) scalar(call(param,[sheet,path])^)];
    if (not (func is map) or func.kind!="lambda") raise error("#VALUE!") else invoke_lambda(func,args)^
}
fn atom(t,at,depth) map^ {
    if (depth>64 or at>=len(t)) raise error("#VALUE!")
    else if (t[at]=="+" or t[at]=="-") {
        let part=primary(t,at+1,depth+1)^;
        {plan:call_plan("u" ++ t[at],[part.plan]),at:part.at}
    }
    else if (t[at]=="(") {
        let part=expression(t,at+1,depth+1)^;
        if (t[part.at]!=")") raise error("#VALUE!") else {*:part,at:part.at+1}
    }
    else if (t[at+1]=="!") {
        let title=if (t[at] is map) t[at].sheet else t[at]
        let start=t[at+2]
        let end=if (t[at+3]==":") t[at+4] else null;
        if (title==null or start==null or (t[at+3]==":" and end==null)) raise error("#REF!") else {
            let checked_start=position(start)^
            let checked_end=if (end!=null) position(end)^ else null;
            let spilled=t[at+3]=="#";
            {plan:if (spilled) spill_plan(start,title) else if (end==null) reference_plan(start,title) else range_plan(start,end,title),
                at:at+(if (spilled) 4 else if (end==null) 3 else 5)}
        }
    }
    else if (t[at] is map) {plan:constant_plan(t[at].literal),at:at+1}
    else if (contains(["LET","_XLFN.LET"],t[at]) and t[at+1]=="(") let_arguments(t,at+2,depth+1)^
    else if (contains(["LAMBDA","_XLFN.LAMBDA"],t[at]) and t[at+1]=="(") lambda_arguments(t,at+2,depth+1)^
    else if (t[at+1]=="(") {
        let args=arguments(t,at+2,depth+1)^;
        {plan:call_plan(t[at],args.plans),at:args.at}
    }
    else if (t[at+1]=="#") {
        let checked=position(t[at])^;
        {plan:spill_plan(t[at]),at:at+2}
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
    else if (letter(slice(t[at],0,1)) or starts_with(t[at],"$") or starts_with(t[at],"_"))
        {plan:reference_plan(t[at]),at:at+1}
    else {plan:constant_plan(float(t[at]) ^ {raise error("#VALUE!")}),at:at+1}
}
fn call_suffix(t,left,depth) map^ {
    if (depth>64) raise error("#VALUE!") else if (t[left.at]!="(") left else {
        let args=arguments(t,left.at+1,depth+1)^;
        call_suffix(t,{plan:apply_plan(left.plan,args.plans),at:args.at},depth+1)^
    }
}
fn primary(t,at,depth) map^ => call_suffix(t,atom(t,at,depth+1)^,depth+1)^
fn percent_tail(t,left,depth) map^ => if (depth>64) raise error("#VALUE!")
    else if (t[left.at]=="%") percent_tail(t,{plan:call_plan("%",[left.plan]),at:left.at+1},depth+1)^
    else left
// Excel evaluates operators of equal precedence from left to right.
fn power_tail(t,left,depth) map^ {
    if (depth>64) raise error("#VALUE!")
    else if (t[left.at]!="^") left else {
        let right=percent_tail(t,primary(t,left.at+1,depth+1)^,depth+1)^;
        power_tail(t,{plan:call_plan("^",[left.plan,right.plan]),at:right.at},depth+1)^
    }
}
fn power(t,at,depth) map^ => power_tail(t,percent_tail(t,primary(t,at,depth+1)^,depth+1)^,depth+1)^
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
fn evaluate(sheet,raw,path,allow_array=false) any^ {
    let t=tokens(slice(raw,1))^
    let result=expression(t,0,0)^
    if (result.at!=len(t)) raise error("#VALUE!") else {
        let calculated=scalar(call(result.plan,[sheet,path])^);
        if (calculated is map and calculated.kind=="lambda") raise error("#CALC!")
        else if (not allow_array and (calculated is array or c.is_array_value(calculated))) raise error("#VALUE!")
        else if (calculated is nan or calculated is inf or calculated == -inf) raise error("#NUM!")
        else calculated
    }
}
// The formula remains a parsed call plan; no spreadsheet text becomes Lambda source.
pub fn conditional(sheet,formula,anchor,row,col) any^ {
    let source=if (starts_with(formula,"=")) formula else "=" ++ formula
    let context={*:sheet,formula_row:row,formula_col:col,conditional_offset:{row:row-anchor.row,col:col-anchor.col}};
    evaluate(context,source,[])^
}
// Literal cells do not need formula parsing or dependency tracking.
pub fn literal_value(raw) any^ {
    if (starts_with(raw,"'")) slice(raw,1)
    else if (trim(raw)=="") ""
    else { let parsed=float(raw) ^ {raw}; if (parsed is nan or parsed is inf or parsed == -inf) raw else parsed }
}
pub fn raw_value(sheet,row,col,path=[]) any^ {
    let cell_address=address(row,col)
    let key = lower(sheet.title or "") ++ "!" ++ cell_address
    let raw = cell(sheet,row,col).raw or ""
    let spill=if (sheet.spill_values==null or len(sheet.spill_values)==0) null else sheet.spill_values[cell_address];
    if (raw=="" and spill!=null) (if (contains(path,spill.key)) raise error("#CYCLE!") else c.checked(spill.value)^)
    else if (contains(path,key) or len(path)>64) raise error("#CYCLE!")
    else if (starts_with(raw,"=")) {
        let result = evaluate({*:sheet,conditional_offset:null,formula_bindings:null,formula_row:row,formula_col:col},raw,[*path,key],true) ^ { {formula_error:^.message} };
        // Saved XLSX results are a display fallback until the workbook is edited.
        if (result is map and result.formula_error!=null) {
            if (cell(sheet,row,col).has_cached==true and contains(["#NAME?","#VALUE!","#REF!"],result.formula_error)) cell(sheet,row,col).cached
            else raise error(result.formula_error)
        } else result
    }
    else literal_value(raw)^
}
pub fn value(sheet,row,col,path=[]) any^ {
    let error_value=if (sheet.spill_errors==null or len(sheet.spill_errors)==0) null else sheet.spill_errors[address(row,col)];
    if (error_value!=null) raise error(error_value) else {
        let result=raw_value(sheet,row,col,path)^;
        if (c.is_array_value(result)) c.checked(result.values[0])^ else c.checked(result)^
    }
}
// Spill geometry is derived from formulas and never saved as authored cells.
fn spill_intersects(a,b) => a.row<b.row+b.result.rows and b.row<a.row+a.result.rows and
    a.col<b.col+b.result.cols and b.col<a.col+a.result.cols
fn spill_blocked(sheet,candidates,anchor) any^ {
    let result=anchor.result;
    if (anchor.row+result.rows>sheet.rows or anchor.col+result.cols>sheet.cols) true
    else if (any([for (other in candidates where other.row!=anchor.row or other.col!=anchor.col)
        spill_intersects(anchor,other)])) true
    else if (any([for (rect in (sheet.merges or [])) rect.top<anchor.row+result.rows and rect.bottom>=anchor.row and
        rect.left<anchor.col+result.cols and rect.right>=anchor.col])) true
    else any([for (r in anchor.row to anchor.row+result.rows-1,c in anchor.col to anchor.col+result.cols-1
        where r!=anchor.row or c!=anchor.col) (cell(sheet,r,c).raw or "")!=""])
}
fn spill_display_value(value) => if (c.cell_error(value)) value.message else value
// Only formulas with a rectangular source or array-producing call can spill.
pub fn may_spill(raw,book=null) any^ {
    let text=upper(raw or "");
    starts_with(text,"=") and (contains(text,":") or contains(text,"#") or
        any([for (name in ["SEQUENCE(","TRANSPOSE(","FILTER(","SORT(","UNIQUE(","TEXTSPLIT(","LAMBDA(","LET(","OFFSET(","INDIRECT("])
            contains(text,name)]) or (book!=null and any([for (entry in (book.defined_names or []))
            contains(text,upper(entry.name or "") ++ "(")])))
}
pub fn spill_pass(sheet) any^ {
    let candidates=[for (source in sheet.cells,
        let result=if (may_spill(source.raw,sheet.workbook)^) (raw_value(sheet,source.row,source.col) ^ {null}) else null
        where c.is_array_value(result)) {row:source.row,col:source.col,result:result}]
    let blocked=[for (candidate in candidates where spill_blocked(sheet,candidates,candidate)^) candidate]
    let allowed=[for (candidate in candidates where not spill_blocked(sheet,candidates,candidate)^) candidate]
    let errors=map([for (candidate in blocked) *[address(candidate.row,candidate.col),"#SPILL!"]])
    let ranges=map([for (candidate in allowed) *[address(candidate.row,candidate.col),candidate.result]])
    let values=map([for (candidate in allowed,r in 0 to candidate.result.rows-1,c in 0 to candidate.result.cols-1
        where r!=0 or c!=0) *[address(candidate.row+r,candidate.col+c),
        {value:candidate.result.values[r*candidate.result.cols+c],row:candidate.row,col:candidate.col,key:lower(sheet.title or "") ++ "!" ++ address(candidate.row,candidate.col)}]]);
    {spill_values:values,spill_ranges:ranges,spill_errors:errors,
        spill_display:map([for (candidate in allowed,r in 0 to candidate.result.rows-1,c in 0 to candidate.result.cols-1
            where r!=0 or c!=0) *[address(candidate.row+r,candidate.col+c),
                spill_display_value(candidate.result.values[r*candidate.result.cols+c])]])}
}
// A non-converging spill graph must report a cycle instead of publishing stale cells.
fn unstable_spills(previous,next) any^ {
    let prior=previous or {spill_values:{},spill_ranges:{},spill_errors:{},spill_display:{}}
    let changed={*:map([for (key,item in prior.spill_ranges where item!=next.spill_ranges[string(key)^]) *[string(key),true]]),
        *:map([for (key,item in next.spill_ranges where item!=prior.spill_ranges[string(key)^]) *[string(key),true]])}
    let values=map([for (key,item in next.spill_values where changed[address(item.row,item.col)^]!=true) *[string(key),item]]);
    {spill_values:values,
        spill_ranges:map([for (key,item in next.spill_ranges where changed[string(key)^]!=true) *[string(key),item]]),
        spill_errors:{*:next.spill_errors,*:map([for (key,item in changed) *[string(key),"#CYCLE!"]])},
        spill_display:map([for (key,item in values) *[string(key),spill_display_value(item.value)]])}
}
pub fn spills(sheet,previous=null,depth=0) any^ {
    let prior=previous or {spill_values:{},spill_ranges:{},spill_errors:{},spill_display:{}}
    let next=spill_pass({*:sheet,*:prior})^;
    if (next==prior) next else if (depth>=63) unstable_spills(prior,next)^ else spills(sheet,next,depth+1)^
}
// Recalculate all sheet spill maps together so cross-sheet references stabilize.
pub fn workbook_spills(book,indices,previous=null,depth=0) any^ {
    let prior=previous or {}
    let next=map([for (sheet in book.sheets) *[sheet.title,spill_pass({*:sheet,cell_index:indices[sheet.title],
        workbook:book,workbook_indices:indices,workbook_spills:prior,*:(prior[sheet.title] or {})})^]]);
    if (next==prior) next else if (depth>=63)
        map([for (sheet in book.sheets) *[sheet.title,unstable_spills(prior[sheet.title],next[sheet.title])^]])
    else workbook_spills(book,indices,next,depth+1)^
}
pub fn display(sheet,row,col) any^ => value(sheet,row,col) ^ {if (starts_with(string(^.message),"#")) ^.message else "#VALUE!"}
