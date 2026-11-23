// Shared PowerPoint import for the viewer and public conversion API (D9.3.4).
import c: .common
import package: .package
import model: .model

fn checked(options) map^ {
    let opts = if (options==null) {} else options;
    if (not (opts is map)) raise c.fail("invalid-options","Options must be a map")
    else if (any([for (key,value in opts) not (string(key) in ["images","unsupported","snapshot"])]))
        raise c.fail("invalid-options","Unknown import option")
    else if (opts.images!=null and not (opts.images in ["data-uri","omit"]))
        raise c.fail("invalid-options","images must be data-uri or omit")
    else if (opts.unsupported!=null and not (opts.unsupported in ["warn","error"]))
        raise c.fail("invalid-options","unsupported must be warn or error")
    else if (opts.snapshot != null and not (opts.snapshot is bool))
        raise c.fail("invalid-options","snapshot must be bool")
    else {images:c.get(opts,"images","data-uri"),unsupported:c.get(opts,"unsupported","warn"),
        snapshot:opts.snapshot == true}
}
pub fn read(source,options=null) map^ {
    let opts = checked(options)^
    let pkg = package.open_package(source,opts.snapshot)^
    let result = model.build({*:pkg,options:opts})^;
    if (opts.unsupported=="error" and len(result.diagnostics)>0)
        raise c.fail("unsupported",result.diagnostics[0].code ++ ": " ++ result.diagnostics[0].message,result.diagnostics[0].part)
    else {*:result,metadata:pkg.metadata}
}
