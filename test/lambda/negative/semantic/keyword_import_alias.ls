// S16.10.1v2: an import alias is a binding, so a keyword alias is rejected at
// the import line. Before this ruling `import edit: …` parsed and every USE
// failed with a confusing "expected a type pattern".
import edit: .keyword_shadow_module
1
