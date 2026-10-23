// saved SmartArt data and supported declarative layouts remain document data (S1.8).
import c: .common
import package: .package
import layout: .diagram_layout

fn issue(pkg, message) => {nodes:[],diagnostics:[c.issue("unsupported-diagram",pkg.main,message)]}
fn part(pkg, reference, attribute, kind) any^ {
    let rel = package.by_id(pkg.relationships,c.attr(reference,attribute,c.R));
    if (rel == null or rel.external or rel.type != c.R ++ "/" ++ kind or pkg.parts[rel.target] == null) null
    else package.xml(pkg.parts,rel.target,true)^
}

pub fn read(pkg, reference, width, height, colors) map^ {
    let rel = package.by_id(pkg.relationships,c.attr(reference,"dm",c.R));
    if (rel == null or rel.type != c.R ++ "/diagramData") issue(pkg,"Unresolved SmartArt data relationship")
    else if (rel.external) issue(pkg,"External SmartArt data was not loaded")
    else if (pkg.parts[rel.target] == null) issue(pkg,"SmartArt data part is missing")
    else {
        let root = package.xml(pkg.parts,rel.target,true)^;
        if (root.uri != c.DGM or root.tag != "dataModel") issue(pkg,"Unsupported SmartArt data root")
        else {
            // presentation and transition points repeat labels or carry layout metadata.
            let labels = [for (point in c.children(c.child(root,"ptLst",c.DGM),"pt",c.DGM),
                paragraph in c.descendants(c.child(point,"t",c.DGM),"p",c.A),
                let value = c.drawing_text(paragraph) where (c.attr(point,"type","") == null or
                    contains(["node","asst"],c.attr(point,"type",""))) and value != "") value];
            if (len(labels) == 0) issue(pkg,"SmartArt has no supported saved text")
            else {
                let image = layout.render(root,part(pkg,reference,"lo","diagramLayout")^,
                    part(pkg,reference,"cs","diagramColors")^,part(pkg,reference,"qs","diagramQuickStyle")^,
                    colors,width,height)^;
                if (image != null) {nodes:[{kind:"vector",class:"docx-diagram",svg:image.svg,
                    text:join(labels,"\n")}],diagnostics:[]}
                else {nodes:[{kind:"fallback",class:"docx-diagram",label:"SmartArt (text only)",
                    text:join(labels,"\n")}],
                diagnostics:[c.issue("unsupported-diagram-layout",pkg.main,
                    "SmartArt text retained; diagram layout omitted")]}
            }
        }
    }
}
