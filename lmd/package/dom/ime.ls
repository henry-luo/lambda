// The IME composition session (F7, ES18).
//
// The session binds to the html body/page rather than to a control, and that is
// the point rather than a convenience: a document has at most one composition
// at a time, so the session's cardinality is the document's. Binding it to the
// control would put it back on FormControlProp — the transient owner whose
// lifetime already cost us the open dropdown (ESO28) and the undo ring (ESO43).
//
// The preedit is not part of any control's value. It is a separate document
// state the renderer draws inline, so the engine still owns the buffer and the
// painting; what lives here is the session policy.
//
// Commit *content* is not handled here — it arrives at the applier as
// `insertFromComposition` and obeys the same newline and maxlength rules as
// typing (F6).
import dom
import chinese: lambda.ime.chinese
import reducer: lambda.ime.chinese.session

// A composition starting while another is still open means the previous one was
// orphaned — the platform can drop a session without ending it. Begin from a
// clean slate rather than appending to a stale preedit.
pub pn begin(body) {
    // an OS composition supersedes package key policy (IME10).
    if (not dom.ime_status(body).scripted) { forget_preedit(body) }
    dom.clear_ime_preedit(body)
}

pub pn update(body, evt, target) {
    let text = if (evt.data == null) "" else evt.data;
    if (len(text) == 0) {
        dom.clear_ime_preedit(body)
    }
    else {
        dom.set_ime_preedit(body, text, evt.composition_caret)
        // The placeholder must go while composing even though the value is
        // still empty — the user is visibly typing into the control, and a
        // placeholder drawn under the preedit reads as garbage.
        if (target != null) { dom.set_state(target, "placeholder_shown", false) }
        else { false }
    }
}

// End covers both commit and cancel: either way the session is over and the
// preedit stops being drawn. A commit's text lands through the applier.
pub pn end(body) {
    dom.clear_ime_preedit(body)
}


// D7.2.5/IME-O1: the body adapter owns all key, settings and learning policy.
fn is_enabled(body,status) bool {
    let override = dom.get_attribute(body,"data-ime");
    if (override == "off") false else if (override == "chinese") true else status.enabled
}

fn accepts(body,status) bool => is_enabled(body,status) and status.target != null and
    not dom.get_state(status.target,"disabled") and not dom.get_state(status.target,"readonly")

pn present_popup(body,target,frame,mode) {
    // configuration popovers keep editor focus but temporarily own the overlay surface.
    if (dom.has_attribute(body,"data-ime-popup-suspended")) dom.ime_popup(body,null,"off")
    else if (target != null) dom.ime_popup(target,frame,mode)
}

pub pn suspend_popup(body,suspended:bool) {
    if (suspended) dom.set_attribute(body,"data-ime-popup-suspended","")
    else if (dom.has_attribute(body,"data-ime-popup-suspended"))
        dom.remove_attribute(body,"data-ime-popup-suspended")
    let status = dom.ime_status(body)
    let current = dom.ime_session(body);
    if (current != null and accepts(body,status) and dom.same_node(current.target,status.target) and
        (not status.active or status.scripted)) {
        // snapshot the existing frame; restoring presentation must not requery or reset selection.
        present_popup(body,current.target,reducer.frame(current.session),current.session.mode)
    } else dom.ime_popup(body,null,"off")
}

fn preferences(body) map^ {
    let fuzzy = dom.get_attribute(body,"data-ime-fuzzy")
    let size = dom.get_attribute(body,"data-ime-page-size");
    {fuzzy:if (fuzzy == null or fuzzy == "") "none"
        else if (fuzzy in ["none","all"]) fuzzy else split(fuzzy,","),
        *:(if (size == null) {} else {page_size:int(size)^})}
}

fn reset(current) map^ {
    let canceled = chinese.key(current.engine,current.session,{type:"keydown",key:"Escape"})^;
    // focus changes cancel text, while mode, quote pairing, settings and learning stay document-owned.
    {*:current,session:{*:canceled.session,shift_held:false,shift_tap:false},active:false}
}

pn emit_step(body,current,step) map {
    var active = current.active;
    for (action in step.actions) {
        if (action.type == "compose" or action.type == "commit") {
            if (not active) { dom.compose(current.target,"begin","",0) }
            dom.compose(current.target,if (action.type == "compose") "update" else "commit",
                action.text,if (action.type == "compose") action.caret else 0)
            active = action.type == "compose"
        } else if (action.type == "cancel") {
            if (active) { dom.compose(current.target,"cancel","",0) }
            active = false
        } else if (action.type == "candidates") {
            // settings may be changed before any editing surface has focus.
            present_popup(body,current.target,action,step.session.mode)
        }
    }
    let next = {*:current,session:step.session,active:active};
    dom.set_ime_session(body,next)
    next
}

pub pn forget_preedit(body) any^ {
    let current = dom.ime_session(body);
    if (current != null) { dom.set_ime_session(body,reset(current)^) }
    dom.ime_popup(body,null,"off")
}

pn acquire(body,status) map^ {
    let current = dom.ime_session(body)
    let options = preferences(body)^
    let directory = dom.get_attribute(body,"data-ime-directory")
    let requested = dom.get_attribute(body,"data-ime-profile")
    let profile = if (requested == null) "zh-Hans" else requested
    let fresh = current == null or current.directory != directory or current.profile != profile
    let engine = if (not fresh) current.engine
        else chinese.load(profile,if (directory == null) null else {directory:directory})^;
    // finish the old provisional range before a new profile can begin composing.
    if (fresh and current != null) { focus_out(body)^ }
    let session = if (fresh) chinese.session(engine,options)^ else current.session
    let initial = if (fresh) {engine:engine,session:session,target:status.target,
        active:false,directory:directory,profile:profile,options:options} else current
    let moved = not dom.same_node(initial.target,status.target)
    let next = if (moved) {*:reset(initial)^,target:status.target} else initial;
    if (not fresh and options != next.options) {
        let changed = chinese.configure(engine,next.session,options)^;
        return emit_step(body,{*:next,options:options},changed)
    }
    dom.set_ime_session(body,next)
    return next
}

pub pn key(body,evt) any^ {
    let status = dom.ime_status(body);
    if (not accepts(body,status)) {
        focus_out(body)^
        return 'pass'
    }
    if (status.active and not status.scripted) {
        forget_preedit(body)^
        return 'pass'
    }
    let current = acquire(body,status)^
    // native event properties are getter-backed; a spread cannot copy their key payload.
    let keyboard = {type:evt.type,key:evt.key,shiftKey:evt.shiftKey,ctrlKey:evt.ctrlKey,
        altKey:evt.altKey,metaKey:evt.metaKey,candidate_index:evt.candidate_index,
        candidate_columns:status.candidate_columns}
    let step = chinese.key(current.engine,current.session,keyboard)^
    let next = emit_step(body,current,step);
    if (any([for (action in step.actions) action.type == "pass"])) 'pass' else 'prevent-default'
}

pub pn focus_in(body) any^ {
    let status = dom.ime_status(body);
    if (not accepts(body,status)) { return focus_out(body)^ }
    if (not status.active or status.scripted) {
        let current = acquire(body,status)^;
        if (not current.active) { present_popup(body,current.target,null,current.session.mode) }
    }
    return 'pass'
}

// Only the unprevented pointer default cancels; toolbar mousedown can preserve preedit.
pub pn pointer_press(body) any^ {
    let current = dom.ime_session(body);
    if (current != null and current.active) { focus_out(body)^ }
}

pub pn focus_out(body) any^ {
    let current = dom.ime_session(body);
    if (current != null and (current.active or current.target != null)) {
        if (current.active) { dom.compose(current.target,"cancel","",0) }
        dom.set_ime_session(body,{*:reset(current)^,target:null})
    }
    dom.ime_popup(body,null,"off")
    return 'pass'
}

pub pn popup_action(body,evt) any^ {
    if (evt.command == "cancel") { return focus_out(body)^ }
    if (evt.command == "toggle") {
        key(body,{type:"keydown",key:"Shift"})^
        return key(body,{type:"keyup",key:"Shift"})^
    }
    return key(body,{type:"keydown",key:"Candidate",candidate_index:evt.option_index})^
}

pub pn enable(body,enabled:bool) any^ {
    dom.set_attribute(body,"data-ime",if (enabled) "chinese" else "off")
    if (enabled) { return focus_in(body)^ }
    return focus_out(body)^
}

pub pn configure(body,settings) any^ {
    let status = dom.ime_status(body)
    let current = acquire(body,status)^
    let step = chinese.configure(current.engine,current.session,settings)^
    let checked = chinese.settings(step.session)^
    dom.set_attribute(body,"data-ime-fuzzy",if (len(checked.fuzzy) == 0) "none" else join(checked.fuzzy,","))
    dom.set_attribute(body,"data-ime-page-size",string(checked.page_size))
    return emit_step(body,{*:current,options:preferences(body)^},step)
}

pub fn session(body) => dom.ime_session(body).session

pub pn with_user(body,user) any^ {
    let current = acquire(body,dom.ime_status(body))^
    let next = {*:current,session:chinese.with_user(current.session,user)^};
    return dom.set_ime_session(body,next)
}
