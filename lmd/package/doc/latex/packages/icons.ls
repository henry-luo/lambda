// latex/packages/icons.ls — fontawesome5 (vibe/impl/Lambda_Impl_Latex_Packages.md §13).
// Font Awesome is not bundled (§17.3), so an icon renders as a Unicode look-alike
// where one exists and as its name otherwise; the catalogue row reports the
// substitution once. Icons are commands named \fa<Name>, owned through the row's
// command_prefix (registry.owner), besides \faIcon{name}.

pub let PREFIX = "fa"
pub let COMMANDS = ["faIcon", "faIconFA"]
// the style options choose regular or solid shapes, which the look-alikes do not have
pub let OPTIONS = ["fixed", "regular", "solid", "brands", "pro"]

let LOOKALIKES = {envelope: "✉", 'envelope-open': "✉", phone: "☎", 'phone-alt': "☎",
    home: "⌂", star: "★", 'star-half': "⯪", check: "✓", 'check-circle': "✔",
    times: "✕", 'times-circle': "✖", heart: "♥", music: "♪", pen: "✎", pencil: "✎",
    'pencil-alt': "✎", cog: "⚙", flag: "⚑", bolt: "⚡", sun: "☀", moon: "☾",
    cloud: "☁", umbrella: "☂", snowflake: "❄", 'arrow-right': "→", 'arrow-left': "←",
    'arrow-up': "↑", 'arrow-down': "↓", plus: "+", minus: "−", info: "ℹ",
    'info-circle': "ⓘ", exclamation: "!", question: "?", link: "⛓", 'map-marker': "⌖",
    'map-marker-alt': "⌖", circle: "●", square: "■",
    copyright: "©", registered: "®", trademark: "™", scissors: "✂",
    cut: "✂", 'hand-point-right': "☞", 'hand-point-left': "☜",
    female: "♀", male: "♂", hourglass: "⌛", clock: "⌚", keyboard: "⌨"}

// an icon's name: \faIcon{map-marker}, or the command's own (\faMapMarker), with
// the capitals of its words as hyphens
pub fn icon_name(command, argument) {
    if (command == "faIcon" or command == "faIconFA") trim(argument or "")
    else kebab(slice(command, len(PREFIX), len(command)), 0, "")
}

fn kebab(text, i, acc) {
    if (i >= len(text)) acc
    else {
        let ch = slice(text, i, i + 1)
        let lower_ch = lower(ch)
        kebab(text, i + 1, acc ++ (if (lower_ch != ch and i > 0) "-" ++ lower_ch else lower_ch))
    }
}

pub fn lookalike(icon) => LOOKALIKES[icon]
