import first: ._template_cache_counter
import second: ._template_cache_import
let a=first.model("first-counter")
let b=second.model("second-counter");
<html <body apply(a) apply(b)>>
