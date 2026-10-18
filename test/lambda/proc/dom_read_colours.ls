// reads remain callable from fn, including after a host mutation (D7.4.6).
import dom
import radiant
import controller: lambda.edit.controller
import files: lambda.edit.files
import interaction: lambda.ui.core.interaction
import chart: lambda.chart.events
import frames: lambda.map

fn inspect(root) {
    let surface = controller.surface(root)
    let label = dom.get_element_by_id(root, "label")
    let field = dom.get_element_by_id(root, "field")
    let evt = {target: label, currentTarget: surface}
    let component = <dtna.button>
    {
        surface: dom.same_node(surface, dom.get_element_by_id(root, "edit-surface")),
        missing: controller.surface(root, "missing") == null,
        null_surface: controller.surface(null) == null,
        control: controller.is_control(field),
        not_control: not controller.is_control(label),
        null_control: not controller.is_control(null),
        value: files.field_value(label, "field"),
        absent_value: files.field_value(label, "missing") == "",
        owner: dom.same_node(interaction.root(component, evt), surface),
        delegated: dom.same_node(interaction.target(component, evt, "span"), label),
        chart: chart.attribute(chart.closest(dom.first_child(label), ".lambda-chart"), "data-chart-label"),
        getter: surface.get_attribute is fn,
        setter: surface.set_attribute is pn,
        inherited_write: surface.remove is pn,
        create_node: surface.owner_document.create_element is pn,
        create_range: surface.owner_document.create_range is fn,
        local_point: len(radiant.local_point(surface, 0, 0)) == 2,
        map_snapshot: frames.snapshot(surface) is error,
        map_query: frames.query_displayed(surface, [0, 0]) is error
    }
}

pn main() {
    let root = dom.load("test/lambda/dom/read_colours.html")
    radiant.layout(root)
    let before = inspect(root)
    let surface = controller.surface(root)
    surface.set_attribute("data-chart-label", "after")
    dom.set_state(dom.get_element_by_id(root, "field"), "value", "after")
    let after = inspect(root)
    let selected_range = dom.document_create_range(root)
    selected_range.select_node_contents(dom.get_element_by_id(root, "label"))
    print({before: before, after_value: after.value, after_chart: after.chart,
        range_write: selected_range.set_start is pn, range_read: selected_range.to_string is fn,
        range_text: selected_range.to_string()})
    radiant.free(root)
}
