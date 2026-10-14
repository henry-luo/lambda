// Public DOCX module, resolved as lambda.doc.docx (D7.2.4).
import c: .docx.common
import package: .docx.package
import model: .docx.model
import renderer: .docx.render

let option_names = ["style_map","include_empty_paragraphs","images","id_prefix","unsupported","presentation"]
let paragraph_tags = ["p","h1","h2","h3","h4","h5","h6","blockquote","pre"]
fn checked(options) map^ {
    if (options != null and not (options is map)) raise c.fail("invalid-options","Options must be a map")
    else {
        let opts = if (options == null) {} else options
        let unknown = [for (key,value in opts where not contains(option_names,string(key))) string(key)]
        let mappings = c.get(opts,"style_map",{})
        let invalid = if (not (mappings is map)) [true] else [for (key,value in mappings)
            if (value is string) not contains(paragraph_tags,value) else if (value is map)
                not contains(paragraph_tags,c.get(value,"tag","p")) or
                (value.class != null and not (value.class is string)) or
                any([for (attr_key,v in value) not contains(["tag","class"],string(attr_key))]) else true];
        if (len(unknown) > 0) raise c.fail("invalid-options","Unknown option " ++ unknown[0])
        else if (any(invalid)) raise c.fail("invalid-options","style_map must map style IDs/names to allowed paragraph tags or {tag, class}")
        else if (opts.include_empty_paragraphs != null and not (opts.include_empty_paragraphs is bool))
            raise c.fail("invalid-options","include_empty_paragraphs must be bool")
        else if (opts.images != null and not contains(["data-uri","omit"],opts.images))
            raise c.fail("invalid-options","images must be data-uri or omit")
        else if (opts.unsupported != null and not contains(["warn","error"],opts.unsupported))
            raise c.fail("invalid-options","unsupported must be warn or error")
        else if (opts.presentation != null and not contains(["semantic","source"],opts.presentation))
            raise c.fail("invalid-options","presentation must be semantic or source")
        else if (opts.id_prefix != null and (not (opts.id_prefix is string) or
            any([for (ch in split(opts.id_prefix,"")) ord(ch) < 33 or contains(["#","\\",":"],ch)])))
            raise c.fail("invalid-options","id_prefix must contain no whitespace or URL delimiters")
        else {style_map:mappings,include_empty_paragraphs:c.get(opts,"include_empty_paragraphs",false),
            images:c.get(opts,"images","data-uri"),id_prefix:c.get(opts,"id_prefix",""),
            unsupported:c.get(opts,"unsupported","warn"),presentation:c.get(opts,"presentation","semantic")}
    }
}
pub fn convert(source, options = null) map^ {
    let opts = checked(options)^
    let pkg = package.open_package(source)^
    let parsed = model.build(pkg,opts)^
    let rendered = renderer.render(parsed,opts)^
    let diagnostics = parsed.diagnostics ++ [for (issue in rendered.diagnostics) {*:issue,part:c.get(issue,"part",pkg.main)}];
    if (opts.unsupported == "error" and len(diagnostics) > 0)
        raise c.fail("unsupported",diagnostics[0].code ++ ": " ++ diagnostics[0].message,diagnostics[0].part)
    else {*:rendered,metadata:pkg.metadata,diagnostics:diagnostics}
}
pub fn to_elements(source, options = null) element^ => convert(source,options)^.document
pub fn to_html(source, options = null) string^ => convert(source,options)^.html
pub fn extract_text(source, options = null) string^ => convert(source,options)^.text
