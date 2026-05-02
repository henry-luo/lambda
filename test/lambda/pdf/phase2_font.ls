// Phase 2 — font resolution & decoding (font.ls).
//
// Verifies Standard 14 lookup, BaseFont heuristics (with subset prefix),
// and hex/literal string decoding fallbacks (no /ToUnicode CMap).

import font: lambda.package.pdf.font

pn main() {
    let h  = font.standard14("Helvetica")
    let tb = font.from_basefont("ABCDEF+TimesNewRomanPSMT-Bold")
    let cb = font.from_basefont("Courier-BoldOblique")
    let dh = font.decode_hex("48656C6C6F", null)
    let dl = font.decode_literal("Hi (\\051)", null)

    print({
        helvetica:    h,
        times_bold:   tb,
        courier_bold: cb,
        decode_hex:   dh,
        decode_lit:   dl
    })
}
