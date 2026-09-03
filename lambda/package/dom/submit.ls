// Form submission policy (HTML 4.10.21.3). Native supplies association,
// validity events, entry construction, and the navigation waist; this module
// chooses the submitter overrides and serialization format.
import radiant
import dom
import ue: lambda.package.dom.urlencode

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

fn encoded_pair(pair) {
    ue.form_encode(string(pair[0])) ++ "=" ++
        ue.form_encode(string(pair[1]))
}

fn urlencoded(entries) {
    join(for (pair in entries) encoded_pair(pair), "&")
}

fn multipart_part(pair, boundary) {
    "--" ++ boundary ++ "\r\n" ++
    "Content-Disposition: form-data; name=\"" ++ string(pair[0]) ++ "\"\r\n\r\n" ++
    string(pair[1]) ++ "\r\n"
}

fn multipart(entries, boundary) {
    join(for (pair in entries) multipart_part(pair, boundary), "") ++
        "--" ++ boundary ++ "--\r\n"
}

fn validation_enabled(form, submitter) {
    let form_skip = dom.get_attribute(form, "novalidate");
    let submitter_skip = if (submitter == null) null
        else dom.get_attribute(submitter, "formnovalidate");
    (form_skip == null or form_skip == "") and
        (submitter_skip == null or submitter_skip == "")
}

// One submit pipeline for button activation and implicit Enter.
pub fn run(form, submitter) {
    if (form == null) { 'pass' }
    else if (validation_enabled(form, submitter) and
             not radiant.check_validity(form)) {
        'prevent-default'
    }
    else if (not radiant.submit_event(form, submitter)) {
        'prevent-default'
    }
    else {
        // submit is cancelable and must precede serialization/navigation.
        let entries = radiant.form_entries(form, submitter);
        let method_name = lower(submit_attr(form, submitter,
            "formmethod", "method", "get"));
        let enctype = lower(submit_attr(form, submitter,
            "formenctype", "enctype", "application/x-www-form-urlencoded"));
        let action = submit_attr(form, submitter,
            "formaction", "action", radiant.form_url(form));
        let target = attr_or(form, "target", "_self");

        if (method_name == "get") {
            let query = urlencoded(entries);
            let separator = if (index_of(action, "?") == null) "?" else "&";
            let url = if (query == "") action else action ++ separator ++ query;
            dom.request_navigation({target: target, url: url,
                method: "get", enctype: enctype, body: ""})
        }
        else if (enctype == "multipart/form-data") {
            let boundary = radiant.form_boundary();
            dom.request_navigation({target: target, url: action,
                method: method_name, enctype: enctype,
                body: multipart(entries, boundary)})
        }
        else {
            dom.request_navigation({target: target, url: action,
                method: method_name,
                enctype: "application/x-www-form-urlencoded",
                body: urlencoded(entries)})
        }
        'prevent-default'
    }
}

pub fn reset(form) {
    if (form == null) { 'pass' }
    else {
        radiant.reset_form(form)
        'prevent-default'
    }
}
