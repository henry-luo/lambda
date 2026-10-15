import parser: .parse
import surface: .surface
import presentation: .presentation
import c: .common

pub pn render_document_file(path,options = null) element^ {
    let opts = c.options(options,["interactive"])^
    let source = input(path,"binary")^
    let document = parser.parse(source)^;
    return if (c.get(opts,"interactive",true)) surface.viewer(document)^ else presentation.to_html(document)^
}
