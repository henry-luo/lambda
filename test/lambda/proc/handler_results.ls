// S7.6.1v8/S7.6.7v5: procedure handlers retain values across all receiving contexts.
pn ok() int { 3 }
pn fail() int^ { raise error("original") }
pn delayed_ok() int^ { sleep(1)^; 23 }
pn delayed_fail() int^ { sleep(1)^; raise error("delayed") }
pn tail() int { fail() ^ { 17 } }
pn tail_declaration() int { fail() ^ { 17 }; let ignored = true }
pn tail_null() any { 11; fail() ^ { null } }
pn explicit_return() int { fail() ^ { return 41 }; 99 }
pn main() {
    let a = ok() ^ { 0 }
    let b = fail() ^ { 17 }
    let c = fail() ^ { null }
    let d = tail_declaration()
    let e = tail_null()
    let f = explicit_return()
    let g = delayed_ok() ^ { 0 }
    let h = delayed_fail() ^ { 29 }
    let fresh: any^ = fail() ^ { error("fresh") };
    [a, b, c, d, e, f, g, h, fresh.message]
}
