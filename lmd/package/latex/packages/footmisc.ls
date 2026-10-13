// latex/packages/footmisc.ls — footmisc: symbol footnote marks, the footnote rule and
// \footref. HTML keeps every note at the document's end, which already satisfies
// bottom and stable; perpage and para need pages and stay unsupported options.

import util: ~~.util

pub let OPTIONS = ["symbol", "symbol*", "bottom", "stable", "norule"]

// footnote n's mark: \fnsymbol under symbol or symbol*, digits otherwise. LaTeX
// stops at the ninth symbol under symbol, where symbol* goes on in digits; both
// continue in digits here.
pub fn mark(options, n) {
    if (options != null and (util.option_enabled(options.symbol) or
        util.option_enabled(options["symbol*"]))) util.fnsymbol(n)
    else string(n)
}

// norule drops the rule above the notes
pub fn ruled(options) => options == null or not util.option_enabled(options.norule)
