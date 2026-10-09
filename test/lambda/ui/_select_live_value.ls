import dom
view <selection_probe> {
    <main id:"probe",
        <select id:"choice", <option value:"a", "Alpha"> <optgroup label:"More", <option "Beta">>>
        <button id:"read", "Read value">
        <button id:"empty", "Clear selection">
    >
}
on click(evt) {
    let owner = dom.closest(evt.target, "#probe")
    let control = dom.get_element_by_id(owner, "choice")
    if (dom.get_attribute(evt.target, "id") == "empty") dom.set_selected_index(control, -1)
    dom.set_attribute(owner, "data-value", dom.get_state(control, "value"))
    return 'handled'
}
<html <body apply(<selection_probe>)>>
