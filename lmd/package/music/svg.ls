// Ordinary SVG elements keep music on the existing Radiant/export path (D7.1.2v2).
import c: .common
import f: .fonts
import s: lambda.chart.svg
import embedded: lambda.doc.font

fn primitive(item) element {
    if (item.kind=="glyph") <path d:item.path,
        transform:s.translate(item.x,item.y)++" scale("++string(item.scale)++" "++string(item.scale_y)++")">
    else if (item.kind=="line") <line x1:item.x1,y1:item.y1,x2:item.x2,y2:item.y2,stroke:"currentColor",'stroke-width':item.thickness>
    else if (item.kind=="polygon") <path d:s.line_path(item.points)++" Z">
    else if (item.kind=="curve") (
        let x1=item.x1,
        let x2=item.x2,
        let c1=x1+(x2-x1)/3,
        let c2=x1+2*(x2-x1)/3,
        let inner=item.bend-(if (item.bend>0) item.thickness else -item.thickness),
        <path d:s.M(x1,item.y1)++s.C(c1,item.y1+item.bend,c2,item.y2+item.bend,x2,item.y2)++
            s.C(c2,item.y2+inner,c1,item.y1+inner,x1,item.y1)++" Z">)
    else if (item.kind=="text") <text x:item.x,y:item.y,'font-family':f.TEXT_FAMILY,'font-size':item.size,
        'font-weight':400,'font-style':"normal",item.text>
    else <g id:item.id,'data-highlight-id':item.id,
        *:(if (item.kind=="event") {'data-event-id':item.id} else {'data-beam-id':item.id}),
        role:"img",'aria-label':item.description,
        <title item.description> for (child in item.shapes) shape(child)>
}
fn shape(item) element => if (item.offset_x!=null or item.offset_y!=null)
    <g transform:s.translate(item.offset_x or 0.0,item.offset_y or 0.0),primitive(item)> else primitive(item)
pub fn svg(layout,page = 0,fit_width = false) element^ {
    let valid=c.require(layout.kind=="music-layout" and c.is_integer(page) and page>=0 and page<len(layout.pages),"invalid layout or page index")^
    let sheet=layout.pages[int(page)]
    let body=<g fill:"currentColor",for (item in sheet.shapes) shape(item)>
    let css=embedded.stylesheet(layout.profile.faces,body);
    <svg xmlns:"http://www.w3.org/2000/svg",width:sheet.width*layout.unit,height:sheet.height*layout.unit,
        viewBox:"0 0 "++string(sheet.width)++" "++string(sheet.height),role:"img",'aria-label':layout.score.title,
        style:"color:#111;background:white"++(if (fit_width) ";display:block;width:100%;height:auto" else ""),
        <title layout.score.title> <defs <style css>> body>
}
pub fn document(layout) element^ => <div class:"lambda-music",style:"overflow:auto;min-width:0;background:#e2e7ee;padding:12px;border-radius:10px",
    for (page in layout.pages) <div class:"lambda-music-page",
        style:"margin:0 auto 12px;width:100%;max-width:"++string(page.width*layout.unit)++"px;line-height:0;background:white;"++
            "box-shadow:0 2px 12px rgba(15,23,42,0.08)",
        svg(layout,page.index,true)^>>
