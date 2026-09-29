// Representation-neutral semantic action selected from a common request.

pub fn lower_request(request, descriptor) =>
    if (request == null or descriptor == null) null
    else {
        descriptor: descriptor,
        family: descriptor.family,
        payload: {data: request.data, html: request.html, mime: request.mime,
                  composition_phase: request.composition_phase,
                  clipboard: request.clipboard, drag: request.drag,
                  requested_caret: request.requested_caret},
        target_rule: request.target_rule,
        history_class: request.history_class,
        event_contract: request.event_contract,
        origin: request.origin
    }
