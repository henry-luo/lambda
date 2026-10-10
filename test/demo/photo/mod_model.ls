import geometry: .mod_geometry
import presets: .mod_presets

pub let limits = {exposure:[-3.0,3.0],brightness:[-1.0,1.0],contrast:[0.0,2.0],
    saturation:[0.0,2.0],temperature:[-1.0,1.0],gamma:[0.2,3.0],intensity:[0.0,1.0],angle:[-15.0,15.0]}
pub fn recipe() => {turns:0,flip_x:false,flip_y:false,angle:0.0,crop:geometry.full(),
    exposure:0.0,brightness:0.0,contrast:1.0,saturation:1.0,temperature:0.0,gamma:1.0,
    preset:"original",intensity:1.0,output_width:0,output_height:0}
pub fn valid(value) => value is map and value.turns is int and value.turns >= 0 and value.turns < 4 and
    value.flip_x is bool and value.flip_y is bool and geometry.valid(value.crop) and
    all([for (key,interval in limits) value[key] is number and value[key] >= interval[0] and value[key] <= interval[1]]) and
    contains([for (item in presets.catalog) item.id],value.preset) and
    value.output_width is int and value.output_width >= 0 and value.output_width <= 16384 and
    value.output_height is int and value.output_height >= 0 and value.output_height <= 16384 and
    ((value.output_width == 0 and value.output_height == 0) or (value.output_width > 0 and value.output_height > 0))
pub fn history(value = recipe()) => {entries:[value],cursor:0,draft:null}
pub fn current(history_state) => if (history_state.draft == null) history_state.entries[history_state.cursor] else history_state.draft
pub fn begin(history_state) => {*:history_state,draft:current(history_state)}
pub fn draft(history_state, value) => {*:history_state,draft:value}
pub fn discard(history_state) => {*:history_state,draft:null}
pub fn commit_edit(history_state, value) {
    if (value == history_state.entries[history_state.cursor]) discard(history_state)
    else (
        let entries = [*[for (i in 0 to history_state.cursor) history_state.entries[i]],value],
        let bounded = if (len(entries) > 100) slice(entries,len(entries)-100,len(entries)) else entries,
        {entries:bounded,cursor:len(bounded)-1,draft:null}
    )
}
pub fn step_back(history_state) => {*:history_state,cursor:max(0,history_state.cursor-1),draft:null}
pub fn step_forward(history_state) => {*:history_state,cursor:min(len(history_state.entries)-1,history_state.cursor+1),draft:null}
pub fn change(value, key, requested) => if (contains(limits,key))
    {*:value,[key]:geometry.clamp(requested,limits[key][0],limits[key][1])} else value
pub fn orient(value, action) => {*:value,crop:geometry.full(),
    turns:if (action == "rotate") (value.turns+3)%4 else value.turns,
    flip_x:if (action == "flip-x") not value.flip_x else value.flip_x,
    flip_y:if (action == "flip-y") not value.flip_y else value.flip_y}
