#include "terminal_host.h"

#include "transpiler.hpp"
#include "template_registry.h"
#include "template_host.h"
#include "template_state.h"
#include "radiant_event_hook.h"
#include "lambda-root-frame.hpp"
#include "heap_api.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include <string.h>

struct TerminalSession {
    TemplateHostSession* mounted;
    char* submitted_line;
    int64_t frame_id;
    int wait_ms;
    bool ended;
    bool interrupted;
    bool ready;
};

static Item terminal_model(TerminalSession* session) {
    return {.item = template_host_session_root_word(session->mounted)};
}

static Item terminal_active_frame(TerminalSession* session) {
    Item model = terminal_model(session);
    TemplateEntry* entry = template_registry_match(g_template_registry,
        model, false, nullptr);
    const char* active_name = template_entry_state_name(entry, "active_frame");
    Item active = active_name
        ? tmpl_state_get(model, entry->template_ref, active_name) : ItemNull;
    return get_type_id(active) == LMD_TYPE_NULL
        ? item_attr(model, "frame") : active;
}

static Item terminal_emit(void* receiver, Item event_name, Item event_data) {
    TerminalSession* session = (TerminalSession*)receiver;
    const char* event = event_name.get_chars();
    if (!session || !event) return ItemNull;
    Item model = terminal_model(session);
    bool handled = false;
    if (strcmp(event, "readline_frame_event") == 0) {
        Item target = terminal_active_frame(session);
        Item name = item_attr(event_data, "name");
        return template_dispatch_event(target, true, name.get_chars(),
            item_attr(event_data, "event"), &handled);
    }
    if (strcmp(event, "readline_line") == 0) {
        Item line_item = item_attr(event_data, "text");
        String* line = line_item.get_string();
        if (!line) return ItemError;
        char* copied = (char*)mem_alloc((size_t)line->len + 1, MEM_CAT_SYSTEM);
        if (!copied) return ItemError;
        memcpy(copied, line->chars, line->len);
        copied[line->len] = '\0';
        mem_free(session->submitted_line);
        session->submitted_line = copied;
        return ItemNull;
    }
    if (strcmp(event, "readline_close") == 0) {
        session->ended = true;
        return ItemNull;
    }
    if (strcmp(event, "readline_cancel") == 0) {
        session->interrupted = true;
        return ItemNull;
    }
    // The receiver routes template events only; edit policy stays in Lambda.
    if (strncmp(event, "readline_", 9) == 0) {
        return template_dispatch_event(model, false, event, event_data, &handled);
    }
    return ItemNull;
}

static Item terminal_event_map(int64_t frame_id, const char* prompt) {
    RootFrame roots(3);
    Rooted<Item> event(roots, vmap_new());
    Rooted<Item> key(roots, ItemNull);
    Rooted<Item> value(roots, ItemNull);
    key.set({.item = s2it(heap_strcpy("frame_id", 8))});
    value.set({.item = i2it(frame_id)});
    vmap_set(event.get(), key.get(), value.get());
    key.set({.item = s2it(heap_strcpy("prompt", 6))});
    value.set({.item = s2it(heap_strcpy(prompt, strlen(prompt)))});
    vmap_set(event.get(), key.get(), value.get());
    return event.get();
}

static bool terminal_write_all(const TerminalTransport* transport,
                               String* output) {
    if (!output) return false;
    size_t accepted = 0;
    while (accepted < output->len) {
        int64_t written = transport->write(transport->device,
            output->chars + accepted, output->len - accepted);
        if (written == -2) continue;
        if (written <= 0) return false;
        accepted += (size_t)written;
    }
    return true;
}

static bool terminal_present(TerminalSession* session,
                             const TerminalTransport* transport) {
    RootFrame roots(1);
    Rooted<Item> snapshot(roots, fn_apply1(terminal_model(session)));
    if (get_type_id(snapshot.get()) != LMD_TYPE_ELEMENT) return false;
    Item bytes = item_attr(snapshot.get(), "bytes");
    String* output = bytes.get_string();
    if (!output || !terminal_write_all(transport, output)) return false;
    Item timeout = item_attr(snapshot.get(), "wait_ms");
    session->wait_ms = get_type_id(timeout) == LMD_TYPE_INT
        ? (int)it2i(timeout) : -1;
    if (output->len > 0) {
        bool handled = false;
        template_dispatch_event(terminal_model(session), false,
            "readline_presented", snapshot.get(), &handled);
        if (!handled) return false;
    }
    return true;
}

static bool terminal_feed(TerminalSession* session, const char* bytes,
                          size_t length) {
    RootFrame roots(1);
    Binary* copied = heap_binary_from_bytes(bytes, (int64_t)length);
    if (!copied) return false;
    Rooted<Item> packet(roots, {.item = x2it(copied)});
    LambdaEmitScope scope = {};
    lambda_emit_scope_enter(&scope, terminal_emit, session);
    bool handled = false;
    template_dispatch_event(terminal_model(session), false,
        "readline_bytes", packet.get(), &handled);
    lambda_emit_scope_leave(&scope);
    return handled;
}

static bool terminal_simple_event(TerminalSession* session,
                                  const char* event_name, Item event_data) {
    LambdaEmitScope scope = {};
    lambda_emit_scope_enter(&scope, terminal_emit, session);
    bool handled = false;
    template_dispatch_event(terminal_model(session), false,
        event_name, event_data, &handled);
    lambda_emit_scope_leave(&scope);
    return handled;
}

TerminalSession* terminal_session_open(bool is_tty, int columns) {
    TerminalSession* session = (TerminalSession*)mem_calloc(1,
        sizeof(TerminalSession), MEM_CAT_SYSTEM);
    if (!session) return nullptr;
    const char* source =
        "import readline: lambda.io.terminal\n"
        "readline.terminal(\"session\", 0, \"\")\n";
    session->mounted = template_host_session_open(source, "<terminal-session>");
    if (session->mounted) {
        TemplateHostBinding binding(session->mounted);
        session->ready = binding.valid() &&
            terminal_simple_event(session, "readline_device",
                {.item = b2it(is_tty)}) &&
            terminal_simple_event(session, "readline_resize",
                {.item = i2it(columns > 0 ? columns : 80)});
    }
    if (!session->ready) {
        log_error("terminal-host: could not mount lambda.io.terminal");
        terminal_session_close(session);
        return nullptr;
    }
    return session;
}

TerminalReadResult terminal_session_readline(TerminalSession* session,
                                             const TerminalTransport* transport,
                                             const char* prompt) {
    if (!session || !session->ready || !transport || !transport->wait ||
            !transport->read || !transport->write || !prompt)
        return {TERMINAL_READ_ERROR, nullptr};
    TemplateHostBinding binding(session->mounted);
    if (!binding.valid()) return {TERMINAL_READ_ERROR, nullptr};
    Item next = terminal_event_map(++session->frame_id, prompt);
    if (!terminal_simple_event(session, "readline_next", next))
        return {TERMINAL_READ_ERROR, nullptr};
    if (transport->is_tty && transport->set_raw &&
            transport->set_raw(transport->device, true) != 0)
        return {TERMINAL_READ_ERROR, nullptr};

    bool healthy = true;
    while (!session->submitted_line && !session->ended && !session->interrupted) {
        if (!terminal_present(session, transport)) { healthy = false; break; }
        int ready = transport->wait(transport->device, session->wait_ms);
        if (ready == -2) continue;
        if (ready == -3 && transport->size) {
            int rows = 0;
            int columns = 0;
            transport->size(transport->device, &rows, &columns);
            if (!terminal_simple_event(session, "readline_resize",
                    {.item = i2it(columns)})) healthy = false;
            if (!healthy) break;
            continue;
        }
        if (ready == 0) {
            if (!terminal_simple_event(session, "readline_flush", ItemNull)) {
                healthy = false; break;
            }
            continue;
        }
        if (ready < 0) { healthy = false; break; }
        // Stop at a submitted frame so bytes already queued for the next
        // frame cannot replace the current line outcome.
        char byte = 0;
        int64_t amount = transport->read(transport->device, &byte, 1);
        if (amount == -2) continue;
        if (amount == -3 && transport->size) {
            int rows = 0;
            int columns = 0;
            transport->size(transport->device, &rows, &columns);
            if (!terminal_simple_event(session, "readline_resize",
                    {.item = i2it(columns)})) healthy = false;
            if (!healthy) break;
            continue;
        }
        if (amount < 0) { healthy = false; break; }
        if (amount == 0) {
            terminal_simple_event(session, "readline_flush", ItemNull);
            terminal_simple_event(session, "readline_end", ItemNull);
            break;
        }
        if (!terminal_feed(session, &byte, 1)) {
            healthy = false; break;
        }
    }

    if (transport->is_tty) {
        RootFrame roots(1);
        Rooted<Item> snapshot(roots, fn_apply1(terminal_model(session)));
        const char* release_name = !healthy ? "release_bytes"
            : session->interrupted ? "interrupt_bytes"
            : session->submitted_line ? "finish_bytes" : "close_bytes";
        Item release = item_attr(snapshot.get(), release_name);
        if (!terminal_write_all(transport, release.get_string())) healthy = false;
    }
    if (transport->is_tty && transport->set_raw) {
        if (transport->set_raw(transport->device, false) != 0) healthy = false;
    }
    if (!healthy) {
        log_error("terminal-host: input or output transport failed");
        mem_free(session->submitted_line);
        session->submitted_line = nullptr;
    }
    char* line = session->submitted_line;
    session->submitted_line = nullptr;
    bool interrupted = session->interrupted;
    session->interrupted = false;
    return {healthy ? line ? TERMINAL_READ_LINE
                           : interrupted ? TERMINAL_READ_INTERRUPTED
                                         : TERMINAL_READ_EOF
                    : TERMINAL_READ_ERROR, line};
}

void terminal_session_close(TerminalSession* session) {
    if (!session) return;
    template_host_session_close(session->mounted);
    mem_free(session->submitted_line);
    mem_free(session);
}
