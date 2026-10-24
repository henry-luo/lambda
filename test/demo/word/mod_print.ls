// Print settings are per-print overrides on a copy of the document; the document's own page setup never changes.
// Lambda has no OS printer API, so the only destination is Save as PDF through the regular PDF export.
import dom
import lambda.editor.mod_step
import page: .mod_page
import stories: .mod_stories

pub let destinations = [{value:"pdf",label:"Save as PDF"}]
pub let page_choices = [{value:"all",label:"All"},{value:"odd",label:"Odd pages only"},{value:"even",label:"Even pages only"},{value:"custom",label:"Custom"}]
pub let margin_choices = [{value:"default",label:"Default"},*[for (m in page.margin_presets) {value:m.id,label:m.label}]]
pub let scale_choices = [{value:"fit",label:"Fit to page width"},{value:"custom",label:"Custom"}]
// pages per sheet and their grid, longer side first; the grid turns with the paper.
let grids = [{n:1,cols:1,rows:1},{n:2,cols:2,rows:1},{n:4,cols:2,rows:2},{n:6,cols:3,rows:2},{n:9,cols:3,rows:3},{n:16,cols:4,rows:4}]
pub let per_sheet_choices = [for (g in grids) {value:string(g.n),label:string(g.n)}]
pub fn defaults(doc) {
    let p = page.settings(doc);
    {destination:"pdf",pages:"all",range:"",paper:p.paper,orientation:p.orientation,per_sheet:"1",margins:"default",
        scale_mode:"fit",scale:"100",headers:true,backgrounds:true,path:"temp/word/print.pdf"}
}
pub fn settings(doc,values) {
    let preset = [for (m in page.margin_presets where m.id==values.margins) m.values][0];
    {*:page.settings(doc),*:(preset or {}),paper:values.paper,orientation:values.orientation}
}
pub fn document(doc,values) map^ {
    let p = settings(doc,values)
    if (not page.valid(p)) raise error("Word: these paper and margin choices leave less than 20 mm of writing space")
    else {
        let attrs = attrs_set(doc.attrs,'data-word-page',format(p,'json')^)
        // turning headers and footers off blanks the stories so page fields and spacing still follow the document.
        let printed = if (values.headers) attrs
            else attrs_set(attrs,'data-word-stories',format({*:stories.settings(doc),header:"",footer:""},'json')^);
        {*:doc,attrs:printed}
    }
}
// content scale: Fit to page width is 100% because document content never exceeds the writing width.
pub fn scale(values) {
    let percent = float(values.scale) ^ { null };
    if (values.scale_mode!="custom" or not (percent is number)) 1.0 else max(10,min(200,percent))/100
}
// the default printout keeps the engine's own pagination; any other choice prints the measured preview sheets.
pub fn composed(values) => scale(values)!=1.0 or values.per_sheet!="1" or values.pages!="all"

// A section ends only at an explicit page break; the PDF paginates long sections. Measure each top-level block at the
// printed width and start a new page before the first block that would cross the writing area, so previews never cut a line.
fn breaks(blocks,height,start=0,i=0,starts=[0]) {
    if (i>=len(blocks)) starts
    else {
        let b = blocks[i];
        if (b.bottom-start<=height) breaks(blocks,height,start,i+1,starts)
        // a block that opens its page and is taller than it continues across pages at page-height steps.
        else if (b.top<=start or i==0 or blocks[i-1].bottom<=start) breaks(blocks,height,start+height,i,[*starts,start+height])
        // headings keep with the next block, as the export's h1–h3 break-after:avoid does.
        else if (blocks[i-1].keep and blocks[i-1].top>start) breaks(blocks,height,blocks[i-1].top,i,[*starts,blocks[i-1].top])
        else breaks(blocks,height,b.top,i,[*starts,b.top])
    }
}
pn blocks(sheet) {
    let origin = dom.bounding_box(sheet).y
    let children = [for (child in dom.child_nodes(sheet) where dom.node_type(child)==1) child]
    return [for (child in children, let box=dom.bounding_box(child))
        {top:box.y-origin,bottom:box.y-origin+box.height,keep:contains(["h1","h2","h3"],lower(dom.node_name(child)))}]
}
pub pn measure(dialog,doc,node) {
    let printed = document(doc,dialog.values) ^ { return dialog }
    let g = page.geometry(printed)
    // scaled content flows in a proportionally larger writing area before it is scaled onto the page.
    let starts = [for (section in dom.query_selector_all(dom.root_node(node),".print-measure-sheet"))
        breaks(blocks(section),(g.height-g.top-g.bottom)/scale(dialog.values))];
    if (starts==dialog.starts) dialog else {*:dialog,starts:starts}
}
// one entry per printed page: its section's content and the vertical window it shows.
pub fn pages(doc,starts) {
    let sections = page.sheets(doc);
    [for (i,children in sections, let offsets=(if (starts is array and i<len(starts)) starts[i] else [0])) for (j,top in offsets)
        {children:children,top:top,bottom:if (j+1<len(offsets)) offsets[j+1] else null}]
}
// null marks an unfinished or invalid custom range; the preview keeps every page meanwhile and saving reports it.
pub fn page_numbers(values,count) {
    if (values.pages=="odd") [for (n in 1 to count where n%2==1) n]
    else if (values.pages=="even") [for (n in 1 to count where n%2==0) n]
    else if (values.pages!="custom") [for (n in 1 to count) n]
    else {
        let parts = [for (part in split(values.range,",") where trim(part)!="") split(trim(part),"-")]
        let spans = [for (p in parts) [int(trim(p[0])) ^ { null },int(trim(p[len(p)-1])) ^ { null }]]
        if (len(spans)==0 or any([for (p in parts) len(p)>2]) or
            not all([for (s in spans) s[0] is int and s[1] is int and 1<=s[0] and s[0]<=s[1] and s[1]<=count])) null
        else [for (n in 1 to count where any([for (s in spans) n>=s[0] and n<=s[1]])) n]
    }
}
pub fn range_message(count) => "Word: enter pages such as 1-3, 5 between 1 and " ++ string(count)
// a sheet of paper holds `per_sheet` pages on a grid; 2 and 6 per sheet turn the paper so pages stay upright and large.
pub fn layout(doc,values) {
    let p = page.settings(doc)
    let g = page.geometry(doc)
    let per = int(values.per_sheet)
    let grid = [for (x in grids where x.n==per) x][0]
    let turned = per==2 or per==6
    let width = if (turned) g.height else g.width
    let height = if (turned) g.width else g.height
    let cols = if ((width>=height)==(grid.cols>=grid.rows)) grid.cols else grid.rows
    let rows = if (cols==grid.cols) grid.rows else grid.cols;
    {width:width,height:height,cols:cols,rows:rows,
        width_mm:page.dimension(p,if (turned) "height" else "width"),height_mm:page.dimension(p,if (turned) "width" else "height"),
        // n-up pages keep a small gutter, as browsers do.
        fit:min(width/cols/g.width,height/rows/g.height)*(if (per==1) 1 else 0.94)}
}
pub fn sheets(doc,values,starts) array^ {
    let all = pages(doc,starts)
    let numbers = page_numbers(values,len(all))
    let per = int(values.per_sheet);
    if (numbers==null) raise error(range_message(len(all)))
    else if (len(numbers)==0) []
    else {
        let chosen = [for (n in numbers) {number:n,entry:all[n-1]}];
        [for (k in 0 to int(ceil(len(chosen)/per))-1) slice(chosen,k*per,min(len(chosen),(k+1)*per))]
    }
}
