// The DOM bridge reads hit metadata; selection policy stays in interaction.ls (D7.2.5).
import dom
import interaction: .interaction
import parameter: .parameter
import util: .util
import behavior: .behavior

pn dom_element(node) { if (node == null or dom.node_type(node) == 1) node else dom.parent_element(node) }
pn closest(node, selector) { if (node == null) null else dom.closest(dom_element(node), selector) }
pn attribute(node, key) { if (node == null) null else dom.get_attribute(node, key) }
pn metadata(node, key) {
    let carrier = closest(node, "[" ++ key ++ "]");
    let text = attribute(carrier, key);
    if (text == null) null else parse(text, 'json') ^ {null}
}

pub pn dispatch(st, evt, definitions, behaviors = []) {
    let host = closest(evt.target, ".lambda-chart");
    let legend = metadata(evt.target, "data-chart-legend");
    let active = if (st.gesture != null) st.gesture[0].frame else null;
    let found = if (legend != null) {view: legend.view, id:legend.id,params: legend.params,behaviors:legend.behaviors,encoding: map([legend.channel, {field: legend.field}])}
        else metadata(evt.target, "data-chart-frame");
    let raw_frame = if (active != null) active else if (found != null) found
        else if (evt.type == "keydown") {view: "chart", params: definitions} else null;
    let part = attribute(closest(evt.target, "[data-chart-part]"), "data-chart-part");
    let named=raw_frame.behaviors |: ~.part!=null;
    let applicable = if (raw_frame == null) [] else raw_frame.behaviors |:
        if (part!=null and len(named)>0) ~.part==part else ~.part==null;
    let allowed = applicable |> ~.param;
    let frame = if (raw_frame == null) null else {*:raw_frame,
        params: raw_frame.params |: ~._behavior != true or contains(allowed, ~.name),
        behaviors: [for (item in applicable) (let candidates = behaviors |: ~.scope == item.scope and ~.id == item.id,
            if (len(candidates) > 0) {*:candidates[0], part: item.part} else item)]};
    let control = closest(evt.target, "[data-chart-input]");
    if (control != null and (evt.type == "input" or evt.type == "change")) {
        let name = attribute(control, "data-chart-input");
        let field = attribute(control, "data-chart-field");
        let raw = if (lower(dom.node_name(control)) == "select") dom.selected_index(control)
            else dom.get_state(control, if (attribute(control, "type") == "checkbox") "checked" else "value");
        let options = metadata(control, "data-chart-options");
        let value = if (options != null) options[int(raw)]
            else if (attribute(control, "type") == "range" or attribute(control, "type") == "number") float(raw)
            else raw;
        interaction.update(st, {param: name, field: field, value: value,
            frame: {view: "chart", params: definitions}})
    } else if (frame == null) st
    else {
        let canvas = if (host != null) dom.query_selector(host, "[data-chart-root='" ++ frame.view ++ "']") else null;
        let box = if (canvas == null) null else dom.bounding_box(canvas);
        let cx = if (evt.clientX != null) evt.clientX else evt.x;
        let cy = if (evt.clientY != null) evt.clientY else evt.y;
        let event = {type: evt.type, frame: frame, row: if (legend != null) legend.row else metadata(evt.target, "data-chart-row"),
            element_key: metadata(evt.target, "data-chart-key"), part: attribute(closest(evt.target, "[data-chart-part]"), "data-chart-part"),
            axis: attribute(closest(evt.target, "[data-chart-axis]"), "data-chart-axis"),
            legend: legend != null, key: evt.key, code: evt.code, repeat: evt.repeat,
            button: evt.button, buttons: evt.buttons, pointerId: evt.pointerId, clientX: cx, clientY: cy,
            shiftKey: evt.shiftKey == true, ctrlKey: evt.ctrlKey == true, altKey: evt.altKey == true, metaKey: evt.metaKey == true,
            deltaX: evt.deltaX, deltaY: evt.deltaY, deltaMode: evt.deltaMode,
            x: if (box == null or box.width == 0 or not util.finite_number(cx)) null else (cx - box.left) * frame.svg_width / box.width - frame.x,
            y: if (box == null or box.height == 0 or not util.finite_number(cy)) null else (cy - box.top) * frame.svg_height / box.height - frame.y};
        let next = interaction.update(st, event);
        if (evt.type == "pointerdown" and next.gesture != null and host != null and evt.pointerId != null) {
            host.set_pointer_capture(evt.pointerId)
        }
        next
    }
}

fn input_control(definition, field, binding, value) {
    let label = if (binding.name != null) binding.name else if (field != null) field else definition.name;
    let input_type = if (binding.input != null) binding.input else "text";
    let attrs = {'data-chart-input': definition.name, *:(if (field != null) {'data-chart-field': field} else {})};
    <label class: "chart-control", label ++ " ";
        if (input_type == "select")
            <select *:attrs, 'data-chart-options': format(binding.options, 'json'),
                for (index, option in binding.options)
                    <option value: string(index), *:(if (option == value) {selected: ""} else {}),
                        if (binding.labels[index] != null) binding.labels[index] else string(option)>>
        else if (input_type == "radio")
            for (index, option in binding.options)
                <label <input *:attrs, type: "radio", name: definition.name ++ (if (field != null) "-" ++ field else ""),
                    value: string(index), 'data-chart-options': format(binding.options, 'json'),
                    *:(if (option == value) {checked: ""} else {})>
                    if (binding.labels[index] != null) binding.labels[index] else string(option)>
        // HTML control state initializes from textual attribute values.
        else <input *:attrs, type: input_type, value: if (value == null) "" else string(value),
            *:(if (input_type == "checkbox" and value == true) {checked: ""} else {}),
            *:(if (binding.min != null) {min: string(binding.min)} else {}),
            *:(if (binding.max != null) {max: string(binding.max)} else {}),
            *:(if (binding.step != null) {step: string(binding.step)} else {})>
    >
}

pub fn controls(definitions, st) => <div class: "chart-controls",
    for (definition in definitions where definition.bind is map and definition.expr == null)
        if (parameter.selection(definition) == null)
            input_control(definition, null, definition.bind, st.values[definition.name])
        else for (field in (if (parameter.selection(definition).fields != null) parameter.selection(definition).fields
            else [for (key in parameter.selection(definition).encodings) definition._encoding[key].field]))
            input_control(definition, field, if (definition.bind.input != null) definition.bind else definition.bind[field],
                (let entries = [for (view_key, store in st.values[definition.name].stores) for (tuple in store) tuple],
                    entries[0][field]))
>
