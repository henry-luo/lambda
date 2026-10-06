// CSS-only profile; exact glyph protrusion and expansion need line-layout controls.
import util: ~~.util

fn tracking_value(raw) {
    if (raw == null or raw == "false" or raw == "0") null
    else if (raw == "true") "0.005em"
    else {
        let value = float(raw)
        if (value == null) null else string(value / 1000.0) ++ "em"
    }
}

pub fn invalid_options(opts) {
    if (opts == null) []
    else [for (key in ["kerning", "spacing", "tracking"]
          where opts[key] != null and
          (if (key == "tracking")
              tracking_value(opts[key]) == null and util.option_enabled(opts[key])
           else opts[key] != "true" and opts[key] != "false")) key]
}

pub fn stylesheet(opts) {
    if (opts == null) ""
    else ".latex-document{font-kerning:" ++
        (if (opts.kerning == "false") "none" else "normal") ++ ";}\n" ++
        ".latex-document p{text-align:justify;hyphens:auto;}\n" ++
        (if (tracking_value(opts.tracking) != null)
            ".latex-document{letter-spacing:" ++ tracking_value(opts.tracking) ++ ";}\n" else "") ++
        (if (util.option_enabled(opts.spacing))
            ".latex-document{word-spacing:0.01em;}\n" else "")
}

pub fn unsupported(opts) {
    if (opts == null) []
    else [for (key in ["protrusion", "expansion"]
          where util.option_enabled(opts[key]))
        util.diagnostic("unsupported-microtype-feature", "microtype", key,
            "Exact microtype " ++ key ++ " is unavailable; CSS spacing only", null)]
}
