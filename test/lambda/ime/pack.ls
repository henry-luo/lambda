import p: lambda.ime.chinese.pack
import lines: lambda.io.line_index
import ime: lambda.ime.chinese
import f: .mod_fixture

pn main() {
    let engine = f.engine()^
    let table = engine.lexicon
    let hit = p.lower_bound(table,"2:bei'jing")^
    let first = p.posting_start(table,hit)
    let item = p.posting(table,hit,first,false)^
    let bad_manifest = {*:engine.manifest,format_version:2}
    let source = lines.from_bytes(binary("a\nb\n"))^
    // S7.6.1: procedure-call handlers are statements and discard normal arm values.
    var unknown_profile = 0
    var unknown_option = 0
    ime.load("unknown") ^ { unknown_profile = ^.code }
    ime.load("zh-Hans",{unknown:true}) ^ { unknown_option = ^.code };
    [
        table.offsets is u32[],
        source.offsets == [0u32,2u32],
        lines.line_bytes(source,0)^ == binary("a"),
        lines.line_bytes(source,1)^ == binary("b"),
        p.line_key(table,hit)^ == "2:bei'jing",
        p.equal(table,hit,"2:bei'jing")^,
        not p.equal(table,hit,"2:bei'j")^,
        p.prefix(table,hit,"2:bei'j")^,
        not p.prefix(table,hit,"3:bei'j")^,
        p.lower_bound(table,"")^ == 0,
        p.lower_bound(table,"9:zz")^ == len(table.offsets),
        not p.equal(table,len(table.offsets),"9:zz")^,
        item.text == "北京" and item.reading == "bei'jing" and item.weight == 4800,
        p.posting(table,hit,item.next,false)^.text == "背景",
        (p.from_bytes(bad_manifest,table.bytes,engine.abbrev.bytes) ^ { ^.code }) == 7830,
        (p.from_bytes(engine.manifest,slice(table.bytes,0,len(table.bytes) - 1)^,engine.abbrev.bytes) ^ { ^.code }) == 7830,
        unknown_profile == 7830,
        unknown_option == 7830,
        (ime.candidates(engine,"bei",{unknown:true}) ^ { ^.code }) == 7830,
        (ime.candidates(engine,"bei",{max_keys:0}) ^ { ^.code }) == 7830,
        (ime.candidates(engine,"bei",{max_candidates:1.5}) ^ { ^.code }) == 7830,
        (ime.candidates(engine,"bei",{fuzzy:["unknown"]}) ^ { ^.code }) == 7830,
        // normal loading trusts prepared metadata and does not traverse or hash rows.
        p.from_bytes({*:engine.manifest,counts:{invalid:"not inspected at load"}},table.bytes,engine.abbrev.bytes)^.lexicon.bytes == table.bytes
    ]
}
