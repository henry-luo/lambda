// Shared Windows ANSI byte mapping for document readers (D9.3.4).
pub fn winansi_special(code) {
    if (code == 128) { chr(8364) }
    else if (code == 130) { chr(8218) }
    else if (code == 131) { chr(402) }
    else if (code == 132) { chr(8222) }
    else if (code == 133) { chr(8230) }
    else if (code == 134) { chr(8224) }
    else if (code == 135) { chr(8225) }
    else if (code == 136) { chr(710) }
    else if (code == 137) { chr(8240) }
    else if (code == 138) { chr(352) }
    else if (code == 139) { chr(8249) }
    else if (code == 140) { chr(338) }
    else if (code == 142) { chr(381) }
    else if (code == 145) { chr(8216) }
    else if (code == 146) { chr(8217) }
    else if (code == 147) { chr(8220) }
    else if (code == 148) { chr(8221) }
    else if (code == 149) { chr(8226) }
    else if (code == 150) { chr(8211) }
    else if (code == 151) { chr(8212) }
    else if (code == 152) { chr(732) }
    else if (code == 153) { chr(8482) }
    else if (code == 154) { chr(353) }
    else if (code == 155) { chr(8250) }
    else if (code == 156) { chr(339) }
    else if (code == 158) { chr(382) }
    else if (code == 159) { chr(376) }
    else { null }
}
