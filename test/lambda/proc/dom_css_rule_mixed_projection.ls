// D7.4.1v2: an active JS realm must not change Lambda's declared member currency.
import dom
import bridge: ~~.js_helper

pn main() {
    let warmed = bridge.add(1, 2)
    let document = dom.load("test/lambda/dom/stylesheet.html")
    let sheet = dom.stylesheets(document)[0]
    let rule = dom.stylesheet_rule_at(sheet, 0)
    let insert = rule.insert_rule
    let index = insert("& .child {}", 0)
    print([warmed == 3, rule.selector_text, rule.parent_style_sheet == sheet,
        index == 0, rule.css_rules.length == 1,
        rule.css_rules[0].parent_rule == rule, sheet[0] == rule])
    print("\n")
    let declaration = rule.style
    dom.rule_style_set_value(declaration, "background-color", "blue", "")
    let get_value = declaration.get_property_value
    print([declaration.background_color == "blue", get_value("background-color") == "blue"])
    print("\n")
}
