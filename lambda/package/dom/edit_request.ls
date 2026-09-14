// Pure constructors for the common editing request protocol (D7.2.5).

fn value_or(value, fallback) => if (value == null) fallback else value

fn normalized_payload(data, html, mime, plaintext_only) {
    let use_html = not plaintext_only and mime == "text/html" and html != null;
    { data: value_or(data, ""), html: if (use_html) html else null,
      mime: if (use_html) "text/html" else "text/plain" }
}

fn make(descriptor, command, origin, data, html, mime, composition_phase,
        clipboard, drag, requested_caret, plaintext_only) {
    if (descriptor == null) null
    else {
        let payload = normalized_payload(data, html, mime, plaintext_only);
        { input_type: descriptor.input_type, command: command, origin: origin,
          data: payload.data, html: payload.html, mime: payload.mime,
          composition_phase: composition_phase, clipboard: clipboard, drag: drag,
          requested_caret: requested_caret, plaintext_only: plaintext_only,
          target_rule: descriptor.target_rule, event_contract: descriptor.event_contract,
          history_class: descriptor.history_class }
    }
}

pub fn from_event(evt, descriptor) {
    let base = make(descriptor, evt.command, value_or(evt.origin, "platform"), evt.data,
                    evt.html, evt.mime, evt.composition_phase, evt.clipboard, evt.drag,
                    evt.composition_caret, evt.edit_plaintext_only == true);
    // Input Events aliases such as insertFromComposition are distinct phases,
    // even when they share one descriptor family.
    if (base == null or evt.input_type == null) base
    else { *: base, input_type: evt.input_type }
}

pub fn from_command(descriptor, command_name, value, origin) =>
    make(descriptor, command_name, value_or(origin, "exec-command"), value, null,
         "text/plain", null, null, null, null, false)

pub fn from_toolbar(descriptor, payload) {
    let base = make(descriptor, descriptor.name, "toolbar", payload.data, payload.html,
                    payload.mime, payload.composition_phase, payload.clipboard, payload.drag,
                    payload.requested_caret, payload.plaintext_only == true);
    if (base == null) null else { *: payload, *: base }
}

pub fn from_replay(descriptor, record) =>
    make(descriptor, record.command, "replay", record.data, record.html, record.mime,
         record.composition_phase, record.clipboard, record.drag,
         record.requested_caret, record.plaintext_only == true)
