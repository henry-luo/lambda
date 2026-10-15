import c: .common

let booleans = ["auto-toc","generate-trivial-toc","component-numbers","component-numbers-inherit",
    "section-numbers","section-numbers-inherit","use-docbook-css","verbatim-trim-leading-blank-lines",
    "verbatim-trim-trailing-blank-lines","chunk-nav","chunk-renumber-footnotes","use-id-as-filename",
    "lists-of-figures","lists-of-tables","lists-of-examples","lists-of-equations","lists-of-procedures",
    "verbatim-number-first-line"]
let strings = ["resource-base-uri","verbatim-style-default","generated-id-root","generated-id-sep",
    "verbatim-syntax-highlighter","sections-inherit-from","verbatim-number-separator"]
let positive_integers = ["verbatim-number-every-nth","verbatim-number-minlines"]
pub let DEFAULTS = {["auto-toc"]:true,["generate-trivial-toc"]:false,["component-numbers"]:true,
    ["component-numbers-inherit"]:false,["section-numbers"]:true,["section-numbers-inherit"]:true,
    ["use-docbook-css"]:true,["resource-base-uri"]:"./",["verbatim-style-default"]:"lines",
    ["generated-id-root"]:"R",["generated-id-sep"]:"_",["chunk-section-depth"]:1,["verbatim-syntax-highlighter"]:"none",
    ["chunk-nav"]:true,["chunk-renumber-footnotes"]:true,["use-id-as-filename"]:false,
    ["lists-of-figures"]:true,["lists-of-tables"]:true,["lists-of-examples"]:true,
    ["lists-of-equations"]:false,["lists-of-procedures"]:false,
    ["sections-inherit-from"]:"section",
    ["verbatim-number-every-nth"]:5,["verbatim-number-minlines"]:5,
    ["verbatim-number-first-line"]:true,["verbatim-number-separator"]:"|",
    ["verbatim-trim-leading-blank-lines"]:false,["verbatim-trim-trailing-blank-lines"]:true}

pub fn checked(value) map^ {
    if (value != null and not (value is map)) raise c.fail("invalid-options","Options must be a map")
    else {
        let options = if (value == null) {} else value
        let params = c.get(options,"parameters",{})
        let unknown = [for (key,item in options where not contains(
            ["output","target","parameters","unsupported","customization"],string(key))) string(key)];
        if (len(unknown) > 0) raise c.fail("invalid-options","Unknown option " ++ unknown[0])
        else if (not (params is map)) raise c.fail("invalid-options","parameters must be a map")
        else if (not contains(["single","chunked"],c.get(options,"output","single")))
            raise c.fail("invalid-options","output must be single or chunked")
        else if (not contains(["browser","radiant"],c.get(options,"target","browser")))
            raise c.fail("invalid-options","target must be browser or radiant")
        else if (not contains(["warn","error"],c.get(options,"unsupported","warn")))
            raise c.fail("invalid-options","unsupported must be warn or error")
        else if (options.customization != null and not (options.customization is map))
            raise c.fail("invalid-options","customization must be a map of pure handlers")
        else if (any([for (key,handler in c.get(options,"customization",{})) not (handler is function)]))
            raise c.fail("invalid-options","Every customization handler must be a function")
        else {
            let invalid = [for (key,item in params where
                (contains(booleans,string(key)) and not (item is bool)) or
                (contains(strings,string(key)) and not (item is string)) or
                (contains(positive_integers,string(key)) and (not (item is int) or item < 1)) or
                (string(key) == "chunk-section-depth" and (not (item is int) or item < 0 or item > 5)) or
                not contains(booleans ++ strings ++ positive_integers ++ ["chunk-section-depth"],string(key))) string(key)]
            let merged = {*:DEFAULTS,*:params};
            if (len(invalid) > 0) raise c.fail("invalid-options","Unsupported parameter or invalid type: " ++ invalid[0])
            else if (not contains(["lines","plain"],merged["verbatim-style-default"]))
                raise c.fail("invalid-options","verbatim-style-default must be lines or plain")
            else if (merged["verbatim-syntax-highlighter"] != "none")
                raise c.fail("invalid-options","Only the none syntax-highlighter profile is currently available")
            else if (not (merged["sections-inherit-from"] in ["section",""]))
                raise c.fail("invalid-options","sections-inherit-from currently supports section or empty")
            else if (merged["resource-base-uri"] != "" and
                (not ends_with(merged["resource-base-uri"],"/") or starts_with(merged["resource-base-uri"],"/") or
                 ".." in split(merged["resource-base-uri"],"/") or contains(merged["resource-base-uri"],":") or
                 contains(merged["resource-base-uri"],"\\")))
                raise c.fail("invalid-options","resource-base-uri must be a relative publication directory")
            else {*:options,output:c.get(options,"output","single"),target:c.get(options,"target","browser"),
                unsupported:c.get(options,"unsupported","warn"),parameters:merged,
                customization:c.get(options,"customization",{})}
        }
    }
}
