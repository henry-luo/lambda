// latex/packages/varioref.ls — varioref's commands (vibe/impl/Lambda_Impl_Latex_Packages.md
// §10). render.ls (render_vref) prints a reference with an English page phrase whose
// page anchor paged output fills, as for \pageref; continuous output reports the
// page as unresolved (registry.target_diagnostics). varioref's "on the next page"
// forms need the pages of both the reference and its target, which only paged
// output has.

pub let COMMANDS = ["vref", "Vref", "vpageref", "Vpageref", "vrefrange", "vpagerefrange", "fullref"]
// the language options other than English would change the phrases
pub let OPTIONS = ["english", "american", "british", "draft", "final", "space", "nospace"]
