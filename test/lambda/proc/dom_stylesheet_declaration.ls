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
}
