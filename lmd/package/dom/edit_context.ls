// Side-effect-free facts for one DOM editing invocation (D7.2.5).  Command
// planners read this record; native mechanism must not infer policy from it.
import dom

fn has_token(evt) {
    evt.edit_token != null and evt.edit_token != 0
}

pub fn build(host, evt, descriptor) {
    let token = if (has_token(evt)) evt.edit_token else null;
    let start_container = if (token == null) null
                          else dom.edit_start_container(host, token);
    let end_container = if (token == null) null
                        else dom.edit_end_container(host, token);
    let start_offset = if (token == null) null else dom.edit_start(host, token);
    let end_offset = if (token == null) null else dom.edit_end(host, token);
    let mutation_epoch = if (token == null) null else dom.edit_epoch(host, token);
    let supported = descriptor != null;
    let requires_host = if (descriptor == null) true else descriptor.requires_host;
    // Event payload members preserve a DOM Boolean; use it directly so an
    // absent member remains falsey without coercing a host-backed value.
    let plaintext_only = evt.edit_plaintext_only;
    let plaintext_allowed = descriptor == null or descriptor.plaintext_rule != "disable";
    let enabled = supported and (not requires_host or token != null) and
                  (not plaintext_only or plaintext_allowed);
    {
        host: host,
        token: token,
        descriptor: descriptor,
        supported: supported,
        enabled: enabled,
        plaintext_only: plaintext_only,
        operation: if (evt.edit_query != null) "query" else "execute",
        command: evt.command,
        value: evt.value,
        // Only platform paste carries HTML. Keep absent event members out of
        // the common typing context so non-clipboard model handlers receive
        // the same record shape they had before this family was added.
        html: null,
        start_container: start_container,
        end_container: end_container,
        start: start_offset,
        end: end_offset,
        collapsed: start_offset != null and start_offset == end_offset,
        // The opaque invocation validates this captured epoch before every
        // mutation; package code only carries the fact into its plan.
        mutation_epoch: mutation_epoch
    }
}
