// S7.6.8/S7.6.9: fallback is lazy; filters classify the current chained outcome.
import filters: .mod_handler_error_types
type AnyError = error
type Original = error that ~.message == "original"
type Fresh = error that ~.message == "fresh"
fn fail() int^ => raise error("original")
let fallback_values = [null or^ { 1 }, false or^ { 2 }, "" or^ { 3 }, error("x") or^ { 4 }]
let truthy_values = [0 or^ { 99 }, nan or^ { 99 }, [] or^ { 99 }, {} or^ { 99 }]
let triggers = [null or^ { ^ is null }, false or^ { ^ == false }, "" or^ { ^ == "" }, error("x") or^ { ^.message }]
let once = null or^ { null }
let broad: any^ = fail() ^AnyError { 5 }
let specific: any^ = fail() ^Original { 6 }
let miss: any^ = fail() ^Fresh { 0 }
let chain: any^ = fail() ^Original { error("fresh") } ^Fresh { 7 }
let outer = match 11 { case int: null or^ { ~ } }
let precedence = 1 + 2 or^ { 0 }
let qualified: any^ = error("qualified") ^filters.Failure { 8 };
[fallback_values, truthy_values, triggers, once, broad, specific, miss.message, chain, outer, precedence, qualified]
