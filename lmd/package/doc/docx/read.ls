// Shared Word import for the viewer and public conversion API (D9.3.4).
import c: .common
import package: .package
import model: .model
import renderer: .render

let option_names = ["style_map","include_empty_paragraphs","images","id_prefix","unsupported","presentation","snapshot"]
let paragraph_tags = ["p","h1","h2","h3","h4","h5","h6","blockquote","pre"]
fn checked(options) map^ {
    if (options != null and not (options is map)) raise c.fail("invalid-options","Options must be a map")
    else {
        let opts = if (options == null) {} else options
        let unknown = [for (key,value in opts where ((not contains(option_names,string(key))) ^ { false })) string(key)]
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
        else if (opts.snapshot != null and not (opts.snapshot is bool))
            raise c.fail("invalid-options","snapshot must be bool")
        else if (opts.id_prefix != null and (not (opts.id_prefix is string) or
            any([for (ch in ((split(opts.id_prefix,"")) ^ { [] })) ord(ch) < 33 or contains(["#","\\",":"],ch)])))
            raise c.fail("invalid-options","id_prefix must contain no whitespace or URL delimiters")
        else {style_map:mappings,include_empty_paragraphs:c.get(opts,"include_empty_paragraphs",false),
            images:c.get(opts,"images","data-uri"),id_prefix:c.get(opts,"id_prefix",""),
            unsupported:c.get(opts,"unsupported","warn"),presentation:c.get(opts,"presentation","semantic"),
            snapshot:opts.snapshot == true}
    }
}
pub fn convert(source, options, include_serialized: bool, viewer = false) map^ {
    let opts = checked(options)^
    let pkg = package.open_package(source,opts.snapshot)^
    let parsed = model.build(pkg,opts,viewer)^
    let rendered = renderer.render(parsed,opts,include_serialized)^
    let diagnostics = parsed.diagnostics ++ [for (issue in rendered.diagnostics) {*:issue,part:c.get(issue,"part",pkg.main)}];
    if (opts.unsupported == "error" and len(diagnostics) > 0)
        raise c.fail("unsupported",diagnostics[0].code ++ ": " ++ diagnostics[0].message,diagnostics[0].part)
    else {*:rendered,metadata:pkg.metadata,diagnostics:diagnostics}
}

pub fn preview_setup(source) map^ {
    let pkg = package.preview_package(source)^
    let options = checked({include_empty_paragraphs:true,presentation:"source",snapshot:true})^;
    {member:pkg.parts[pkg.main],options:options,setup:model.preview_setup(pkg)^,
        pending:[],counters:{},notes:[],started:false}
}

pub fn preview_batch(cursor, parsed, eof) map^ {
    let body = package.preview_body(parsed,cursor.setup.pkg.main)^
    let batch = model.preview_blocks(cursor.setup,body,cursor.options,not cursor.started)^
    let done = eof or batch.stop
    let blocks = cursor.pending ++ batch.blocks
    // Keep a trailing list together so numbering and nesting survive XML batch boundaries.
    let end = if (done) len(blocks) else max([0,*[for (i,node in blocks where node.list == null) i+1]])^
    let ready = slice(blocks,0,end)
    let pending = slice(blocks,end)
    let notes = model.preview_annotations(ready,cursor.setup,cursor.options,cursor.notes)^;
    let rendered = renderer.render({blocks:ready,annotations:notes,page_style:batch.page_style},
        cursor.options,false,{counters:cursor.counters,done:done,paper_known:batch.paper_known})^;
    {document:rendered.document,metadata:cursor.setup.pkg.metadata,stop:done,
        diagnostics:cursor.setup.pkg.diagnostics ++ batch.diagnostics ++ rendered.diagnostics,
        continuation:{*:cursor,pending:pending,counters:rendered.counters,notes:notes,started:batch.started}}
}
