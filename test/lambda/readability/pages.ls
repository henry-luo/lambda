// lambda.doc.readability against Readability.js 0.6.0 on synthetic pages:
// each pages/<name>/expected.html and expected-metadata.json was produced by
// Readability.js under Mozilla's harness settings, by
// utils/generate_readability_expected.js (vibe/Lambda_Pkg_Readability.md §7).
import cmp: .mod_compare

let PAGES = ["basic", "brs", "json-ld", "media", "nested", "short", "tables", "unlikely"];
[for (page in PAGES) (let r = cmp.check("test/lambda/readability/pages", page)^, [page, r.ok, r.problems])]
