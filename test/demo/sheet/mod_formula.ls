// Formula syntax compiles into callable Lambda closures (S12.3.4v2).
import cells: lambda.doc.xlsx.cells
import ops: .mod_formula_functions
import c: .mod_formula_common
import dates: .mod_formula_date
import lookup: .mod_formula_lookup
// Carry the cache even when a formula fails and IFERROR continues evaluation.
fn outcome(samples,value: any | error) => if (value is error) {samples:samples,failure:value,failed:true}
    else {samples:samples,value:value,failed:false}
fn unwrap(result) any^ => if (result.failed) raise result.failure else result.value
type FormulaLambda = pn (packet) -> any^ @[clock, random]
function run_lambda(target: FormulaLambda,packet) any^ => target(packet)^
type FormulaPlan = pn (sheet, path, samples) -> any^ @[clock, random]
function run_plan(plan: FormulaPlan,sheet,path,samples) any^ =>
    plan(sheet,path,samples) ^ {outcome(samples,^)}
let LETTERS = "ABCDEFGHIJKLMNOPQRSTUVWXYZ"
fn letter(ch) any^ => ch!="" and contains(LETTERS,upper(ch))
fn digit(ch) any^ => ch!="" and contains("0123456789",ch)
pub fn column(col) any^ => cells.column(col)^
pub fn address(row,col) any^ => cells.address(row,col)^
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
        if (row<0 or row>=cells.ROW_LIMIT or col<0 or col>=cells.COLUMN_LIMIT) raise error("#REF!") else {row:row,col:col}
    }
}
// A prepared sheet supplies an address index so visible cells and references avoid scans.
pub fn index_cells(sheet) => map([for (cell in sheet.cells) *[address(cell.row,cell.col),cell]])
pub fn indexed_cell(sheet,key) => sheet.cell_index[key]
pub fn cell(sheet,row,col) any^ => if (sheet.cell_index!=null) indexed_cell(sheet,address(row,col)^)
    else [for (c in sheet.cells where c.row==row and c.col==col) c][0]

// References use the same calculation as their caller, including across sheets.
function reference(sheet,key,path,samples) any^ {
    let p=position(key)^
    if (p.row>=sheet.rows or p.col>=sheet.cols) raise error("#REF!") else {
        let source=cell(sheet,p.row,p.col)
        let result=if (sheet.reference_only==true) outcome(samples,null) else cell_value(sheet,p.row,p.col,path,samples)^;
        if (result.failed) result else outcome(result.samples,
            {kind:"cellref",value:result.value,blank:source==null or source.raw=="",row:p.row,col:p.col,title:sheet.title})
    }
}
function range_values(sheet,row,col,rows,cols,path,samples) any^ {
    var values=fill(rows*cols,null)
    var current=samples
    var i=0
    while (i<rows*cols) {
        let result=cell_value(sheet,row+i div cols,col+i%cols,path,current)^
        if (result.failed) { return result }
        current=result.samples
        values[i]=result.value
        i=i+1
    }
    return outcome(current,values)
}
function range_value(sheet,start,end,path,samples) any^ {
    let a=position(start)^
    let b=position(end)^
    if (max(a.row,b.row)>=sheet.rows or max(a.col,b.col)>=sheet.cols) raise error("#REF!")
    else {
        let rows=abs(a.row-b.row)+1
        let cols=abs(a.col-b.col)+1;
        if (rows*cols>1000000) raise error("#VALUE!") else {
            let result=if (sheet.reference_only==true) outcome(samples,[]) else
                range_values(sheet,min(a.row,b.row),min(a.col,b.col),rows,cols,path,samples)^;
            if (result.failed) result else outcome(result.samples,
                {kind:"range",rows:rows,cols:cols,row:min(a.row,b.row),col:min(a.col,b.col),title:sheet.title,values:result.value})
        }
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
            {*:target,reference_only:sheet.reference_only,workbook:sheet.workbook,workbook_indices:sheet.workbook_indices,workbook_spills:sheet.workbook_spills,
                *:(sheet.workbook_spills[target.title] or {}),cell_index:if (existing==null) index_cells(target) else existing}
        }
    }
}
fn constant_plan(v) {
    pn plan(sheet,path,samples) any^ @[clock, random] { outcome(samples,v) }
    plan
}
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
        if (row<0 or row>=cells.ROW_LIMIT or col<0 or col>=cells.COLUMN_LIMIT) raise error("#REF!") else address(row,col)^
    }
}
fn binding_name(key) => if ((starts_with(key,"_XLPM.")) ^ {false}) (slice(key,6) ^ {key}) else key
fn reference_plan(key,title=null) {
    pn plan(sheet,path,samples) any^ @[clock, random] {
        if (title==null and sheet.formula_bindings[binding_name(key)]!=null) outcome(samples,sheet.formula_bindings[binding_name(key)].value)
        else reference(target_sheet(sheet,title)^,translated_reference(key,sheet.conditional_offset)^,path,samples)^
    }
    plan
}
fn spill_plan(key,title=null) {
    pn plan(sheet,path,samples) any^ @[clock, random] {
        let target=target_sheet(sheet,title)^
        let translated=translated_reference(key,sheet.conditional_offset)^
        let source=target.spill_ranges[upper(replace(translated,"$",""))];
        if (source==null) raise error("#REF!") else outcome(samples,source)
    }
    plan
}
fn range_plan(start,end,title=null) {
    pn plan(sheet,path,samples) any^ @[clock, random] {
        range_value(target_sheet(sheet,title)^,translated_reference(start,sheet.conditional_offset)^,
        translated_reference(end,sheet.conditional_offset)^,path,samples)^
    }
    plan
}
fn value_context(sheet) => if (sheet.reference_only==true) {*:sheet,reference_only:false} else sheet
function arguments_value(plans,sheet,path,samples,mode="value",start=0,end=null) any^ {
    let stop=if (end==null) len(plans) else end
    var current=samples
    var values=fill(stop-start,null)
    var i=start
    while (i<stop) {
        let reference_only=mode=="reference" or mode=="aggregate" or (mode=="offset" and i==0)
        let context=if (reference_only) {*:sheet,reference_only:true} else value_context(sheet)
        // XLOOKUP evaluates its fallback only after the lookup misses.
        let result=if (mode=="xlookup" and i==3) outcome(current,null) else run_plan(plans[i],context,path,current)^
        if (result.failed and mode!="aggregate") { return result }
        current=result.samples
        values[i-start]=if (result.failed) {kind:"excel_error",message:result.failure.message} else result.value
        i=i+1
    }
    return outcome(current,values)
}
fn scalar(value) => if (value is map and value.kind=="cellref") value.value else value
// Split the cache by sheet and row blocks so inserting a sample copies only its path.
fn sample_path(sheet) => [lower(sheet.title or ""),string(sheet.formula_row div 256),
    string(sheet.formula_row%256),string(sheet.formula_col),sheet.call_scope or ""]
fn cached_sample(cache,keys,index=0) any^ => if (index==len(keys) or cache==null) cache
    else cached_sample(cache[keys[index]]^,keys,index+1)^
fn store_sample(cache,keys,reading,index=0) any^ => if (index==len(keys)) reading
    else { *:(cache or {}), *:map([keys[index],store_sample(cache[keys[index]]^,keys,reading,index+1)^]) }
function volatile_value(name,sheet,samples) any^ {
    let keys=sample_path(sheet)
    let stored=if (sheet.conditional_mode==true) null else cached_sample(samples,keys)^
    if (stored!=null) {*:stored,samples:samples} else {
        let reading=if (name=="RAND" or name=="RANDOM") (c.random_sample() ^ {^})
            else (dates.current_value(sheet.date1904==true,name=="TODAY",c.clock_sample()) ^ {^});
        if (sheet.conditional_mode==true) outcome(samples,reading)
        else {
            // Cache the reading itself so subsequent formulas can add to the current cache.
            let entry=if (reading is error) {failed:true,failure:reading} else {failed:false,value:reading};
            {*:entry,samples:store_sample(samples,keys,entry)^}
        }
    }
}
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
            else (ops.dispatch(target,if (len(args)==1) [a] else [a,b]) ^ {{kind:"excel_error",message:^.message}})];
        {kind:"array",rows:rows,cols:cols,values:values}
    }
}
// Lazy choices execute the selected plan with the cache left by the condition.
function select_branch(name,plans,sheet,path,samples,index=0,selector=null) any^ {
    if (index>=len(plans)) raise error("#N/A")
    else if (name=="SWITCH" and index==len(plans)-1) run_plan(plans[index],sheet,path,samples)^
    else {
        let test=run_plan(plans[index],value_context(sheet),path,samples)^;
        if (test.failed) test else {
            let matched=(if (name=="SWITCH") c.equal(selector,scalar(test.value)) else c.truth(scalar(test.value))) ^ {^};
            if (matched is error) outcome(test.samples,matched)
            else if (matched) run_plan(plans[index+1],sheet,path,test.samples)^
            else select_branch(name,plans,sheet,path,test.samples,index+2,selector) ^ {outcome(test.samples,^)}
        }
    }
}
function conditional_call(name,plans,sheet,path,samples) any^ {
    if (name=="IFS") {
        if (len(plans)%2!=0) raise error("#VALUE!") else select_branch(name,plans,sheet,path,samples)^
    } else {
        let first=run_plan(plans[0],value_context(sheet),path,samples)^;
        if (name=="ISERROR") outcome(first.samples,first.failed)
        else if (name=="ISNA") outcome(first.samples,first.failed and first.failure.message=="#N/A")
        else if (name=="IFERROR" or name=="IFNA") {
            if (first.failed and (name=="IFERROR" or first.failure.message=="#N/A"))
                run_plan(plans[1],sheet,path,first.samples)^ else first
        }
        else if (first.failed) first
        else if (name=="SWITCH")
            select_branch(name,plans,sheet,path,first.samples,1,scalar(first.value)) ^ {outcome(first.samples,^)}
        else if (name=="CHOOSE") {
            let choice=trunc(c.numeric(scalar(first.value))^) ^ {^};
            if (choice is error) outcome(first.samples,choice)
            else if (choice<1 or choice>=len(plans)) outcome(first.samples,error("#VALUE!"))
            else run_plan(plans[choice],sheet,path,first.samples)^
        }
        else {
            let test=c.truth(scalar(first.value)) ^ {^};
            if (test is error) outcome(first.samples,test) else {
                let branch=if (test) 1 else 2
                let result=if (branch==len(plans)) outcome(first.samples,false) else
                    run_plan(plans[branch],sheet,path,first.samples)^;
                if (result.failed or result.value!=null) result else outcome(result.samples,0)
            }
        }
    }
}
// Filtering happens before evaluation so hidden rows and nested aggregates can be ignored.
function aggregate_values(sheet,path,refs,options,samples) any^ {
    var current=samples
    var groups=fill(len(refs),null)
    var i=0
    while (i<len(refs)) {
        let ref=refs[i]
        if (ref is map and contains(["cellref","range"],ref.kind)^) {
            let target=target_sheet(sheet,ref.title)^
            let rows=if (ref.kind=="range") ref.rows else 1
            let cols=if (ref.kind=="range") ref.cols else 1
            let coords=[for (r in ref.row to ref.row+rows-1,c in ref.col to ref.col+cols-1,
                let raw=upper(trim(cell(target,r,c).raw or ""))
                where not (options.hidden and contains(target.hidden_rows or [],r)^) and
                    not (options.nested and (starts_with(raw,"=SUBTOTAL(")^ or starts_with(raw,"=AGGREGATE(")^))) [r,c]]
            var values=fill(len(coords),null)
            var j=0
            while (j<len(coords)) {
                let coord=coords[j]
                let result=cell_value(target,coord[0],coord[1],path,current)^
                current=result.samples
                if (result.failed and not options.errors) { return result }
                values[j]=if (result.failed) null else result.value
                j=j+1
            }
            groups[i]=values
        } else {
            let values=if (c.is_array_value(ref)) ref.values else [ref]
            let errors=[for (value in values where c.cell_error(value)^) value]
            if (len(errors)>0 and not options.errors) { return outcome(current,error(errors[0].message)) }
            groups[i]=[for (value in values where not c.cell_error(value)^) value]
        }
        i=i+1
    }
    return outcome(current,[for (group in groups,value in group) value])
}
function aggregate_call(name,plans,sheet,path,samples) any^ {
    let subtotal=name=="SUBTOTAL"
    let leading=arguments_value(plans,sheet,path,samples,"value",0,if (subtotal) 1 else 2)^;
    if (leading.failed) leading else {
        let options=ops.aggregate_options(subtotal,scalar(leading.value[0]),scalar(leading.value[1])) ^ {^};
        if (options is error) outcome(leading.samples,options) else {
            let has_k=options.base>=14
            let end=len(plans)-(if (has_k) 1 else 0)
            if (has_k and end!=3) outcome(leading.samples,error("#VALUE!")) else {
                let refs=arguments_value(plans,sheet,path,leading.samples,if (subtotal) "reference" else "aggregate",
                    if (subtotal) 1 else 2,end)^;
                if (refs.failed) refs else {
                    let k=if (has_k) run_plan(plans[end],sheet,path,refs.samples)^ else outcome(refs.samples,null);
                    if (k.failed) k else if (has_k and scalar(k.value)==null) outcome(k.samples,error("#VALUE!")) else {
                        let values=aggregate_values(sheet,path,refs.value,options,k.samples) ^ {outcome(k.samples,^)};
                        if (values.failed) values else
                            outcome(values.samples,ops.aggregate_calculate(options.base,values.value,scalar(k.value)) ^ {^})
                    }
                }
            }
        }
    }
}
function invoke(name,plans,sheet,path,samples) any^ {
    let basic=if (starts_with(name,"_XLFN.")) slice(name,6) else name
    let resolved=if (starts_with(basic,"_XLWS.")) slice(basic,6) else basic
    let entry=ops.FUNCTIONS[resolved];
    if (entry==null and sheet.formula_bindings[binding_name(resolved)]==null and
        (sheet.workbook==null or len(sheet.workbook.defined_names or [])==0)) raise error("#NAME?")
    else if (entry==null) {
        let binding=sheet.formula_bindings[binding_name(resolved)]
        let func=if (binding!=null) outcome(samples,binding.value) else defined_lambda(sheet,resolved,path,samples)^;
        if (func.failed) func else if (func.value==null or func.value.kind!="lambda") outcome(func.samples,error("#NAME!"))
        else {
            let args=arguments_value(plans,sheet,path,func.samples)^;
            if (args.failed) args else
                invoke_lambda(func.value,[for (arg in args.value) scalar(arg)],sheet,args.samples) ^ {outcome(args.samples,^)}
        }
    }
    else if (len(plans)<entry.min or len(plans)>entry.max) raise error("#VALUE!")
    else if (contains(["TODAY","NOW","RAND","RANDOM"],resolved)) volatile_value(resolved,sheet,samples)^
    else if (entry.lazy=="subtotal" or entry.lazy=="aggregate") aggregate_call(resolved,plans,sheet,path,samples)^
    else if (entry.lazy in ["if","iferror","inspect","thunks","selector"]) conditional_call(resolved,plans,sheet,path,samples)^
    else {
        let mode=if (resolved=="OFFSET") "offset" else if (entry.lazy=="xlookup") "xlookup"
            else if (entry.reference_context==true and entry.references==true) "reference" else "value"
        let result=arguments_value(plans,sheet,path,samples,mode)^;
        if (result.failed) result else {
            let args=result.value;
            if (entry.reference_resolver==true) {
                let descriptor=(if (resolved=="INDIRECT") ops.xl_indirect(args[0],if (len(args)>1) args[1] else true)
                    else ops.xl_offset(args[0],args[1],args[2],args[3],args[4])) ^ {^};
                if (descriptor is error) outcome(result.samples,descriptor)
                else resolve_reference(sheet,path,descriptor,result.samples) ^ {outcome(result.samples,^)}
            }
            else if (entry.reference_context==true) {
                let value=(if (resolved=="ADDRESS") ops.xl_address(column,scalar(args[0]),scalar(args[1]),
                    if (len(args)>2) scalar(args[2]) else 1,if (len(args)>3) scalar(args[3]) else true,scalar(args[4]))
                    else ops.dispatch(entry.target,[{row:sheet.formula_row,col:sheet.formula_col,column_name:column},
                        *[for (arg in args) if (entry.references==true) arg else scalar(arg)]])) ^ {^};
                outcome(result.samples,value)
            }
            else if (mode=="xlookup") {
                let normalized=[for (arg in args) scalar(arg)]
                let found=lookup.xlookup_index(normalized[0],normalized[1],normalized[2],
                    if (len(args)>4) normalized[4] else 0,if (len(args)>5) normalized[5] else 1) ^ {^};
                if (found is error) outcome(result.samples,found)
                else if (found!=null) outcome(result.samples,normalized[2].values[found])
                else if (len(args)<4) outcome(result.samples,error("#N/A")) else {
                    let fallback=run_plan(plans[3],sheet,path,result.samples)^;
                    if (not fallback.failed and fallback.value==null) outcome(fallback.samples,error("#N/A")) else fallback
                }
            }
            else {
                let normalized=if (entry.references==true) args else [for (arg in args) scalar(arg)]
                let value=(if (contains(["+","-","*","/","^","%","u+","u-","&","=","<>","<",">","<=",">="],resolved) and
                    any([for (arg in normalized) c.is_array_value(arg)])) array_operator(entry.target,normalized)
                    else ops.dispatch(entry.target,if (entry.date_context==true) [sheet.date1904==true,*normalized] else normalized)) ^ {^};
                outcome(result.samples,value)
            }
        }
    }
}
// Only cell/range references are accepted from INDIRECT; arbitrary text is never run as code.
function resolve_reference(sheet,path,descriptor,samples) any^ {
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
            if (has_range) range_value(target,start,t[at+2],path,samples)^ else reference(target,start,path,samples)^
        }
    } else {
        let target=target_sheet(sheet,descriptor.title)^
        let first=address(descriptor.row,descriptor.col)^
        let final=address(descriptor.row+descriptor.rows-1,descriptor.col+descriptor.cols-1)^;
        if (descriptor.rows==1 and descriptor.cols==1) reference(target,first,path,samples)^
        else range_value(target,first,final,path,samples)^
    }
}
fn call_plan(name,plans,site=null) {
    pn plan(sheet,path,samples) any^ @[clock, random] {
        let context=if (site==null) sheet else {*:sheet,call_scope:(sheet.call_scope or "") ++ "/" ++ string(site)};
        invoke(name,plans,context,path,samples)^
    }
    plan
}

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
function let_run(bindings,body,sheet,path,samples,index=0) any^ {
    if (index>=len(bindings)) run_plan(body,sheet,path,samples)^ else {
        let binding=bindings[index]
        let result=run_plan(binding.plan,value_context(sheet),path,samples)^
        let context={*:sheet,formula_bindings:{*:(sheet.formula_bindings or {}),*:map([binding.name,{value:result.value}])}};
        if (result.failed) result else let_run(bindings,body,context,path,result.samples,index+1)^
    }
}
fn let_plan(bindings,body) {
    pn plan(sheet,path,samples) any^ @[clock, random] { let_run(bindings,body,sheet,path,samples)^ }
    plan
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
function invoke_lambda(func,args,sheet,samples) any^ {
    if (len(args)!=len(func.params)) raise error("#VALUE!")
    else run_lambda(func.target,{values:args,call_scope:sheet.call_scope,samples:samples})^
}
fn lambda_plan(params,body) {
    pn plan(sheet,path,samples) any^ @[clock, random] {
        pn target(packet) any^ @[clock, random] {
            let bindings=map([for (i,param in params) *[param,{value:packet.values[i]}]])
            let context={*:sheet,call_scope:packet.call_scope,lambda_depth:(sheet.lambda_depth or 0)+1,formula_bindings:{*:(sheet.formula_bindings or {}),*:bindings}};
            if (context.lambda_depth>64) raise error("#NUM!") else run_plan(body,context,path,packet.samples)^
        }
        outcome(samples,{kind:"lambda",params:params,target:target})
    }
    plan
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
function defined_lambda(sheet,name,path,samples) any^ {
    if (sheet.workbook==null) { return outcome(samples,null) }
    let definitions=sheet.workbook.defined_names or []
    let index=[for (i,item in (sheet.workbook.sheets or []) where lower(item.title)==lower(sheet.title or "")) i][0]
    let matching=[for (entry in definitions where upper(entry.name)==name and (entry.scope==null or entry.scope==index)) entry]
    let scoped=[for (entry in matching where entry.scope!=null) entry]
    let chosen=if (len(scoped)>0) scoped[0] else matching[0];
    if (chosen==null) outcome(samples,null) else {
        let raw=if (starts_with(chosen.formula,"=")) slice(chosen.formula,1) else chosen.formula
        let t=tokens(raw)^
        let plan=expression(t,0,0)^;
        if (plan.at!=len(t)) raise error("#VALUE!") else run_plan(plan.plan,sheet,path,samples)^
    }
}
fn apply_plan(target_plan,params,site) {
    pn plan(sheet,path,samples) any^ @[clock, random] {
        let func=run_plan(target_plan,sheet,path,samples)^;
        if (func.failed) func else {
            let args=arguments_value(params,sheet,path,func.samples)^;
            if (args.failed) args else if (not (func.value is map) or func.value.kind!="lambda")
                outcome(args.samples,error("#VALUE!"))
            else invoke_lambda(func.value,[for (arg in args.value) scalar(arg)],
                {*:sheet,call_scope:(sheet.call_scope or "") ++ "/" ++ string(site)},args.samples) ^ {outcome(args.samples,^)}
        }
    }
    plan
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
        {plan:call_plan(t[at],args.plans,at),at:args.at}
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
        call_suffix(t,{plan:apply_plan(left.plan,args.plans,left.at),at:args.at},depth+1)^
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
function evaluate(sheet,raw,path,samples,allow_array=false) any^ {
    let t=tokens(slice(raw,1))^
    let parsed=expression(t,0,0)^;
    if (parsed.at!=len(t)) raise error("#VALUE!") else {
        let result=run_plan(parsed.plan,sheet,path,samples)^;
        if (result.failed) result else {
            let calculated=scalar(result.value);
            if (calculated is map and calculated.kind=="lambda") outcome(result.samples,error("#CALC!"))
            else if (not allow_array and (calculated is array or c.is_array_value(calculated)))
                outcome(result.samples,error("#VALUE!"))
            else if (calculated is nan or calculated is inf or calculated == -inf) outcome(result.samples,error("#NUM!"))
            else outcome(result.samples,calculated)
        }
    }
}
// Template formulas take fresh samples and share the worksheet readings reached by their references.
pub function conditional_result(sheet,formula,anchor,row,col,samples) any^ {
    let source=if (starts_with(formula,"=")) formula else "=" ++ formula
    let context={*:sheet,conditional_mode:true,call_scope:"",formula_row:row,formula_col:col,
        conditional_offset:{row:row-anchor.row,col:col-anchor.col}};
    evaluate(context,source,[],samples) ^ {outcome(samples,^)}
}
pub function conditional(sheet,formula,anchor,row,col) any^ =>
    unwrap(conditional_result(sheet,formula,anchor,row,col,sheet.samples)^)^
// Literal cells do not need formula parsing or dependency tracking.
pub fn literal_value(raw) any^ {
    if (starts_with(raw,"'")) slice(raw,1)
    else if (trim(raw)=="") ""
    else { let parsed=float(raw) ^ {raw}; if (parsed is nan or parsed is inf or parsed == -inf) raw else parsed }
}
function evaluate_cell(sheet,row,col,path,samples) any^ {
    let cell_address=address(row,col)^
    let key=lower(sheet.title or "") ++ "!" ++ cell_address
    let source=cell(sheet,row,col)^
    let raw=source.raw or ""
    let spill=sheet.spill_values[cell_address];
    if (raw=="" and spill!=null) {
        if (contains(path,spill.key)) outcome(samples,error("#CYCLE!"))
        else outcome(samples,c.checked(spill.value) ^ {^})
    }
    else if (contains(path,key) or len(path)>64) outcome(samples,error("#CYCLE!"))
    else if (starts_with(raw,"=")) {
        let context={*:sheet,conditional_mode:false,conditional_offset:null,reference_only:false,formula_bindings:null,
            formula_row:row,formula_col:col,call_scope:""}
        let result=evaluate(context,raw,[*path,key],samples,true) ^ {outcome(samples,^)};
        // Imported XLSX results can serve as a display fallback until edited.
        if (result.failed and source.has_cached==true and contains(["#NAME?","#VALUE!","#REF!"],result.failure.message))
            outcome(result.samples,source.cached)
        else result
    }
    else outcome(samples,literal_value(raw) ^ {^})
}
function cell_value(sheet,row,col,path,samples) any^ {
    let spill_error=sheet.spill_errors[address(row,col)^];
    if (spill_error!=null) outcome(samples,error(spill_error)) else {
        let result=evaluate_cell(sheet,row,col,path,samples)^;
        if (result.failed) result else {
            let value=if (c.is_array_value(result.value)) result.value.values[0] else result.value;
            outcome(result.samples,c.checked(value) ^ {^})
        }
    }
}
pub function raw_value(sheet,row,col,path=[]) any^ => unwrap(evaluate_cell(sheet,row,col,path,sheet.samples)^)^
pub function value(sheet,row,col,path=[]) any^ => unwrap(cell_value(sheet,row,col,path,sheet.samples)^)^
fn display_value(result) => if (not result.failed) result.value
    else if ((starts_with(string(result.failure.message),"#") ^ {false})) result.failure.message else "#VALUE!"
// One batch shares the samples taken by its roots and all their dependencies.
pub function calculate_values(sheet,samples) any^ {
    var current=samples
    var values=fill(len(sheet.cells),null)
    var i=0
    while (i<len(sheet.cells)^) {
        let source=sheet.cells[i]
        let result=cell_value(sheet,source.row,source.col,[],current)^
        current=result.samples
        values[i]=display_value(result)
        i=i+1
    }
    return {samples:current,values:map([*[for (i,source in sheet.cells) *[address(source.row,source.col)^,values[i]]],
        *[for (key,item in sheet.spill_display) *[string(key),item]]])}
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
function spill_pass(sheet,samples) any^ {
    var current=samples
    let sources=[for (source in sheet.cells where may_spill(source.raw,sheet.workbook)^) source]
    var evaluated=fill(len(sources),null)
    var i=0
    while (i<len(sources)) {
        let source=sources[i]
        let result=evaluate_cell(sheet,source.row,source.col,[],current)^
        current=result.samples
        if (not result.failed and c.is_array_value(result.value)^) {
            evaluated[i]={row:source.row,col:source.col,result:result.value}
        }
        i=i+1
    }
    let candidates=[for (candidate in evaluated where candidate!=null) candidate]
    let blocked=[for (candidate in candidates where spill_blocked(sheet,candidates,candidate)^) candidate]
    let allowed=[for (candidate in candidates where not spill_blocked(sheet,candidates,candidate)^) candidate]
    let errors=map([for (candidate in blocked) *[address(candidate.row,candidate.col),"#SPILL!"]])
    let ranges=map([for (candidate in allowed) *[address(candidate.row,candidate.col),candidate.result]])
    let values=map([for (candidate in allowed,r in 0 to candidate.result.rows-1,c in 0 to candidate.result.cols-1
        where r!=0 or c!=0) *[address(candidate.row+r,candidate.col+c),
        {value:candidate.result.values[r*candidate.result.cols+c],row:candidate.row,col:candidate.col,key:lower(sheet.title or "") ++ "!" ++ address(candidate.row,candidate.col)}]]);
    return {samples:current,spills:{spill_values:values,spill_ranges:ranges,spill_errors:errors,
        spill_display:map([for (candidate in allowed,r in 0 to candidate.result.rows-1,c in 0 to candidate.result.cols-1
            where r!=0 or c!=0) *[address(candidate.row+r,candidate.col+c),
                spill_display_value(candidate.result.values[r*candidate.result.cols+c])]])}}
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
pub function spills(sheet,samples=null,previous=null,depth=0) any^ {
    let prior=previous or {spill_values:{},spill_ranges:{},spill_errors:{},spill_display:{}}
    let pass=spill_pass({*:sheet,*:prior},samples)^;
    if (pass.spills==prior) pass
    else if (depth>=63) {samples:pass.samples,spills:unstable_spills(prior,pass.spills)^}
    else spills(sheet,pass.samples,pass.spills,depth+1)^
}
// Spill geometry uses a workbook snapshot. Volatile calls retain their results across passes.
pub function workbook_spills(book,indices,samples=null,previous=null,depth=0) any^ {
    let prior=previous or {}
    var current=samples
    var results=fill(len(book.sheets),null)
    var i=0
    while (i<len(book.sheets)^) {
        let sheet=book.sheets[i]
        let base={*:sheet,cell_index:indices[sheet.title],workbook:book,workbook_indices:indices,
            workbook_spills:prior,*:(prior[sheet.title] or {})}
        let pass=spill_pass(base,current)^
        current=pass.samples
        results[i]=pass.spills
        i=i+1
    }
    let next=map([for (i,sheet in book.sheets) *[sheet.title,results[i]]])
    if (next==prior) { return {spills:next,samples:current} }
    if (depth>=63) { return {samples:current,spills:map([for (sheet in book.sheets)
        *[sheet.title,unstable_spills(prior[sheet.title],next[sheet.title])^]])} }
    return workbook_spills(book,indices,current,next,depth+1)^
}
pub function display(sheet,row,col) any^ {
    let address_value=address(row,col)^;
    if (sheet.calculated_values[address_value]!=null) sheet.calculated_values[address_value]
    else display_value(cell_value(sheet,row,col,[],sheet.samples)^)
}
