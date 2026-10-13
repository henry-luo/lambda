import shared: lambda.ui.core.theme

pub let defaults = {
    primary:"#d7ad62", success:"#82b580", warning:"#e4aa4f", error:"#e3746d",
    text:"#f4e4c3", text_secondary:"#c0a782", border:"#806345", background:"#171313",
    surface:"#2d2420", disabled_background:"#292425", disabled_text:"#84786b",
    gold:"#cda464", gold_light:"#f9e1a2", gold_dark:"#634326", iron:"#869196",
    wood:"#39251c", parchment:"#e8d2a6", ink:"#382a20", health:"#bd3c49", mana:"#3a96c7",
    stamina:"#699b58", epic:"#ae78d7", shadow:"#090909",
    font_family:"Georgia, 'Times New Roman', serif",
    font_size:16, control_height:42, radius:5, spacing:12, line_height:1.5,
    frame_width:3, slot_size:76
}
let lengths = ["font_size","control_height","radius","spacing","frame_width","slot_size"]
let zero_lengths = ["radius","spacing"]
let extra = ["gold","gold_light","gold_dark","iron","wood","parchment","ink","health","mana","stamina","epic","shadow","frame_width","slot_size"]
pub fn resolve(overrides = {}) map^ => shared.skin_resolve(defaults,overrides,lengths,zero_lengths,extra,"blade")^
pub fn variables(overrides = {}, scoped = false) string^ => shared.skin_variables(defaults,overrides,lengths,zero_lengths,extra,"blade",scoped)^
