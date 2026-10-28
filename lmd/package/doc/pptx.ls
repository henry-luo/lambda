// Public PresentationML import is a shipped script package, like DOCX (D7.2.1/D7.2.4v4).
import c: .pptx.common
import package: .pptx.package
import model: .pptx.model
import slide: lambda.slide

fn checked(options) map^ {
    let opts = if (options==null) {} else options;
    if (not (opts is map)) raise c.fail("invalid-options","Options must be a map")
    else if (any([for (key,value in opts) not (string(key) in ["images","unsupported"])]))
        raise c.fail("invalid-options","Unknown import option")
    else if (opts.images!=null and not (opts.images in ["data-uri","omit"]))
        raise c.fail("invalid-options","images must be data-uri or omit")
    else if (opts.unsupported!=null and not (opts.unsupported in ["warn","error"]))
        raise c.fail("invalid-options","unsupported must be warn or error")
    else {images:c.get(opts,"images","data-uri"),unsupported:c.get(opts,"unsupported","warn")}
}
pub fn convert(source,options=null) map^ {
    let opts = checked(options)^
    let pkg = package.open_package(source)^
    let result = model.build({*:pkg,options:opts})^;
    if (opts.unsupported=="error" and len(result.diagnostics)>0)
        raise c.fail("unsupported",result.diagnostics[0].code ++ ": " ++ result.diagnostics[0].message,result.diagnostics[0].part)
    else {*:result,metadata:pkg.metadata,html:format(slide.handout(result.document)^,'html')^}
}
pub fn to_elements(source,options=null) element^ => convert(source,options)^.document
pub fn to_html(source,options=null) string^ => convert(source,options)^.html
pub fn extract_text(source,options=null) string^ => convert(source,options)^.text
