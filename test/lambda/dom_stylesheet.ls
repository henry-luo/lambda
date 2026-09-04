// Stylesheets and rules from a Lambda script with no JS realm (ESO114):
// stylesheets(doc) names the document; pools come from the sheet, not the realm.
import dom
let doc = dom.load("test/lambda/dom/stylesheet.html")
let sheets = dom.stylesheets(doc)
let sheet = sheets[0]
let rules = dom.stylesheet_rules(sheet)
let r0 = dom.stylesheet_rule_at(sheet, 0)
{ sheet_count: len(sheets), rule_count: dom.stylesheet_get_length(sheet), rules_len: len(rules),
  href: dom.stylesheet_get_href(sheet), type: dom.stylesheet_get_type(sheet), disabled: dom.stylesheet_get_disabled(sheet),
  r0_selector: dom.rule_get_selector_text(r0), r0_type: dom.rule_get_type(r0), r0_text: dom.rule_get_css_text(r0),
  r1_selector: dom.rule_get_selector_text(dom.stylesheet_rule_at(sheet, 1)),
  r1_has_weight: dom.rule_style_has_property(dom.rule_get_style(dom.stylesheet_rule_at(sheet, 1)), "fontWeight") }
