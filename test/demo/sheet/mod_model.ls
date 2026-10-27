// S12.1.3: workbook commands are pure; handlers own publication and I/O.
import f: .mod_formula
pub fn empty_sheet(title="Sheet1") => {title:title,rows:60,cols:20,cells:[]}
pub fn blank() => {format:"lambda-sheet",version:1,title:"Untitled workbook",sheets:[empty_sheet()]}
pub fn current(st) => st.book.sheets[st.sheet]
pub fn initial(book=null) => {book:book or sample(),saved:book or sample(),sheet:0,row:4,col:4,anchor:{row:4,col:4},
    undo:[],redo:[],tab:"Home",editing:false,draft:"",dialog:"",path:"",dialog_path:"",error:null,pending:null,status:"Ready",grid:true,formulas:false,zoom:1.0}
pub fn bounds(st) => {top:min(st.row,st.anchor.row),bottom:max(st.row,st.anchor.row),left:min(st.col,st.anchor.col),right:max(st.col,st.anchor.col)}
pub fn selected(st,row,col) => {let b=bounds(st);row>=b.top and row<=b.bottom and col>=b.left and col<=b.right}
pub fn active(st) => f.cell(current(st),st.row,st.col)
pub fn select(st,row,col,extend=false) {
    let r = max(0,min(current(st).rows-1,row))
    let c = max(0,min(current(st).cols-1,col));
    {*:st,row:r,col:c,anchor:if (extend) st.anchor else {row:r,col:c},editing:false,draft:"",status:"Ready"}
}
pub fn change(st,book,status="Ready") => if (book==st.book) {*:st,editing:false} else
    {*:st,book:book,undo:take([st.book,*st.undo],100),redo:[],editing:false,status:status}
fn replace_sheet(st,sheet,status="Ready") => change(st,{*:st.book,sheets:[for (i,s in st.book.sheets) if (i==st.sheet) sheet else s]},status)
pub fn set_cells(st,cells,status="Ready") => replace_sheet(st,{*:current(st),cells:cells},status)
fn patch_cell(sheet,row,col,raw,style=null) => [for (cell in sheet.cells where cell.row!=row or cell.col!=col) cell,
    {row:row,col:col,raw:raw,style:style or f.cell(sheet,row,col).style or {}}]
pub fn set_value(st,text) => set_cells(st,patch_cell(current(st),st.row,st.col,text),"Cell updated")
pub fn clear(st) => set_cells(st,[for (c in current(st).cells) if (selected(st,c.row,c.col)) {*:c,raw:""} else c],"Selection cleared")
pub fn format_cells(st,key,value) {
    let b = bounds(st)
    let cells = [for (r in b.top to b.bottom,c in b.left to b.right) {
        let old = f.cell(current(st),r,c);
        {row:r,col:c,raw:old.raw or "",style:{*:(old.style or {}),*:map([key,value])}}
    }];
    set_cells(st,[*[for (c in current(st).cells where not selected(st,c.row,c.col)) c],*cells],"Formatting updated")
}
pub fn history(st,redo=false) {
    let stack = if (redo) st.redo else st.undo
    if (len(stack)==0) st else {
        let sheet=min(st.sheet,len(stack[0].sheets)-1)
        let rows=stack[0].sheets[sheet].rows
        let cols=stack[0].sheets[sheet].cols;
        {*:st,book:stack[0],sheet:sheet,row:min(st.row,rows-1),col:min(st.col,cols-1),
            anchor:{row:min(st.anchor.row,rows-1),col:min(st.anchor.col,cols-1)},editing:false,
            undo:if (redo) [st.book,*st.undo] else slice(st.undo,1),
            redo:if (redo) slice(st.redo,1) else [st.book,*st.redo],status:if (redo) "Redone" else "Undone"}
    }
}
pub fn add_sheet(st) {
    let title = "Sheet" ++ string(len(st.book.sheets)+1)
    let next = change(st,{*:st.book,sheets:[*st.book.sheets,empty_sheet(title)]},"Sheet added");
    {*:select(next,0,0),sheet:len(st.book.sheets)}
}
pub fn paste(st,text) {
    let rows = split(replace(text,"\r",""),"\n")
    let incoming = [for (r,line in rows,c,raw in split(line,"\t") where st.row+r<current(st).rows and st.col+c<current(st).cols)
        {row:st.row+r,col:st.col+c,raw:raw,style:f.cell(current(st),st.row+r,st.col+c).style or {}}]
    let positions = [for (c in incoming) f.address(c.row,c.col)];
    set_cells(st,[*[for (c in current(st).cells where not contains(positions,f.address(c.row,c.col))) c],*incoming],"Pasted cells")
}
pub fn copy_text(st) => {let b=bounds(st);join([for (r in b.top to b.bottom)
    join([for (c in b.left to b.right) f.cell(current(st),r,c).raw or ""],"\t")],"\n")}
pub fn stats(st) {
    let b=bounds(st)
    let values=[for (r in b.top to b.bottom,c in b.left to b.right) f.display(current(st),r,c)]
    let numbers=[for (v in values where v is number) v];
    {count:len([for (v in values where v!="") v]),sum:sum(numbers),average:if (len(numbers)>0) sum(numbers)/len(numbers) else 0}
}
fn sum_start(sheet,row,col) => if (row<0 or not (f.display(sheet,row,col) is number)) row+1 else sum_start(sheet,row-1,col)
pub fn autosum(st) {
    let start=sum_start(current(st),st.row-1,st.col)
    if (start==st.row) {*:st,status:"Select an empty cell below a column of numbers"}
    else set_value(st,"=SUM(" ++ f.address(start,st.col) ++ ":" ++ f.address(st.row-1,st.col) ++ ")")
}
pub fn sort_rows(st,descending=false) {
    let b=bounds(st)
    let rows=sort(b.top to b.bottom,{dir:if (descending) 'desc' else 'asc',by:(r)=>f.display(current(st),r,st.col)})
    let moved=[for (r,source in rows,c in b.left to b.right) {
        let cell=f.cell(current(st),source,c);{row:b.top+r,col:c,raw:cell.raw or "",style:cell.style or {}}
    }];
    set_cells(st,[*[for (c in current(st).cells where not selected(st,c.row,c.col)) c],*moved],"Selection sorted")
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
