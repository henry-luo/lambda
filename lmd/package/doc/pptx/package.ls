// Relationship owners, never filename guesses, define the presentation graph (D7.5.2).
import c: .common
import opc: lambda.doc.opc
import theme: lambda.doc.docx.theme

pub fn xml(parts,path,required=true) any^ => opc.xml(parts,path,required,c.fail,c.uri)^
fn part(pkg,path,tag) map^ {
    let root = xml(pkg.parts,path)^;
    if (root.tag!=tag or root.uri!=c.P or c.child(c.child(root,"cSld"),"spTree")==null)
        raise c.fail("invalid-package","Expected PresentationML " ++ tag,path)
    else {root:root,main:path,relationships:opc.relationships(pkg.parts,path,false,c.fail,c.uri)^}
}
pub fn relation(owner,kind) any^ {
    let matches = [for (rel in owner.relationships where rel.type==c.R ++ "/" ++ kind) rel];
    if (len(matches)>1 or matches[0].external==true)
        raise c.fail("invalid-package","Expected one internal " ++ kind ++ " relationship",owner.main)
    else matches[0]
}
pub fn linked(pkg,owner,kind,tag) any^ {
    let rel = relation(owner,kind)^;
    if (rel==null) null else part(pkg,rel.target,tag)^
}
fn unique_relations(owners,kind) array^ {
    let rels = [for (owner in owners,let rel=relation(owner,kind)^ where rel!=null) rel];
    [for (i,rel in rels where not any([for (j,prior in rels where j<i) prior.target==rel.target])) rel]
}
pub fn open_package(source) map^ {
    let pkg = opc.capture(source,c.fail,c.uri,"PPTX")^
    let ids = c.children(c.child(pkg.root,"sldIdLst"),"sldId")
    let size = c.child(pkg.root,"sldSz")
    let width = c.pixels(c.attr(size,"cx"),-1.0)
    let height = c.pixels(c.attr(size,"cy"),-1.0);
    if (pkg.root.tag!="presentation" or pkg.root.uri!=c.P or
        opc.content_type(pkg,pkg.main)!="application/vnd.openxmlformats-officedocument.presentationml.presentation.main+xml")
        raise c.fail("invalid-package","Expected a PPTX presentation",pkg.main)
    else if (width<=0 or height<=0 or width is inf or height is inf or width is nan or height is nan)
        raise c.fail("invalid-package","Invalid or missing slide dimensions",pkg.main)
    else if (len(ids)==0) raise c.fail("invalid-package","Presentation has no slides",pkg.main)
    else {
        let slides = [for (i,id in ids) {
            let value = c.attr(id,"id")
            let rel = opc.by_id(pkg.relationships,c.attr(id,"id",c.R));
            if (value==null or any([for (j,prior in ids where j<i) c.attr(prior,"id")==value]) or
                rel==null or rel.external or rel.type!=c.R ++ "/slide")
                raise c.fail("invalid-package","Missing or duplicate slide ID/relationship",pkg.main)
            else {*:part(pkg,rel.target,"sld")^,id:"slide-" ++ value}
        }]
        // Layout/master/theme parts are decoded once even when hundreds of slides share them.
        let layouts = [for (rel in unique_relations(slides,"slideLayout")^) part(pkg,rel.target,"sldLayout")^]
        let masters = [for (rel in unique_relations(layouts,"slideMaster")^) part(pkg,rel.target,"sldMaster")^]
        let themes = map([for (rel in unique_relations([pkg,*masters,*layouts,*slides],"theme")^) {
            let root = xml(pkg.parts,rel.target)^;
            if (root.tag!="theme" or root.uri!=c.A) raise c.fail("invalid-package","Invalid DrawingML theme",rel.target)
            else (rel.target,theme.read(root))
        }]);
        {*:pkg,width:width,height:height,slides:slides,
            layouts:map([for (item in layouts) (item.main,item)]),
            masters:map([for (item in masters) (item.main,item)]),themes:themes}
    }
}
pub fn context(pkg,slide) map^ {
    let layout = pkg.layouts[relation(slide,"slideLayout")^.target]
    let master = pkg.masters[relation(layout,"slideMaster")^.target]
    let theme_rel = [for (owner in [slide,layout,master,pkg],let rel=relation(owner,"theme")^ where rel!=null) rel][0]
    let colors = c.get(pkg.themes,theme_rel.target,theme.read(null))
    let mapping = c.get(c.child(c.child(slide.root,"clrMapOvr"),"overrideClrMapping",c.A),"attrs",
        c.get(c.child(master.root,"clrMap"),"attrs",{}))
    let defaults = {bg1:"lt1",tx1:"dk1",bg2:"lt2",tx2:"dk2"}
    let mapped = {*:colors,colors:{*:colors.colors,
        *:map([for (key,value in defaults) (string(key),colors.colors[c.get(mapping,"#" ++ string(key),value)])]),
        *:map([for (key,value in mapping) (slice(string(key),1),colors.colors[value])])}};
    {*:slide,layout:layout,master:master,theme:mapped,width:pkg.width,height:pkg.height,parts:pkg.parts,options:pkg.options}
}
