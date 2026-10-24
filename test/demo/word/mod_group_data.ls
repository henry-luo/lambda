// A group is one image atom with recoverable member sources and an embedded SVG (D7.2.5).
import lambda.editor.mod_step
import objects: .mod_objects

pub let key = 'data-word-group'
fn valid(p) => p is map and objects.dimensions_valid({width:p.width,height:p.height,rotation:0}) and
    p.items is array and len(p.items)>=2 and len(p.items)<=20 and all([for (v in p.items)
        v is map and v.source is string and v.src is string and objects.dimensions_valid(v) and
        v.x is number and v.y is number and v.flip_x is bool and v.flip_y is bool and v.hidden is bool])
pub fn data(n) { let p=objects.settings(n,key); if (valid(p)) p else null }
fn limits(p) => {low:max([for (v in p.items) max((v.min_width or 16)/v.width,(v.min_height or 16)/v.height)]),
    high:min([for (v in p.items) min((v.max_width or 650)/v.width,(v.max_height or 2000)/v.height)])}
pub fn settings(n) {
    let p = data(n);
    if (p==null or not (p.items is array) or len(p.items)<2) null else
        {width:(float(attrs_get(n.attrs,'width')) ^ {p.width}) or p.width,height:(float(attrs_get(n.attrs,'height')) ^ {p.height}) or p.height,
            rotation:(float(attrs_get(n.attrs,'data-word-rotation')) ^ {0}) or 0,
            min_width:p.width*limits(p).low,min_height:p.height*limits(p).low,
            max_width:min(650,p.width*limits(p).high),max_height:min(2000,p.height*limits(p).high)}
}
pub fn proportional(n,width,height) {
    let p=data(n);
    p==null or (height is number and abs(width/p.width-height/p.height)<0.001 and
        width/p.width>=limits(p).low and width/p.width<=limits(p).high)
}
pub fn graphic(p) => <svg xmlns:"http://www.w3.org/2000/svg",width:p.width,height:p.height,
    viewBox:"0 0 " ++ string(p.width) ++ " " ++ string(p.height),
    *[for (v in p.items where not v.hidden) <g transform:"translate(" ++ string(v.x) ++ " " ++ string(v.y) ++ ") rotate(" ++
        string(v.rotation) ++ ") scale(" ++ string(if (v.flip_x) -1 else 1) ++ " " ++ string(if (v.flip_y) -1 else 1) ++ ")",
        <image href:v.src,x:-v.width/2,y:-v.height/2,width:v.width,height:v.height,preserveAspectRatio:"none">>]>
