// Form submission policy (HTML 4.10.21.3). Native supplies association,
// validity events, entry construction, and GET URL construction. This module
// selects the submission method and serializes POST bodies.
import dom
import ue: lambda.dom.urlencode

fn attr_or(elem, name, fallback) {
    let value = dom.get_attribute(elem, name);
    if (value == null or value == "") fallback else value
}

fn submit_attr(form, submitter, submitter_name, form_name, fallback) {
    let submitter_value = if (submitter == null) null
        else dom.get_attribute(submitter, submitter_name);
    if (submitter_value != null and submitter_value != "") submitter_value
    else attr_or(form, form_name, fallback)
}

fn encoded_pair(pair) any^ {
    ue.form_encode(string(pair[0])) ++ "=" ++
        ue.form_encode(string(pair[1]))
}

fn urlencoded(entries) {
    join([for (pair in entries) encoded_pair(pair)], "&")
}

fn multipart_part(pair, boundary) any^ {
    "--" ++ boundary ++ "\r\n" ++
    "Content-Disposition: form-data; name=\"" ++ string(pair[0]) ++ "\"\r\n\r\n" ++
    string(pair[1]) ++ "\r\n"
}

fn multipart(entries, boundary) {
    join([for (pair in entries) multipart_part(pair, boundary)], "") ++
        "--" ++ boundary ++ "--\r\n"
}

// Button activation and implicit Enter use the same guarded submission stages.
pub pn run(form, submitter) {
    if (form == null) { 'pass' }
    else if (not dom.form_prepare_submit(form, submitter)) { 'prevent-default' }
    else {
        let entries = dom.form_submit_entries(form, submitter);
        if (entries != null) {
            // Form data listeners can change navigation attributes synchronously.
            let method_name = dom.form_submit_method(form, submitter);
            let enctype = lower(submit_attr(form, submitter,
                "formenctype", "enctype", "application/x-www-form-urlencoded"));
            let action = submit_attr(form, submitter,
                "formaction", "action", dom.form_url(form));
            let target = dom.form_submit_target(form, submitter);

            if (method_name == "dialog") {
                dom.form_close_dialog(form, submitter)
            }
            else if (method_name == "get") {
                let url = dom.form_get_url(form, submitter, entries);
                if (url != null) {
                    dom.request_navigation({target: target, url: url,
                        method: "get", enctype: enctype, body: ""})
                }
            }
            else if (enctype == "multipart/form-data") {
                let boundary = dom.form_boundary();
                dom.request_navigation({target: target, url: action,
                    method: method_name, enctype: enctype,
                    body: multipart(entries, boundary)})
            }
            else {
                dom.request_navigation({target: target, url: action,
                    method: method_name, enctype: "application/x-www-form-urlencoded",
                    body: urlencoded(entries)})
            }
        }
        'prevent-default'
    }
}

pub pn reset(form) {
    if (form == null) { 'pass' }
    else {
        dom.reset_form(form)
        'prevent-default'
    }
}
