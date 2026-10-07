// D7.4.1v2: Lambda projections keep declared names and bound method currency.
import dom

pn main() {
    let document = dom.load("test/lambda/dom/stylesheet.html")
    let sheet = dom.stylesheets(document)[0]
    let rule = dom.stylesheet_rule_at(sheet, 0)
    print([rule.selector_text, rule.parent_style_sheet == sheet,
        rule.css_rules.length == 0, sheet[0] == rule])
    print("\n")
    let declaration = rule.style
    dom.rule_style_set_value(declaration, "width", "12px", "")
    print([rule.selector_text, rule.css_text, rule.parent_style_sheet == sheet])
    print("\n")
    let insert = rule.insert_rule
    let index = insert("& .child {}", 0)
    print([index == 0, rule.css_rules.length == 1,
        rule.css_rules[0].parent_rule == rule])
    print("\n")
    dom.rule_style_set_value(declaration, "background-color", "blue", "")
    let get_value = declaration.get_property_value
    print([declaration.background_color == "blue", get_value("background-color") == "blue"])
    print("\n")
}
