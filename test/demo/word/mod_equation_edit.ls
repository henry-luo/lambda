// An equation's source draft is UI state; only a validated transaction changes its atom (S12.1.3, D7.2.5).
import dom
import image: lambda.editor.mod_image
import equation: .mod_equation
import catalog: .mod_equation_catalog

pub fn active(st) => st.equation_edit!=null and st.equation_edit.path==image.selected(st.editor).path and
    st.equation_edit.revision==st.editor.model_revision and equation.settings(image.selected(st.editor).node)!=null
pub fn draft(st) => if (active(st)) st.equation_edit else {
    path:image.selected(st.editor).path,revision:st.editor.model_revision,values:equation.settings(image.selected(st.editor).node),error:null,selection:null}
pub fn parameters(values) => {*:values,size:if (values.size is string) (float(values.size) ^ {null}) else values.size}
pub pn source_range(node,source) {
    let field = dom.get_element_by_id(dom.root_node(node),"equation-editor-source")
    return if (field==null) {start:len(source),end:len(source)} else
        {start:dom.tc_selection_start(field),end:dom.tc_selection_end(field)}
}
pub pn request(node) {
    dom.request_frame(dom.get_element_by_id(dom.root_node(node),"word"),"word_equation_ready")
}
pub pn focus(st,node) {
    if (not active(st)) { return }
    let field = dom.get_element_by_id(dom.root_node(node),st.equation_edit.focus or "equation-editor-source")
    dom.focus_set(field,false)
    if (st.equation_edit.selection!=null and st.equation_edit.focus=="equation-editor-source")
        dom.tc_set_selection(field,st.equation_edit.selection.start,st.equation_edit.selection.end,"forward")
}
pub fn fragment(params) => if (params.category=="symbols")
    [for (g in catalog.symbol_groups) for (s in g.entries where s.id==params.id) s][0]
    else if (params.category=="normal") {template:"\\text{#}",body:"text",literal:true}
    else catalog.entry(params.category,params.id)
