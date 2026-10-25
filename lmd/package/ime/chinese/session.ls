import c: .common
import syllables: .syllables
import query: .query
import learned: .user

fn checked_settings(value) map^ {
    let opts = c.options(value,["fuzzy","page_size"])^
    let pairs = syllables.settings(opts.fuzzy)^;
    {fuzzy:[for (pair in pairs) pair.name],page_size:c.bounded(c.get(opts,"page_size",9),1,9,"page_size")^}
}

fn valid_session(value) bool^ => c.require(value is map and value.kind == "lambda.ime.chinese.session",
    "expected an IME session")^
fn active(s) bool => len(s.raw) > 0 or len(s.chosen) > 0
fn preedit(s) string => join([for (item in s.chosen) item.text],"") ++ s.raw
fn pages(s) int => (len(s.items) + s.settings.page_size - 1) div s.settings.page_size
fn frame(s) map => {type:"candidates",items:slice(s.items,s.page * s.settings.page_size,
    (s.page + 1) * s.settings.page_size),page:s.page,pages:pages(s),highlight:s.highlight}
fn result(s,actions) map => {session:s,actions:actions}
fn passed(s) map => result(s,[{type:"pass"}])
fn cleared(s) map => {*:s,raw:"",chosen:[],items:[],page:0,highlight:0,truncated:false,dirty:false}

pub fn create(engine,value = null) map^ {
    let opts = checked_settings(value)^;
    {kind:"lambda.ime.chinese.session",profile:engine.profile,syllables:engine.syllables,
        settings:opts,mode:"chinese",raw:"",chosen:[],items:[],page:0,highlight:0,
        truncated:false,dirty:false,shift_held:false,shift_tap:false,
        double_quote:false,single_quote:false,user:learned.empty(engine.profile)}
}

fn refreshed(engine,s) map^ {
    let found = query.candidates(engine,s.raw,{fuzzy:s.settings.fuzzy},s.user)^;
    {*:s,items:found.items,page:0,highlight:0,truncated:found.truncated,dirty:false}
}

fn composing(engine,s) map^ {
    let next = refreshed(engine,s)^;
    result(next,[{type:"compose",text:preedit(next),caret:len(preedit(next))},frame(next)])
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
    let next = {*:s,settings:checked_settings(value)^}
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

fn select(engine,s,index) map^ {
    let at = s.page * s.settings.page_size + index;
    if (index < 0 or index >= s.settings.page_size or at >= len(s.items)) result(s,[frame(s)])
    else {
        let item = s.items[at]
        let choice = {text:item.text,reading:item.reading,raw:slice(s.raw,0,item.consumed)}
        let next = {*:s,raw:slice(s.raw,item.consumed),chosen:[*s.chosen,choice]};
        if (len(next.raw) == 0) finish(next,preedit(next))^ else composing(engine,next)^
    }
}

fn backspace(engine,s) map^ {
    if (len(s.raw) > 0) {
        let next = {*:s,raw:slice(s.raw,0,len(s.raw) - 1)};
        if (not active(next)) cancel(next) else composing(engine,next)^
    } else if (len(s.chosen) > 0) {
        let previous = s.chosen[len(s.chosen) - 1]
        let next = {*:s,raw:previous.raw,chosen:take(s.chosen,len(s.chosen) - 1)};
        composing(engine,next)^
    } else cancel(s)
}

fn navigate(s,key) map {
    let size = s.settings.page_size
    let absolute = s.page * size + s.highlight
    let target = if (key in ["-","PageUp"]) max(0,s.page - 1) * size
        else if (key in ["=","PageDown"]) max(0,min(pages(s) - 1,s.page + 1)) * size
        else if (key in ["ArrowLeft","ArrowUp"]) max(0,absolute - 1)
        else min(max(0,len(s.items) - 1),absolute + 1)
    let next = {*:s,page:target div size,highlight:target % size};
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
        else composing(engine,{*:s,raw:s.raw ++ lower(key)})^
    } else if (active(s)) {
        let current = if (s.dirty) refreshed(engine,s)^ else s;
        if (key == "'") {
            if (len(current.raw) == 0 or ends_with(current.raw,"'")) result(current,[frame(current)])
            else composing(engine,{*:current,raw:current.raw ++ "'"})^
        } else if (key == " ") select(engine,current,current.highlight)^
        else if (len(key) == 1 and key >= "1" and key <= "9") select(engine,current,int(key)^ - 1)^
        else if (key in ["-","=","PageUp","PageDown","ArrowLeft","ArrowRight","ArrowUp","ArrowDown"])
            navigate(current,key)
        else if (key == "Backspace") backspace(engine,current)^
        else if (key == "Enter") finish(current,preedit(current))^
        else if (key == "Escape") cancel(current)
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
