// S12.1.3: workbook commands are pure; handlers own publication and I/O.
import f: .mod_formula
import grid: lambda.ui.core.virtual_grid
pub let MAX_ROWS=100000
pub let MAX_COLUMNS=1000
pub let MAX_MATERIALIZED_CELLS=1000000
pub fn empty_sheet(title="Sheet1") => {title:title,rows:60,cols:20,cells:[]}
pub fn blank() => {format:"lambda-sheet",version:1,title:"Untitled workbook",sheets:[empty_sheet()]}
pub fn current(st) => st.book.sheets[st.sheet]
// The cache belongs to the active workbook revision, never the saved file.
pub fn prepare(sheet) {
    let index=map([for (cell in sheet.cells) *[f.address(cell.row,cell.col),cell]])
    let indexed={*:sheet,cell_index:index};
    {cells:index,values:map([for (cell in sheet.cells) *[f.address(cell.row,cell.col),f.display(indexed,cell.row,cell.col)]])}
}
pub fn initial(book=null) {
    let initial_book=book or sample();
    refresh_stats({book:initial_book,saved:initial_book,sheet:0,row:4,col:4,anchor:{row:4,col:4},cache:prepare(initial_book.sheets[0]),
        viewport:{x:0.0,y:0.0,width:0.0,height:0.0},geometry_key:null,geometry:null,
        undo:[],redo:[],tab:"Home",editing:false,draft:"",dialog:"",path:"",dialog_path:"",error:null,pending:null,status:"Ready",grid:true,formulas:false,zoom:1.0,
        selection_drag:null,selection_click:false,resize_drag:null})
}
pub fn indexed(st) => {*:current(st),cell_index:st.cache.cells}
pub fn activate(st,index) => if (index==st.sheet) st else refresh_stats({*:st,sheet:index,cache:prepare(st.book.sheets[index])})
pub fn dimension_property(axis) => if (axis=="column") "column_widths" else "row_heights"
pub fn dimension_limits(axis) => if (axis=="column") {min:40,max:1000} else {min:27,max:500}
pub fn default_dimension(axis,index) => if (axis=="column") (if (index==0) 220 else if (index==1) 150 else 124)
    else if (index==0) 39 else 27
pub fn dimension(sheet,axis,index) {
    let value=sheet[dimension_property(axis)][string(index)];
    if (value==null) default_dimension(axis,index) else value
}
pub fn resizing(st,axis,index) => st.resize_drag!=null and st.resize_drag.axis==axis and st.resize_drag.index==index
// preview sizes leave the workbook and undo history unchanged until pointer release.
pub fn used_dimension(st,axis,index) any^ => if (resizing(st,axis,index)) st.resize_drag.value else dimension(current(st),axis,index)
pub fn valid_dimension(sheet,axis,index,value) => axis in ["column","row"] and index is int and index>=0 and
    index<(if (axis=="column") sheet.cols else sheet.rows) and value is number and
    not (value is nan or value is inf or value == -inf) and value>=dimension_limits(axis).min and value<=dimension_limits(axis).max
pub fn resize(st,axis,index,value) map^ {
    let sheet=current(st)
    if (not valid_dimension(sheet,axis,index,value)) raise error("Sheet: invalid row height or column width")
    // a manual row height constrains wrapped text even when it equals the default.
    else if (dimension(sheet,axis,index)==value and (axis=="column" or sheet.row_heights[string(index)]!=null)) st
    else {
        // sizes are saved before zoom, so changing zoom cannot alter authored geometry.
        let property=dimension_property(axis)
        let sizes={*:(sheet[property] or {}),*:map([string(index),value])};
        replace_sheet(st,{*:sheet,*:map([property,sizes])},if (axis=="column") "Column width updated" else "Row height updated")
    }
}
pub fn bounds(st) => grid.bounds(st)
pub fn range_size(b) => (b.bottom-b.top+1)*(b.right-b.left+1)
pub fn selected(st,row,col) => grid.selected(st,row,col)
pub fn active(st) any^ => f.cell(indexed(st),st.row,st.col)
pub fn select(st,row,col,extend=false) {
    let r = max(0,min(current(st).rows-1,row))
    let c = max(0,min(current(st).cols-1,col));
    refresh_stats({*:st,row:r,col:c,anchor:if (extend) st.anchor else {row:r,col:c},editing:false,draft:"",status:"Ready"})
}
pub fn change(st,book,status="Ready") => if (book==st.book) {*:st,editing:false} else
    refresh_stats({*:st,book:book,cache:prepare(book.sheets[st.sheet]),undo:take([st.book,*st.undo],100),redo:[],editing:false,status:status})
fn replace_sheet(st,sheet,status="Ready") => change(st,{*:st.book,sheets:[for (i,s in st.book.sheets) if (i==st.sheet) sheet else s]},status)
pub fn set_cells(st,cells,status="Ready") => replace_sheet(st,{*:current(st),cells:cells},status)
fn patch_cell(sheet,row,col,raw,style=null) => [for (cell in sheet.cells where cell.row!=row or cell.col!=col) cell,
    {row:row,col:col,raw:raw,style:style or f.cell(sheet,row,col).style or {}}]
// selecting another cell can blur an unchanged native draft without creating an edit.
pub fn set_value(st,text) any^ => if ((active(st).raw or "")==text) {*:st,editing:false}
    else set_cells(st,patch_cell(current(st),st.row,st.col,text),"Cell updated")
pub fn clear(st) => set_cells(st,[for (c in current(st).cells) if (selected(st,c.row,c.col)) {*:c,raw:""} else c],"Selection cleared")
pub fn format_cells(st,key,value) map^ {
    let b = bounds(st)
    // Formatting materializes empty cells, so bound the requested rectangle.
    if (range_size(b)>MAX_MATERIALIZED_CELLS) raise error("Sheet: selection is too large to format")
    else {
        let sheet=indexed(st)
        let cells = [for (r in b.top to b.bottom,c in b.left to b.right) {
            let old = f.cell(sheet,r,c);
            {row:r,col:c,raw:old.raw or "",style:{*:(old.style or {}),*:map([key,value])}}
        }];
        set_cells(st,[*[for (c in current(st).cells where ((not selected(st,c.row,c.col)) ^ { false })) c],*cells],"Formatting updated")
    }
}
pub fn history(st,redo=false) any^ {
    let stack = if (redo) st.redo else st.undo
    if (len(stack)==0) st else {
        let sheet=min(st.sheet,len(stack[0].sheets)-1)
        let rows=stack[0].sheets[sheet].rows
        let cols=stack[0].sheets[sheet].cols;
        refresh_stats({*:st,book:stack[0],sheet:sheet,cache:prepare(stack[0].sheets[sheet]),row:min(st.row,rows-1),col:min(st.col,cols-1),
            anchor:{row:min(st.anchor.row,rows-1),col:min(st.anchor.col,cols-1)},editing:false,
            undo:if (redo) [st.book,*st.undo] else slice(st.undo,1),
            redo:if (redo) slice(st.redo,1) else [st.book,*st.redo],status:if (redo) "Redone" else "Undone"})
    }
}
pub fn add_sheet(st) any^ {
    let title = "Sheet" ++ string(len(st.book.sheets)+1)
    let next = change(st,{*:st.book,sheets:[*st.book.sheets,empty_sheet(title)]},"Sheet added");
    select(activate(next,len(st.book.sheets))^,0,0)
}
pub fn paste(st,text) {
    let rows = split(replace(text,"\r",""),"\n")
    let incoming = [for (r,line in rows,c,raw in ((split(line,"\t")) ^ { [] }) where st.row+r<current(st).rows and st.col+c<current(st).cols)
        {row:st.row+r,col:st.col+c,raw:raw,style:f.cell(current(st),st.row+r,st.col+c).style or {}}]
    let positions = [for (c in incoming) f.address(c.row,c.col)];
    set_cells(st,[*[for (c in current(st).cells where ((not contains(positions,f.address(c.row,c.col))) ^ { false })) c],*incoming],"Pasted cells")
}
pub fn copy_text(st) string^ {
    let b=bounds(st)
    if (range_size(b)>MAX_MATERIALIZED_CELLS) raise error("Sheet: selection is too large to copy")
    else {
        let sheet=indexed(st);
        join([for (r in b.top to b.bottom)
            join([for (c in b.left to b.right) f.cell(sheet,r,c).raw or ""],"\t")],"\n")
    }
}
fn calculate_stats(st) {
    let b=bounds(st)
    // Sparse selections can span millions of empty cells without visiting them.
    let values=[for (cell in current(st).cells where cell.row>=b.top and cell.row<=b.bottom and cell.col>=b.left and cell.col<=b.right)
        st.cache.values[f.address(cell.row,cell.col)]]
    let numbers=[for (v in values where v is number) v];
    {count:len([for (v in values where v!="") v]),sum:sum(numbers),average:if (len(numbers)>0) sum(numbers)/len(numbers) else 0}
}
// Scroll-only renders reuse the last selection's aggregates.
fn refresh_stats(st) => {*:st,selection_stats:calculate_stats(st)}
pub fn stats(st) => st.selection_stats
fn sum_start(sheet,row,col) any^ => if (row<0 or not (f.display(sheet,row,col) is number)) row+1 else sum_start(sheet,row-1,col)
pub fn autosum(st) any^ {
    let start=sum_start(current(st),st.row-1,st.col)
    if (start==st.row) {*:st,status:"Select an empty cell below a column of numbers"}
    else set_value(st,"=SUM(" ++ f.address(start,st.col) ++ ":" ++ f.address(st.row-1,st.col) ++ ")")
}
pub fn sort_rows(st,descending=false) map^ {
    let b=bounds(st)
    if (range_size(b)>MAX_MATERIALIZED_CELLS) raise error("Sheet: selection is too large to sort")
    else {
        let sheet=indexed(st)
        let rows=sort(b.top to b.bottom,{dir:if (descending) 'desc' else 'asc',by:(r)any^ =>f.display(sheet,r,st.col)})
        let moved=[for (r,source in rows,c in b.left to b.right) {
            let cell=f.cell(sheet,source,c);{row:b.top+r,col:c,raw:cell.raw or "",style:cell.style or {}}
        }];
        set_cells(st,[*[for (c in current(st).cells where ((not selected(st,c.row,c.col)) ^ { false })) c],*moved],"Selection sorted")
    }
}
pub fn sample() {
    let rows = [
        ["NORTHSTAR / OPERATING PLAN"],
        ["Q1 2027 · Team budget", "", "", "", "", "", "Currency: USD"],
        [],
        ["Department","Owner","January","February","March","Q1 total","Budget","Variance"],
        ["Engineering","Alex Chen","42500","44000","46500","=SUM(C5:E5)","140000","=G5-F5"],
        ["Design","Sam Rivera","18200","19500","20100","=SUM(C6:E6)","60000","=G6-F6"],
        ["Marketing","Morgan Lee","24000","28500","31000","=SUM(C7:E7)","90000","=G7-F7"],
        ["Operations","Jamie Park","15600","15900","16200","=SUM(C8:E8)","50000","=G8-F8"],
        ["Customer success","Taylor Kim","12800","13600","14500","=SUM(C9:E9)","45000","=G9-F9"],
        ["Research","Casey Wu","9500","11000","12500","=SUM(C10:E10)","36000","=G10-F10"],
        ["TOTAL","","=SUM(C5:C10)","=SUM(D5:D10)","=SUM(E5:E10)","=SUM(F5:F10)","=SUM(G5:G10)","=G11-F11"],
        [],["AT A GLANCE"],
        ["Monthly average","=AVERAGE(C11:E11)"],
        ["Budget utilization","=F11/G11"],
        [],["Make the numbers yours."],
        ["Double-click a cell to edit. Formulas recalculate as you work."],
        ["Try =SUM(C5:E5), select a range with Shift, or add a new sheet."]
    ]
    let cells=[for (r,row in rows,c,raw in row where raw!="") {row:r,col:c,raw:raw,style:
        if (r==0) {bold:true,color:"#185b42",size:18}
        else if (r==3) {bold:true,fill:"#217346",color:"#ffffff"}
        else if (r==10) {bold:true,fill:"#e1eee6",format:if (c>=2) "currency" else "general"}
        else if (r>=4 and r<=9) {fill:if (r%2==0) "#f1f7f3" else "#ffffff",format:if (c>=2) "currency" else "general"}
        else if (r==14 and c==1) {bold:true,format:"percent",color:"#217346"}
        else if (r==13 and c==1) {bold:true,format:"currency"}
        else if (r==12 or r==16) {bold:true,color:"#217346"}
        else {color:"#758078"}}];
    {format:"lambda-sheet",version:1,title:"Northstar · Q1 operating plan",sheets:[{*:empty_sheet("Budget"),cells:cells},empty_sheet("Forecast"),empty_sheet("Notes")]}
}
