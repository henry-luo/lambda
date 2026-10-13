// Symbol-font packages as Unicode: pifont's ZapfDingbats codes and a bounded
// marvosym table. A code or name outside them is reported, never guessed.

// ZapfDingbats code → Unicode, after the Unicode Consortium's ZDINGBAT mapping:
// most of 0x21–0x7E sit at U+2700 + (code − 0x20); these do not
let DING_EXCEPTIONS = {'37': 0x260E, '42': 0x261B, '43': 0x261E, '72': 0x2605,
    '108': 0x25CF, '110': 0x25A0, '115': 0x25B2, '116': 0x25BC, '117': 0x25C6, '119': 0x25D7,
    '168': 0x2663, '169': 0x2666, '170': 0x2665, '171': 0x2660,
    '212': 0x2794, '213': 0x2192, '214': 0x2194, '215': 0x2195}

// the Unicode code point of ZapfDingbats code n, or null
pub fn ding_point(n) {
    let exception = DING_EXCEPTIONS[string(n)]
    if (exception != null) exception
    else if (n >= 0x21 and n <= 0x7E) 0x2700 + (n - 0x20)
    else if (n >= 0xA1 and n <= 0xA7) 0x2761 + (n - 0xA1)
    else if (n >= 0xAC and n <= 0xB5) 0x2460 + (n - 0xAC)
    else if (n >= 0xB6 and n <= 0xD3) 0x2776 + (n - 0xB6)
    else if (n >= 0xD8 and n <= 0xEF) 0x2798 + (n - 0xD8)
    else if (n >= 0xF1 and n <= 0xFE) 0x27B1 + (n - 0xF1)
    else null
}

// \ding{n} as text, or null for a code outside the font
pub fn ding(raw) {
    let n = if (raw == null) null else int(trim(raw)) ^ { null }
    let point = if (n == null) null else ding_point(n)
    if (point == null) null else chr(point)
}

// marvosym commands with a Unicode equivalent
pub let MARVOSYM = {
    Letter: "✉", Email: chr(0x1F4E7), Telefon: "☎", Mobilefone: chr(0x1F4F1),
    EUR: "€", Euro: "€", Pointinghand: "☞", Writinghand: "✍",
    Checkedbox: "☑", Square: "☐", Crossedbox: "☒", Info: "ℹ",
    Keyboard: "⌨", Coffeecup: "☕", Yinyang: "☯", Peace: "☮", Ankh: "☥",
    Male: "♂", Female: "♀", Smiley: "☺", Frowny: "☹",
    Sun: "☉", Moon: "☾", Mercury: "☿", Venus: "♀", Earth: "♁",
    Mars: "♂", Jupiter: "♃", Saturn: "♄", Uranus: "♅", Neptune: "♆",
    Pluto: "♇", Aries: "♈", Taurus: "♉", Gemini: "♊", Cancer: "♋",
    Leo: "♌", Virgo: "♍", Libra: "♎", Scorpio: "♏", Sagittarius: "♐",
    Capricorn: "♑", Aquarius: "♒", Pisces: "♓"
}
