// Read-only worksheet presentation of the semantic XLSX model (D9.3.4).
import dom
import cells: .cells
import grid: lambda.ui.core.virtual_grid

let ROW_HEADER = 44
let COLUMN_HEADER = 26
let CELL_WIDTH = 100
let CELL_HEIGHT = 27

pub let css = "
  .xlsx-preview { display:flex; flex-direction:column; min-width:0; min-height:0; background:white;
                  font:13px/1.35 var(--document-font); color:#20242c; }
  .xlsx-preview .xlsx-sheet-scroll { flex:1; min-width:0; min-height:0; padding:0; }
  .xlsx-grid { position:relative; }
  .xlsx-cell, .xlsx-column, .xlsx-row-number, .xlsx-corner {
    position:absolute; box-sizing:border-box; border-right:1px solid #e1e6e4; border-bottom:1px solid #e1e6e4;
    display:flex; align-items:center; padding:3px 8px; overflow:hidden; white-space:nowrap; }
  .xlsx-cell { background:#fff; display:block; text-overflow:ellipsis; }
  .xlsx-cell.numeric { text-align:right; }
  .xlsx-column, .xlsx-row-number, .xlsx-corner { background:#f3f6f4; color:#53635b; justify-content:center; }
  .xlsx-columns { position:sticky; top:0; z-index:3; height:26px; }
  .xlsx-rows { position:sticky; left:0; z-index:2; width:44px; height:0; }
  .xlsx-corner { position:sticky; left:0; z-index:1; width:44px; height:26px; }
  .xlsx-tabs { flex:none; display:flex; gap:2px; overflow-x:auto; padding:2px 8px 0;
               background:#f3f6f4; border-top:1px solid #d9e1dd; }
  .xlsx-tab { flex:none; padding:7px 15px; border:0; border-bottom:2px solid transparent;
              background:transparent; color:#41524a; font:inherit; cursor:pointer; }
  .xlsx-tab:hover { background:#e7efea; }
  .xlsx-tab[aria-selected='true'] { color:#217346; background:#fff; border-bottom-color:#217346; font-weight:600; }
  .xlsx-preview > .preview-error { margin:22px; }
"

fn indices(first:int, end:int) => if (first < end) first to end - 1 else []

fn prepared(sheet) any^ {
  let rows = min(cells.ROW_LIMIT, max([24, for (c in sheet.cells) c.row + 1,
    for (r in sheet.row_heights) r.row + 1, for (m in sheet.merges) m.bottom + 1]));
  let cols = min(cells.COLUMN_LIMIT, max([12, for (c in sheet.cells) c.col + 1,
    for (c in sheet.column_widths) c.max + 1, for (m in sheet.merges) m.right + 1]));
  let heights = map([for (r in sheet.row_heights where r.height > 0) (string(r.row), cells.row_height(r.height)),
    for (r in sheet.hidden_rows) (string(r), 0)]);
  let widths = map([for (span in sheet.column_widths,
    c in indices(max(0, span.min), min(cols, span.max + 1))^ where span.width > 0) (string(c), cells.column_width(span.width)),
    for (span in sheet.hidden_columns, c in indices(max(0, span.min), min(cols, span.max + 1))^) (string(c), 0)]);
  {title:sheet.title, merges:sheet.merges,
    by_address:map([for (c in sheet.cells) (cells.address(c.row,c.col)^, c)]),
    rows:grid.axis(rows,CELL_HEIGHT,[for (key,value in heights) {index:int(key)^,size:value}]),
    cols:grid.axis(cols,CELL_WIDTH,[for (key,value in widths) {index:int(key)^,size:value}])}
}

fn window(sheet, x, y, width, height) any^ => {
  rows:grid.window(sheet.rows,y,max(0,height-COLUMN_HEADER),2)^,
  cols:grid.window(sheet.cols,x,max(0,width-ROW_HEADER),1)^
}

fn initial_window(sheet) any^ => window(sheet,0,0,840,580)^

fn slots(axis, visible) any^ => [for (i in indices(visible.first,visible.last)^,
  let start=grid.offset(axis,i)^, let size=grid.offset(axis,i+1)^-start where size > 0)
    {index:i,start:start,size:size}]

fn box(x,y,w,h) =>
  "left:" ++ string(x) ++ "px;top:" ++ string(y) ++ "px;width:" ++ string(w) ++ "px;height:" ++ string(h) ++ "px;"

fn cell(sheet,row,col,x,y,width,height,merge=null) any^ {
  let address = cells.address(row,col)^
  let item = sheet.by_address[address]
  let formula = if (item.formula != null and item.formula != "") "=" ++ item.formula else null
  let style = item.style or {}
  let value = if (formula != null and not item.has_value) formula else item.value
  let display = if (value == null or value == "") "" else cells.formatted(value,style) ^ { string(value) };
  <div class:(if (value is number) "xlsx-cell numeric" else "xlsx-cell"),
    role:"gridcell", 'data-cell':address, 'aria-rowindex':string(row+1), 'aria-colindex':string(col+1),
    'aria-rowspan':if (merge != null) string(merge.bottom - row + 1) else null,
    'aria-colspan':if (merge != null) string(merge.right - col + 1) else null,
    title:formula, style:box(x,y,width,height) ++ cells.cell_style(style,1), display>
}

fn grid_view(sheet, visible) any^ {
  let rows = slots(sheet.rows, visible.rows)^
  let cols = slots(sheet.cols, visible.cols)^
  let merges = [for (rect in sheet.merges where rect.top < visible.rows.last and rect.bottom >= visible.rows.first
    and rect.left < visible.cols.last and rect.right >= visible.cols.first) rect];
  <div class:"xlsx-grid", role:"grid", 'aria-label':sheet.title, 'aria-readonly':"true",
    'aria-rowcount':string(sheet.rows.count), 'aria-colcount':string(sheet.cols.count),
    style:"width:" ++ string(ROW_HEADER + grid.extent(sheet.cols)^) ++ "px;height:" ++
      string(COLUMN_HEADER + grid.extent(sheet.rows)^) ++ "px;",
    <div class:"xlsx-columns", role:"row",
      for (col in cols) <span class:"xlsx-column", role:"columnheader",
        style:box(ROW_HEADER+col.start,0,col.size,COLUMN_HEADER), cells.column(col.index)^>;
      <div class:"xlsx-corner", 'aria-hidden':"true">>;
    <div class:"xlsx-rows",
      for (row in rows) <span class:"xlsx-row-number", role:"rowheader",
        style:box(0,row.start,ROW_HEADER,row.size), string(row.index+1)>>;
    for (row in rows) <div role:"row",
      for (col in cols where not any([for (rect in merges) grid.contains(rect,row.index,col.index)]))
        cell(sheet,row.index,col.index,ROW_HEADER+col.start,COLUMN_HEADER+row.start,col.size,row.size)^>;
    // Mount each merge once, even if its anchor row or column is hidden or off-screen.
    for (rect in merges) {
      let x = grid.offset(sheet.cols,rect.left)^
      let y = grid.offset(sheet.rows,rect.top)^
      let w = grid.offset(sheet.cols,rect.right+1)^ - x
      let h = grid.offset(sheet.rows,rect.bottom+1)^ - y
      if (w > 0 and h > 0) cell(sheet,rect.top,rect.left,ROW_HEADER+x,COLUMN_HEADER+y,w,h,rect)^ else null
    }
  >
}

pn measured(owner,sheet) {
  let port = dom.query_selector(owner,".xlsx-sheet-scroll")
  let metrics = dom.scroll_metrics(port)
  let scroll = dom.scroll_state(port)
  return window(sheet,scroll.x,scroll.y,metrics.client_width,metrics.client_height)^
}

view <xlsx_preview> () any^ state selected:0, sheet:prepared(~.sheets[0])^, visible:null {
  <section id:"xlsx-preview", class:"xlsx-preview",
    <div class:"rendered-preview xlsx-sheet-scroll", tabindex:"0",
      grid_view(sheet,visible or initial_window(sheet)^)^>;
    <nav class:"xlsx-tabs", role:"tablist", 'aria-label':"Worksheets",
      for (i, tab in ~.sheets)
        <button class:"xlsx-tab", type:"button", role:"tab", 'data-sheet':string(i),
          'aria-selected':string(i==selected), tab.title>>
  >
}
on render(evt) {
  if (evt.event_phase==2) {
    dom.watch_window(evt.target,"resize",true)
    dom.request_frame_once(evt.target,"xlsx_measure")
  }
  return 'pass'
}
on resize(evt) {
  dom.request_frame_once(evt.target,"xlsx_measure")
  return 'pass'
}
on scroll(evt) {
  if (dom.matches(evt.target,".xlsx-sheet-scroll"))
    dom.request_frame_once(dom.closest(evt.target,".xlsx-preview"),"xlsx_measure")
  return 'pass'
}
on xlsx_measure(evt) {
  // Coalesce rapid scroll and resize events into one grid update per frame.
  if (evt.event_phase==2) {
    let next = measured(evt.target,sheet)^
    if (next != visible) { visible = next }
  }
  return 'pass'
}
on click(evt) {
  let target = if (dom.node_type(evt.target)==1) evt.target else dom.parent_node(evt.target)
  let tab = dom.closest(target,".xlsx-tab")
  if (tab==null) { return 'pass' }
  let index = int(dom.get_attribute(tab,"data-sheet"))^
  if (index!=selected) {
    let owner = dom.closest(tab,".xlsx-preview")
    dom.set_scroll_state(dom.query_selector(owner,".xlsx-sheet-scroll"),0.0,0.0)
    selected = index
    sheet = prepared(~.sheets[index])^
    visible = measured(owner,sheet)^
  }
  return 'pass'
}

pub fn preview(workbook) any^ {
  let sheets = if (workbook.error==null) [for (sheet in workbook.sheets where sheet.hidden!=true) sheet] else [];
  if (len(sheets)>0) apply(<xlsx_preview sheets:sheets>)
  else <section id:"xlsx-preview", class:"xlsx-preview",
    <p class:"preview-error", role:"alert", workbook.error or "The workbook has no visible worksheets.">>
}
