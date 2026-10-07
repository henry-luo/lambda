// D7.3.3: the catalog also serves declaration methods without a JavaScript realm.
import dom

pn main() {
    let document = dom.load("test/lambda/dom/stylesheet.html")
    let sheet = dom.stylesheets(document)[0]
    let rule = dom.stylesheet_rule_at(sheet, 0)
    let declaration = dom.rule_get_style(rule)
    let result = dom.rule_style_set_value(declaration, "WIDTH", "12px", "IMPORTANT")
    print([result == null,
        dom.rule_style_get_value(declaration, "width"),
        dom.rule_style_get_priority(declaration, "WIDTH"),
        dom.rule_style_item(declaration, 1),
        dom.rule_style_get_value(declaration, "parentRule")])
    print("\n")
    dom.rule_style_set_value(declaration, "width", "20px", "urgent")
    print(dom.rule_style_get_value(declaration, "width"))
    print("\n")
    dom.rule_style_set_value(declaration, "width", "", "urgent")
    print([dom.rule_style_get_value(declaration, "width") == "",
        dom.rule_style_get_priority(declaration, "width")])
    print("\n")
    // D7.3.3: native inline access shares CSSOM without a JavaScript realm.
    let intro = dom.query_selector(document, "#intro")
    let width_result = dom.style_set_property(intro, "width", "18px")
    let opacity_result = dom.style_set_property(intro, "opacity", 0.5)
    print([dom.style_get_property(intro, "color") == "",
        width_result == "18px", opacity_result == 0.5,
        dom.style_get_property(intro, "width") == "18px",
        dom.style_get_property(intro, "opacity") == "0.5"])
    print("\n")
}
