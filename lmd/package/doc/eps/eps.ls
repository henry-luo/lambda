// D7.2.4 document package; S12.1.2 keeps the execution loop behind a pn boundary.
import interp: .interp
import svg: .svg
import operators: .operators

pub let supported_operators = (operators.SIGNATURES |> string(~key))
pub fn eps_metadata(doc) => {metadata: doc.metadata, bbox: doc.bbox, hires_bbox: doc.hires_bbox, pending: doc.bbox_pending}
pub pn interpret(doc, options = null) map^ { return interp.run(doc, options)^ }
pub fn display_to_svg(display, options = null) element^ => svg.render(display, options)^
pub pn eps_to_svg(doc, options = null) element^ {
    let display = interpret(doc, options)^
    return display_to_svg(display, options)^
}
