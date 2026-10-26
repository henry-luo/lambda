import c: .common
import syllables: .syllables
import query: .query
import learned: .user

fn checked_settings(value) map^ {
    let opts = c.options(value,["fuzzy","page_size"])^
    let pairs = syllables.settings(opts.fuzzy)^;
    {fuzzy:[for (pair in pairs) pair.name],page_size:c.bounded(c.get(opts,"page_size",7),1,9,"page_size")^}
}

fn valid_session(value) bool^ => c.require(value is map and value.kind == "lambda.ime.chinese.session",
    "expected an IME session")^
fn active(s) bool => len(s.raw) > 0 or len(s.chosen) > 0
fn preedit(s) string => join([for (item in s.chosen) item.text],"") ++ s.raw
fn pages(s) int => (len(s.items) + s.settings.page_size - 1) div s.settings.page_size
fn absolute(s) int => s.page * s.settings.page_size + s.highlight
// hosts reuse the reducer's frame when resuming presentation without changing the session.
pub fn frame(s) map => {type:"candidates",items:if (s.expanded) s.items else slice(s.items,s.page * s.settings.page_size,
    (s.page + 1) * s.settings.page_size),page:s.page,pages:pages(s),
    highlight:if (s.expanded) absolute(s) else s.highlight,
    *:(if (s.expanded) {expanded:true,columns:s.settings.page_size} else {})}
fn result(s,actions) map => {session:s,actions:actions}
fn passed(s) map => result(s,[{type:"pass"}])
fn cleared(s) map => {*:s,raw:"",caret:0,stops:[0],expanded:false,chosen:[],items:[],page:0,highlight:0,truncated:false,dirty:false}
fn compose_action(s) map => {type:"compose",text:preedit(s),
    caret:len(preedit(s)) - len(s.raw) + s.caret}

pub fn create(engine,value = null) map^ {
    let opts = checked_settings(value)^;
    {kind:"lambda.ime.chinese.session",profile:engine.profile,syllables:engine.syllables,
        settings:opts,mode:"chinese",raw:"",caret:0,stops:[0],expanded:false,chosen:[],items:[],page:0,highlight:0,
        truncated:false,dirty:false,shift_held:false,shift_tap:false,
        double_quote:false,single_quote:false,user:learned.empty(engine.profile)}
}

fn refreshed(engine,s) map^ {
    let found = query.candidates(engine,s.raw,{fuzzy:s.settings.fuzzy},s.user)^;
    {*:s,items:found.items,stops:found.caret_stops,page:0,highlight:0,truncated:found.truncated,dirty:false}
}

fn composing(engine,s) map^ {
    let next = refreshed(engine,{*:s,expanded:false})^;
    result(next,[compose_action(next),frame(next)])
}

fn move_caret(s,delta) map {
    let before = [for (stop in s.stops where stop < s.caret) stop]
    let position = if (delta < 0) (if (len(before) == 0) 0 else before[len(before) - 1])
        else max(0,min(len(s.raw),s.caret + delta))
    let next = {*:s,caret:position};
    // S2.5.8/S17.4.1: converted text precedes the editable pinyin in code-point coordinates.
    result(next,[compose_action(next)])
}

fn edit_raw(engine,s,from_at,to_at,text) map^ {
    let prefix = slice(s.raw,0,from_at) ++ text
    let suffix = slice(s.raw,to_at)
    // deleting a syllable's only letter also removes its now-empty separator.
    let tail = if ((len(prefix) == 0 or ends_with(prefix,"'")) and starts_with(suffix,"'"))
        slice(suffix,1) else suffix
    let next = {*:s,raw:prefix ++ tail,caret:len(prefix)};
    if (not active(next)) cancel(next) else composing(engine,next)^
}

fn cancel(s) map {
    let next = cleared(s);
    result(next,[{type:"cancel"},frame(next)])
}

fn finish(s,text) map^ {
    let user = learned.learn(s.user,s.chosen)^
    let next = {*:cleared(s),user:user};
    result(next,[{type:"commit",text:text},frame(next)])
}

pub fn configure(engine,s,value) map^ {
    let valid = valid_session(s)^
    let profile = c.require(s.profile == engine.profile,"session/engine profile mismatch")^
    let next = {*:s,settings:checked_settings(value)^,expanded:false}
    let updated = if (active(next)) refreshed(engine,next)^ else next;
    result(updated,[frame(updated)])
}

pub fn settings(s) map^ {
    let valid = valid_session(s)^;
    s.settings
}

pub fn with_user(s,user) map^ {
    let valid = valid_session(s)^
    let checked = learned.validate(user,s.syllables,s.profile)^;
    {*:s,user:checked,dirty:true}
}

fn select_at(engine,s,at) map^ {
    if (at < 0 or at >= len(s.items)) result(s,[frame(s)])
    else {
        let item = s.items[at]
        let choice = {text:item.text,reading:item.reading,raw:slice(s.raw,0,item.consumed)}
        let next = {*:s,raw:slice(s.raw,item.consumed),caret:max(0,s.caret - item.consumed),
            chosen:[*s.chosen,choice]};
        if (len(next.raw) == 0) finish(next,preedit(next))^ else composing(engine,next)^
    }
}

fn select(engine,s,index) map^ =>
    if (index < 0 or index >= s.settings.page_size) result(s,[frame(s)])
    else select_at(engine,s,s.page * s.settings.page_size + index)^

fn backspace(engine,s) map^ {
    if (s.caret > 0) edit_raw(engine,s,s.caret - 1,s.caret,"")^
    else if (len(s.raw) == 0 and len(s.chosen) > 0) {
        let previous = s.chosen[len(s.chosen) - 1]
        let next = {*:s,raw:previous.raw,caret:len(previous.raw),
            chosen:take(s.chosen,len(s.chosen) - 1)};
        composing(engine,next)^
    } else move_caret(s,0)
}

fn navigate(s,key,evt) map {
    let size = s.settings.page_size
    let at = absolute(s)
    let columns = if (evt.candidate_columns is int) max(1,min(size,evt.candidate_columns)) else size
    // up from the first grid row returns to the strip without changing its selection.
    let collapsing = s.expanded and key == "ArrowUp" and at < columns
    let target = if (collapsing) at
        else if (key in ["-","PageUp"]) max(0,s.page - 1) * size
        else if (key in ["=","PageDown"]) max(0,min(pages(s) - 1,s.page + 1)) * size
        else max(0,min(max(0,len(s.items) - 1),at +
            (if (key == "ArrowLeft") -1 else if (key == "ArrowRight") 1
             else if (key == "ArrowUp") -columns else columns)))
    let next = {*:s,expanded:s.expanded and not collapsing,page:target div size,highlight:target % size};
    result(next,[frame(next)])
}

let punctuation = {',':"，",'.':"。",'?':"？",'!':"！",'\\':"、",
    ':':"：",';':"；",'(':"（",')':"）",'[':"【",']':"】",'<':"《",'>':"》"}

fn punctuate(s,key) map^ {
    let text = if (key == "\"") (if (s.double_quote) "”" else "“")
        else if (key == "'") (if (s.single_quote) "’" else "‘") else punctuation[key]
    let next = {*:s,double_quote:if (key == "\"") not s.double_quote else s.double_quote,
        single_quote:if (key == "'") not s.single_quote else s.single_quote};
    finish(next,preedit(next) ++ text)^
}

fn shift(s,evt,kind) map^ {
    if (kind == "keydown") passed({*:s,shift_held:true,
        shift_tap:if (s.shift_held) s.shift_tap else true})
    else if (evt.shiftKey == true) passed({*:s,shift_held:true,shift_tap:false})
    else {
        let tapped = s.shift_held and s.shift_tap
        let next = {*:s,shift_held:false,shift_tap:false};
        if (not tapped) passed(next)
        else if (s.mode == "chinese") {
            let english = {*:next,mode:"english"};
            // flush the typed preedit before passing English keys to the host.
            if (active(english)) finish(english,preedit(english))^ else result(english,[frame(english)])
        } else {
            let chinese = {*:next,mode:"chinese"};
            result(chinese,[frame(chinese)])
        }
    }
}

fn keydown(engine,s,evt) map^ {
    let key = evt.key
    let letter = len(key) == 1 and key >= "a" and key <= "z"
    let upper = len(key) == 1 and key >= "A" and key <= "Z";
    if (letter or upper) {
        if (not active(s) and (evt.shiftKey == true or upper)) passed(s)
        else if (len(replace(s.raw,"'","")) >= 32) result(s,[frame(s)])
        else edit_raw(engine,s,s.caret,s.caret,lower(key))^
    } else if (active(s)) {
        let moving = not s.expanded and key in ["ArrowLeft","ArrowRight"]
        let current = if (s.dirty and not moving) refreshed(engine,s)^ else s;
        if (key == "'") {
            if (current.caret == 0 or current.raw[current.caret - 1] == "'" or
                current.raw[current.caret] == "'") result(current,[frame(current)])
            else edit_raw(engine,current,current.caret,current.caret,"'")^
        } else if (key == " ") select(engine,current,current.highlight)^
        else if (key == "Candidate" and evt.candidate_index is int)
            select_at(engine,current,evt.candidate_index +
                (if (current.expanded) 0 else current.page * current.settings.page_size))^
        else if (len(key) == 1 and key >= "1" and key <= "9") select(engine,current,int(key)^ - 1)^
        else if (moving)
            move_caret(current,if (key == "ArrowLeft") -1 else 1)
        else if (current.expanded and key in ["ArrowLeft","ArrowRight","ArrowUp","ArrowDown"])
            navigate(current,key,evt)
        else if (key == "ArrowDown") {
            let next = {*:current,expanded:len(current.items) > 0};
            result(next,[frame(next)])
        }
        else if (key == "ArrowUp") result(current,[frame(current)])
        else if (key in ["-","=","PageUp","PageDown"]) navigate(current,key,evt)
        else if (key == "Backspace") backspace(engine,current)^
        else if (key == "Delete") {
            if (current.caret < len(current.raw)) edit_raw(engine,current,current.caret,current.caret + 1,"")^
            else move_caret(current,0)
        }
        else if (key == "Enter") {
            if (current.expanded) select(engine,current,current.highlight)^
            else finish(current,preedit(current))^
        }
        else if (key == "Escape") {
            if (current.expanded) {
                let next = {*:current,expanded:false};
                result(next,[frame(next)])
            } else cancel(current)
        }
        else if (key == "\"" or punctuation[key] != null) punctuate(current,key)^
        else passed(s)
    } else if (key in ["\"","'"] or punctuation[key] != null) punctuate(s,key)^
    else passed(s)
}

pub fn key(engine,s,evt) map^ {
    let valid = valid_session(s)^
    let profile = c.require(s.profile == engine.profile,"session/engine profile mismatch")^
    let event = c.require(evt is map and evt.key is string,"key event must have a string key")^
    let kind = c.get(evt,"type","keydown")
    let next = if (evt.key != "Shift" and s.shift_held) {*:s,shift_tap:false} else s;
    if (not (kind in ["keydown","keyup"])) passed(next)
    else if (evt.ctrlKey == true or evt.altKey == true or evt.metaKey == true)
        passed({*:next,shift_tap:false,shift_held:if (evt.key == "Shift") kind == "keydown" else next.shift_held})
    else if (evt.key == "Shift") shift(next,evt,kind)^
    else if (kind == "keyup" or next.mode == "english") passed(next)
    else keydown(engine,next,evt)^
}
