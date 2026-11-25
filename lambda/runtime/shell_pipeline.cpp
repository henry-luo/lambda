#include "shell_pipeline.hpp"

#if !defined(LAMBDA_NO_TASKS) && !defined(LAMBDA_NO_FILE_IO)
#include "../lambda.hpp"
#include "concurrency.h"
#include "gc/gc_heap.h"
#include "io_file_search.hpp"
#include "lambda-root-frame.hpp"
#include "transpiler.hpp"
#include "lib/memtrack.h"
#include "lib/file.h"
#include "lib/uv_loop.h"
#include "lib/utf.h"
#include "lambda/core/print.h"

#include <errno.h>
#include <fcntl.h>
#include <limits.h>
#include <signal.h>
#include <stdio.h>
#include <string.h>
#include <uv.h>

#ifndef _WIN32
#include <sys/stat.h>
#else
#include <io.h>
#endif

extern "C" StrBuf* lambda_get_local_path_from_item(Item item);
extern __thread EvalContext* context;

namespace {

struct ShellOutput {
    LambdaTask* owner;
    LambdaTask* waiter;
    LambdaTask* writer_waiter;
    LambdaTask* record_waiter;
    LambdaTask* feeder;
    ShellOutput* previous;
    ShellOutput* following;
    ShellOutput* next_owned;
    uv_process_t process;
    uv_timer_t deadline;
    uint64_t start_ms;
    uv_pipe_t reader;
    uv_pipe_t writer;
    uv_write_t request;
#ifdef SIGPIPE
    uv_signal_t sigpipe_watcher;
    bool sigpipe_protected;
#endif
    uv_file output;
    StrBuf* input;
    StrBuf* line;
    FILE* file;
    FileAnchor* anchor;
    FileAtomicWriter* staged_file;
    Item source;
    Item callback;
    Item pipe_root;
    uint64_t pipe_root_scalar;
    Item pipe_error;
    uint64_t pipe_error_scalar;
    Item feed_error;
    size_t cursor;
    int64_t index;
    unsigned refs;
    bool inherited_stdin;
    bool deadline_ready;
    bool timed_out;
    bool record_stage;
    bool filter;
    bool eof;
    bool running;
    bool reading;
    bool writing;
    bool writer_ready;
    bool feed_records;
    bool feed_eof;
    bool feed_cancelled_early;
    bool killed_for_early_exit;
    bool claimed;
    int signal;
    int64_t exit_code;
    int write_error;
    int write_status;
    ssize_t read_result;
    char* buffer;
};

char output_brand;
char task_outputs_key;

void close_file(uv_file file) {
    if (file < 0) return;
    uv_fs_t request;
    uv_fs_close(NULL, &request, file, NULL);
    uv_fs_req_cleanup(&request);
}

uv_file open_file(const char* path, int flags) {
    uv_fs_t request;
    int fd = uv_fs_open(NULL, &request, path, flags, 0666, NULL);
    uv_fs_req_cleanup(&request);
    return fd;
}

void release_output(void* ptr) {
    ShellOutput* output = (ShellOutput*)ptr;
    if (--output->refs == 0) {
        mem_free(output->buffer);
        if (output->input) strbuf_free(output->input);
        if (output->line) strbuf_free(output->line);
        mem_free(output);
    }
}

void handle_closed(uv_handle_t* handle) {
    ShellOutput* output = (ShellOutput*)handle->data;
    if (handle == (uv_handle_t*)&output->reader) {
        mem_free(output->buffer);
        output->buffer = NULL;
    }
    release_output(output);
}

void close_handle(uv_handle_t* handle) {
    if (!uv_is_closing(handle)) uv_close(handle, handle_closed);
}

int protect_pipe_writes(ShellOutput* output) {
#ifdef SIGPIPE
    if (output->sigpipe_protected) return 0;
    int status = uv_signal_init(lambda_uv_loop(), &output->sigpipe_watcher);
    if (status < 0) return status;
    output->sigpipe_watcher.data = output;
    ++output->refs;
    // A closed child pipe is a write error in Lambda. Caught signals revert
    // to their default disposition across exec, so children retain SIGPIPE.
    status = uv_signal_start(&output->sigpipe_watcher, [](uv_signal_t*, int) {}, SIGPIPE);
    if (status < 0) close_handle((uv_handle_t*)&output->sigpipe_watcher);
    else {
        uv_unref((uv_handle_t*)&output->sigpipe_watcher);
        output->sigpipe_protected = true;
    }
    return status;
#else
    (void)output;
    return 0;
#endif
}

void kill_output(ShellOutput* output) {
    if (!output->running) return;
#ifndef _WIN32
    if (uv_kill(-output->process.pid, SIGKILL) == 0) return;
#endif
    uv_process_kill(&output->process, SIGKILL);
}

void close_stdin(ShellOutput* output) {
    if (!output->writer_ready) return;
    output->writer_ready = false;
    close_handle((uv_handle_t*)&output->writer);
}

void close_deadline(ShellOutput* output) {
    if (!output->deadline_ready) return;
    output->deadline_ready = false;
    close_handle((uv_handle_t*)&output->deadline);
}

void abort_file(ShellOutput* output) {
    if (output->staged_file) {
        file_anchor_write_abort(output->staged_file);
        output->staged_file = NULL;
    }
    if (output->anchor) {
        file_anchor_close(output->anchor);
        output->anchor = NULL;
    }
    if (output->file) {
        fclose(output->file);
        output->file = NULL;
    }
}

void stop_output(ShellOutput* output) {
    close_deadline(output);
    abort_file(output);
    if (output->feeder) lambda_task_cancel(output->feeder);
    close_file(output->output);
    output->output = -1;
    if (output->reading) {
        uv_read_stop((uv_stream_t*)&output->reader);
        output->reading = false;
        close_handle((uv_handle_t*)&output->reader);
    }
    close_stdin(output);
#ifdef SIGPIPE
    if (output->sigpipe_protected) {
        output->sigpipe_protected = false;
        close_handle((uv_handle_t*)&output->sigpipe_watcher);
    }
#endif
    kill_output(output);
}

void release_outputs(void* ptr) {
    ShellOutput* stage = (ShellOutput*)ptr;
    while (stage) {
        ShellOutput* next = stage->next_owned;
        stage->owner = NULL;
        stage->waiter = stage->writer_waiter = NULL;
        stop_output(stage);
        release_output(stage);
        stage = next;
    }
}

void trace_output(void* ptr, gc_heap* gc) {
    ShellOutput* output = (ShellOutput*)ptr;
    gc_mark_item(gc, output->source.item);
    gc_mark_item(gc, output->callback.item);
    gc_mark_item(gc, output->pipe_root.item);
    gc_mark_item(gc, output->pipe_error.item);
    gc_mark_item(gc, output->feed_error.item);
}

VMapVtable output_vtable = {
    [](void*, Item) { return ItemNull; }, nullptr,
    [](void*) -> int64_t { return 0; },
    [](void*) -> SymbolKeyList* { return nullptr; },
    [](void*, int64_t) { return ItemNull; },
    [](void*, int64_t) { return ItemNull; },
    release_output, trace_output,
};

ShellOutput* as_output(Item value) {
    if (get_type_id(value) != LMD_TYPE_VMAP || !value.vmap ||
            value.vmap->host_type != &output_brand) return NULL;
    return (ShellOutput*)value.vmap->host_data;
}

Item register_output(LambdaTask* task, ShellOutput* output) {
    RootFrame roots(1);
    Rooted<Item> held(roots, ItemNull);
    VMap* handle = (VMap*)heap_calloc(sizeof(VMap), LMD_TYPE_VMAP);
    if (!handle) return ItemError;
    handle->type_id = LMD_TYPE_VMAP;
    handle->vtable = &output_vtable;
    handle->host_type = &output_brand;
    handle->data = handle->host_data = output;
    ++output->refs;
    held.set((Item){.vmap = handle});

    // one task owns the native chain across procedure returns. The GC handle
    // holds a separate reference and can outlast the task without keeping I/O open.
    output->next_owned = (ShellOutput*)lambda_task_attachment_take(task, &task_outputs_key);
    if (!lambda_task_attachment_install(task, &task_outputs_key, output, release_outputs)) {
        release_outputs(output->next_owned);
        output->next_owned = NULL;
        return ItemError;
    }
    output->owner = task;
    ++output->refs;
    return held.get();
}

ShellOutput* new_output() {
    ShellOutput* output = (ShellOutput*)mem_calloc(1, sizeof(ShellOutput), MEM_CAT_EVAL);
    if (output) {
        output->output = -1;
        output->refs = 1;
        output->source = output->callback = output->pipe_root = output->pipe_error = output->feed_error = ItemNull;
    }
    return output;
}

void wake_output(ShellOutput* output) {
    if (output->waiter && lambda_task_state(output->waiter) == LAMBDA_TASK_PARKED)
        lambda_task_resume(output->waiter);
}

void stop_input_chain(ShellOutput* upstream, bool early) {
    for (ShellOutput* stage = upstream; stage; stage = stage->previous) {
        stage->killed_for_early_exit |= early && stage->running;
        stop_output(stage);
    }
}

void deadline_expired(uv_timer_t* timer) {
    ShellOutput* output = (ShellOutput*)timer->data;
    while (output->following) output = output->following;
    for (ShellOutput* stage = output; stage; stage = stage->previous)
        stage->timed_out = true;
    stop_input_chain(output, false);
    // The terminal can be parked inside a deferred procedure rather than on
    // stdout itself. Return through that wait before releasing its pipeline.
    if (output->record_waiter && lambda_task_state(output->record_waiter) == LAMBDA_TASK_PARKED)
        lambda_task_resume_external(output->record_waiter, ItemError);
    for (ShellOutput* stage = output; stage; stage = stage->previous)
        wake_output(stage);
}

int start_deadline(ShellOutput* stage, uint64_t limit_ms) {
    if (!limit_ms) return 0;
    int status = uv_timer_init(lambda_uv_loop(), &stage->deadline);
    if (status < 0) return status;
    stage->deadline_ready = true;
    stage->deadline.data = stage;
    ++stage->refs;
    uint64_t elapsed = uv_hrtime() / 1000000 - stage->start_ms;
    status = uv_timer_start(&stage->deadline, deadline_expired,
        elapsed < limit_ms ? limit_ms - elapsed : 0, 0);
    if (status < 0) close_deadline(stage);
    return status;
}

Item timeout_error() {
    return io_fs_error(ERR_TIMEOUT, "shell: pipeline timed out");
}

void process_exit(uv_process_t* process, int64_t code, int signal) {
    ShellOutput* output = (ShellOutput*)process->data;
    output->running = false;
    output->exit_code = code;
    output->signal = signal;
    if (output->feeder && !output->feed_eof) {
        output->feed_cancelled_early = code == 0 && signal == 0;
        lambda_task_cancel(output->feeder);
        stop_input_chain(output->previous, output->feed_cancelled_early);
    }
    wake_output(output);
    close_handle((uv_handle_t*)process);
}

int attach_pipe(uv_pipe_t* pipe, ShellOutput* output, uv_file* file) {
    int status = uv_pipe_init(lambda_uv_loop(), pipe, 0);
    if (status < 0) return status;
    pipe->data = output;
    ++output->refs;
    status = uv_pipe_open(pipe, *file);
    if (status < 0) close_handle((uv_handle_t*)pipe);
    else *file = -1;
    return status;
}

void input_written(uv_write_t* request, int status) {
    ShellOutput* output = (ShellOutput*)request->data;
    output->write_status = status;
    output->writing = false;
    if (!output->feed_records) {
        if (status != UV_EPIPE && status != UV_ECANCELED) output->write_error = status;
        strbuf_free(output->input);
        output->input = NULL;
        close_stdin(output);
    }
    if (output->writer_waiter && lambda_task_state(output->writer_waiter) == LAMBDA_TASK_PARKED)
        lambda_task_resume(output->writer_waiter);
    wake_output(output);
}

int write_stdin(ShellOutput* output) {
    output->writing = true;
    output->request.data = output;
    uv_buf_t bytes = uv_buf_init(output->input->str, (unsigned)output->input->length);
    int status = uv_write(&output->request, (uv_stream_t*)&output->writer, &bytes, 1, input_written);
    if (status < 0) input_written(&output->request, status);
    return status;
}

void encode_item(Item value, StrBuf* buffer) {
    if (get_type_id(value) == LMD_TYPE_STRING)
        strbuf_append_str_n(buffer, value.get_chars(), value.get_len());
    else
        print_item(buffer, value, 0);
    strbuf_append_char(buffer, '\n');
}

bool encode_input(Item value, StrBuf* buffer) {
    RootFrame roots(2);
    Rooted<Item> source(roots, value), item(roots, ItemNull);
    TypeId type = get_type_id(value);
    if (type == LMD_TYPE_BINARY) {
        Binary* bytes = value.get_safe_binary();
        if (bytes) strbuf_append_str_n(buffer, (const char*)binary_data(bytes), binary_length(bytes));
        return true;
    }
    if (!is_array_family_type_id(type)) return false;
    int64_t count = iter_len(source.get(), NULL, LOOP_KEY_ALL);
    for (int64_t i = 0; i < count; ++i) {
        item.set(iter_val_at(source.get(), NULL, i, LOOP_KEY_ALL));
        encode_item(item.get(), buffer);
    }
    return true;
}

Item wait_stage(ShellOutput* stage) {
    stage->waiter = lambda_task_running();
    Item result = lambda_task_suspend(stage->waiter);
    stage->waiter = NULL;
    return result;
}

Item finish_chain(ShellOutput* output, Item failure) {
    RootFrame roots(1);
    Rooted<Item> error(roots, failure);
    LambdaTask* task = lambda_task_running();
    if (output->timed_out) error.set(timeout_error());
    if (item_is_error(error.get())) stop_input_chain(output, false);
    bool downstream_success = false;
    for (ShellOutput* stage = output; stage; stage = stage->previous) {
        while (stage->running || stage->writing || stage->feeder) {
            bool cleanup = item_is_error(error.get());
            bool was_masked = cleanup && lambda_task_set_cleanup_masked(task, true);
            Item result = wait_stage(stage);
            if (cleanup) lambda_task_set_cleanup_masked(task, was_masked);
            if (item_is_error(result)) {
                if (!item_is_error(error.get())) error.set(result);
                stop_input_chain(output, false);
            }
        }
        if (item_is_error(stage->feed_error) && !item_is_error(error.get()))
            error.set(stage->feed_error);
        if (stage->record_stage) continue;
        bool expected = downstream_success && stage->killed_for_early_exit &&
            stage->signal == SIGKILL;
#ifdef SIGPIPE
        expected |= downstream_success && stage->signal == SIGPIPE;
#endif
        downstream_success = expected || (!stage->exit_code && !stage->signal && !stage->write_error);
        if (!downstream_success && !item_is_error(error.get())) {
            error.set(io_fs_error(ERR_INVALID_OPERATION, "shell: process failed (exit %lld, signal %d, stdin %d)",
                (long long)stage->exit_code, stage->signal, stage->write_error));
        }
    }
    for (ShellOutput* stage = output; stage; stage = stage->previous)
        close_deadline(stage);
    if (output->timed_out) return timeout_error();
    return error.get();
}

void read_buffer(uv_handle_t* handle, size_t, uv_buf_t* buffer) {
    ShellOutput* output = (ShellOutput*)handle->data;
    *buffer = uv_buf_init(output->buffer, 16384);
}

void read_ready(uv_stream_t* stream, ssize_t nread, const uv_buf_t*) {
    if (nread == 0) return;
    ShellOutput* output = (ShellOutput*)stream->data;
    output->read_result = nread;
    uv_read_stop(stream);
    wake_output(output);
}

void close_stdout(ShellOutput* output) {
    if (!output->reading) return;
    uv_read_stop((uv_stream_t*)&output->reader);
    output->reading = false;
    close_handle((uv_handle_t*)&output->reader);
}

Item fill_stdout(ShellOutput* output) {
    if (output->timed_out) return timeout_error();
    if (output->eof) return ItemNull;
    LambdaTask* task = lambda_task_running();
    if (!task || lambda_task_cancel_requested(task))
        return io_fs_error(ERR_CANCELLED, "shell: stdout read cancelled");
    if (!output->reading) {
        // The reader may still be closing if a task leaves through a fault.
        output->buffer = (char*)mem_alloc(16384, MEM_CAT_EVAL);
        if (!output->buffer)
            return io_fs_error(ERR_OUT_OF_MEMORY, "shell: cannot allocate stdout buffer");
        int status = attach_pipe(&output->reader, output, &output->output);
        if (status < 0)
            return io_fs_error(ERR_INVALID_OPERATION, "shell: cannot open stdout pipe: %s", uv_strerror(status));
        output->reading = true;
    }
    output->cursor = 0;
    output->read_result = 0;
    int status = uv_read_start((uv_stream_t*)&output->reader, read_buffer, read_ready);
    if (status < 0) output->read_result = status;
    while (!output->read_result) {
        Item result = wait_stage(output);
        if (output->timed_out) return timeout_error();
        if (item_is_error(result)) {
            close_stdout(output);
            return result;
        }
    }
    if (output->read_result > 0) return ItemNull;
    close_stdout(output);
    output->eof = output->read_result == UV_EOF;
    if (output->eof) return ItemNull;
    return io_fs_error(ERR_INVALID_OPERATION, "shell: reading stdout failed: %s",
        uv_strerror((int)output->read_result));
}

// Reads may split LF, CRLF or any UTF-8 code unit boundary. Validate only
// complete records, including the final partial record at EOF (S14.3.5).
Item next_line(ShellOutput* output, bool* eof) {
    if (!output->line) output->line = strbuf_new();
    if (!output->line) return ItemError;
    StrBuf* line = output->line;
    strbuf_reset(line);
    bool terminated = false;
    for (;;) {
        if (output->eof) {
            if (!line->length) { *eof = true; return ItemNull; }
            break;
        }
        if (output->read_result <= 0 || output->cursor == (size_t)output->read_result) {
            Item outcome = fill_stdout(output);
            if (item_is_error(outcome)) return outcome;
            if (output->eof) continue;
        }
        const char* start = output->buffer + output->cursor;
        size_t count = (size_t)output->read_result - output->cursor;
        const char* lf = (const char*)memchr(start, '\n', count);
        size_t segment = lf ? (size_t)(lf - start) : count;
        if (segment >= SIZE_MAX - line->length ||
                !strbuf_ensure_cap(line, line->length + segment + 1))
            return io_fs_error(ERR_OUT_OF_MEMORY, "shell: cannot buffer stdout line");
        strbuf_append_str_n(line, start, segment);
        output->cursor += segment + (lf ? 1 : 0);
        if (lf) { terminated = true; break; }
    }
    size_t length = line->length;
    if (terminated && length && line->str[length - 1] == '\r') --length;
    if (!utf8_valid(line->str, length))
        return io_fs_error(ERR_INVALID_OPERATION, "shell: invalid UTF-8 in stdout line");
    String* text = heap_strcpy(line->str, (int64_t)length);
    return text ? (Item){.item = s2it(text)} : ItemError;
}

Item next_record(ShellOutput* output, bool* eof, uint64_t* result_home) {
    if (!output->record_stage) return next_line(output, eof);
    RootFrame roots(2);
    Rooted<Item> record(roots, ItemNull), transformed(roots, ItemNull);
    uint64_t record_scalar = 0;
    for (;;) {
        record.set(next_record(output->previous, eof, &record_scalar));
        if (*eof || item_is_error(record.get())) return record.get();
        RootSpan actuals(5);
        actuals.items()[0] = record.get();
        actuals.items()[1] = (Item){.item = i2it(output->index++)};
        actuals.items()[2] = output->source;
        actuals.items()[3] = output->pipe_root;
        actuals.items()[4] = output->pipe_error;
        List args = {.length = 5, .items = actuals.items()};
        transformed.set(fn_call_into(output->callback.function, &args, result_home));
        if (item_is_error(transformed.get()) || !output->filter) return transformed.get();
        if (is_truthy(transformed.get()) == BOOL_TRUE)
            return lambda_item_adopt_scalar_home(record.get(), result_home);
    }
}

Item feed_records(Activation*, Item downstream) {
    RootFrame roots(3);
    Rooted<Item> held(roots, downstream), record(roots, ItemNull), error(roots, ItemNull);
    ShellOutput* output = as_output(held.get());
    if (!output || !output->writer_ready) return ItemError;
    output->input = strbuf_new();
    if (!output->input) return ItemError;
    LambdaTask* task = lambda_task_running();
    uint64_t scalar = 0;
    for (;;) {
        if (lambda_task_cancel_requested(task))
            return io_fs_error(ERR_CANCELLED, "shell: stdin feeder cancelled");
        bool eof = false;
        record.set(next_record(output->previous, &eof, &scalar));
        if (item_is_error(record.get())) return record.get();
        // A procedure in the record stage can yield while stdin is closed.
        if (!output->writer_ready)
            return io_fs_error(ERR_CANCELLED, "shell: stdin feeder cancelled");
        if (eof) {
            output->feed_eof = true;
            return ItemNull;
        }
        strbuf_reset(output->input);
        encode_item(record.get(), output->input);
        if (output->input->length > UINT_MAX)
            return io_fs_error(ERR_INVALID_OPERATION, "shell: encoded item exceeds pipe write limit");
        write_stdin(output);
        while (output->writing) {
            output->writer_waiter = task;
            error.set(lambda_task_suspend(task));
            output->writer_waiter = NULL;
            if (item_is_error(error.get())) return error.get();
        }
        if (output->write_status == UV_EPIPE) {
            output->feed_eof = true;
            stop_input_chain(output->previous, true);
            return ItemNull;
        }
        if (output->write_status < 0)
            return io_fs_error(ERR_FILE_WRITE_ERROR, "shell: writing child stdin: %s",
                uv_strerror(output->write_status));
    }
}

void feed_completed(LambdaTask*, Item result, void* data) {
    ShellOutput* output = (ShellOutput*)data;
    output->feeder = NULL;
    output->writer_waiter = NULL;
    close_stdin(output);
    if (item_is_error(result) && !output->feed_cancelled_early) {
        output->feed_error = result;
        stop_input_chain(output->previous, false);
        kill_output(output);
    }
    wake_output(output);
}

Item pump_output(ShellOutput* output, FILE* destination) {
    RootFrame roots(2);
    Rooted<Item> error(roots, ItemNull), item(roots, ItemNull);
    int64_t count = 0;
    if (output->record_stage) {
        // The task attachment owns the buffer if a stage faults during pull.
        output->input = strbuf_new();
        if (!output->input) return finish_chain(output, ItemError);
    }
    StrBuf* encoded = output->record_stage ? output->input : NULL;
    uint64_t record_scalar = 0;
    for (;;) {
        const char* bytes;
        size_t length;
        if (encoded) {
            bool eof = false;
            output->record_waiter = lambda_task_running_if_present();
            item.set(next_record(output, &eof, &record_scalar));
            output->record_waiter = NULL;
            if (item_is_error(item.get())) { error.set(item.get()); break; }
            if (output->timed_out) { error.set(timeout_error()); break; }
            if (eof) break;
            strbuf_reset(encoded);
            encode_item(item.get(), encoded);
            bytes = encoded->str;
            length = encoded->length;
        } else {
            error.set(fill_stdout(output));
            if (item_is_error(error.get()) || output->eof) break;
            bytes = output->buffer;
            length = (size_t)output->read_result;
        }
        bool written = output->staged_file
            ? file_anchor_write_chunk(output->staged_file, bytes, length)
            : fwrite(bytes, 1, length, destination) == length;
        if (!written) {
            error.set(io_fs_error(ERR_FILE_WRITE_ERROR, "shell: writing stdout failed"));
            break;
        }
        count += (int64_t)length;
    }
    if (encoded) {
        strbuf_free(encoded);
        output->input = NULL;
    }
    error.set(finish_chain(output, error.get()));
    return item_is_error(error.get()) ? error.get() : (Item){.item = i2it(count)};
}

Item claim_output(ShellOutput* output, LambdaTask* task) {
    if (!output || !task || output->owner != task || output->claimed)
        return io_fs_error(ERR_INVALID_OPERATION, "shell: output must be consumed once by its owning task");
    if (output->timed_out) return finish_chain(output, timeout_error());
    return ItemNull;
}

} // namespace

bool shell_output_is(Item item) {
    ShellOutput* output = as_output(item);
    return output && !output->inherited_stdin && !output->record_stage;
}

extern "C" int64_t fn_shell_is_stream(Item value) {
    ShellOutput* output = as_output(value);
    return output && !output->inherited_stdin;
}

extern "C" Item fn_shell_record_stage(Item source, Item callback, Item root, Item caught, int64_t filter) {
    ShellOutput* upstream = as_output(source);
    LambdaTask* task = lambda_task_running();
    Item error = claim_output(upstream, task);
    if (item_is_error(error)) return error;
    if (get_type_id(callback) != LMD_TYPE_FUNC)
        return io_fs_error(ERR_PROC_IN_FN, "shell: per-item stages require a procedure context");
    RootFrame roots(4);
    Rooted<Item> source_root(roots, source), callable(roots, callback);
    Rooted<Item> context_root(roots, root), error_root(roots, caught);
    ShellOutput* output = new_output();
    if (!output) return ItemError;
    Item result = register_output(task, output);
    if (!item_is_error(result)) {
        upstream->claimed = true;
        output->previous = upstream;
        upstream->following = output;
        output->start_ms = upstream->start_ms;
        output->source = source_root.get();
        output->callback = callable.get();
        // Wide scalar context values must outlive the defining procedure.
        output->pipe_root = lambda_item_adopt_scalar_home(context_root.get(), &output->pipe_root_scalar);
        output->pipe_error = lambda_item_adopt_scalar_home(error_root.get(), &output->pipe_error_scalar);
        output->record_stage = true;
        output->filter = filter != 0;
    }
    release_output(output);
    return result;
}

extern "C" Item pn_shell_stdin() {
    Item denied = ItemNull;
    if (!io_fs_allow_unbrokered("shell.stdin", &denied)) return denied;
    if (lambda_dry_run_enabled()) return ItemNull;
    LambdaTask* task = lambda_task_running();
    if (!task) return ItemError;
    ShellOutput* output = new_output();
    if (!output) return ItemError;
    output->inherited_stdin = true;
#ifdef _WIN32
    output->output = _dup(0);
#else
    output->output = fcntl(0, F_DUPFD_CLOEXEC, 3);
#endif
    Item result = output->output >= 0 ? register_output(task, output) :
        io_fs_error(ERR_INVALID_OPERATION, "shell.stdin: cannot duplicate stdin");
    if (item_is_error(result)) stop_output(output);
    release_output(output);
    return result;
}

extern "C" Item pn_shell_start(Item source, Item program, Item arguments, Item errors, Item timeout) {
    Item operands[] = {source, program, arguments, errors, timeout};
    for (Item operand : operands)
        if (item_is_error(operand)) return operand;
    Item denied = ItemNull;
    if (!io_fs_allow_unbrokered("shell.cmd", &denied)) return denied;
    if (lambda_dry_run_enabled()) return ItemNull;
    LambdaTask* task = lambda_task_running();
    if (!task || lambda_task_cancel_requested(task))
        return io_fs_error(ERR_CANCELLED, "shell: owning task cancelled");
    if (!lambda_uv_loop() && lambda_uv_init() != 0) return ItemError;
    int64_t limit_ms = 0;
    if (get_type_id(timeout) != LMD_TYPE_NULL &&
            (!lambda_read_milliseconds(timeout, &limit_ms) || limit_ms < 0))
        return io_fs_error(ERR_TYPE_MISMATCH, "shell: timeout_ms must be a nonnegative integer");

    RootFrame roots(5);
    Rooted<Item> input(roots, source), args(roots, arguments), executable(roots, program), stderr_path(roots, errors);
    Rooted<Item> handle(roots, ItemNull);
    if (!is_array_family_type_id(get_type_id(arguments)))
        return io_fs_error(ERR_TYPE_MISMATCH, "shell: executable arguments must be an array or list");
    int64_t length = iter_len(args.get(), NULL, LOOP_KEY_ALL);
    if (length > INT_MAX - 2) return ItemError;
    char** argv = (char**)mem_calloc((size_t)length + 2, sizeof(char*), MEM_CAT_EVAL);
    ShellOutput* output = new_output();
    if (!argv || !output) {
        if (output) release_output(output);
        mem_free(argv);
        return ItemError;
    }
    argv[0] = lambda_process_argument(executable.get());
    bool valid = argv[0] && argv[0][0];
    for (int64_t i = 0; valid && i < length; ++i)
        valid = (argv[i + 1] = lambda_process_argument(iter_val_at(args.get(), NULL, i, LOOP_KEY_ALL))) != NULL;
    ShellOutput* previous = as_output(input.get());
    uv_file read_end[2] = {-1, -1}, write_end[2] = {-1, -1};
    uv_file stdin_file = -1, stderr_file = -1;
    Item result = ItemNull;
    if (!valid) result = io_fs_error(ERR_TYPE_MISMATCH, "shell: invalid executable or argument");
    if (!item_is_error(result) && previous) {
        result = claim_output(previous, task);
        if (!item_is_error(result)) {
            output->previous = previous;
            previous->claimed = true;
            output->source = input.get();
            output->feed_records = previous->record_stage;
            if (output->feed_records) {
                if (uv_pipe(write_end, 0, UV_NONBLOCK_PIPE) < 0)
                    result = io_fs_error(ERR_INVALID_OPERATION, "shell: cannot create stdin pipe");
            } else {
                stdin_file = previous->output;
                previous->output = -1;
            }
        }
    }
    if (!item_is_error(result) && !previous) {
        TypeId type = get_type_id(input.get());
        if (type == LMD_TYPE_PATH) {
            StrBuf* path = lambda_get_local_path_from_item(input.get());
            if (path) { stdin_file = open_file(path->str, UV_FS_O_RDONLY); strbuf_free(path); }
            if (stdin_file < 0) result = io_fs_error(ERR_INVALID_OPERATION, "shell: cannot open stdin file");
        } else if (type != LMD_TYPE_NULL) {
            output->input = strbuf_new();
            if (!output->input || !encode_input(input.get(), output->input))
                result = io_fs_error(ERR_TYPE_MISMATCH, "shell: stdin requires a path, binary, list or array");
            else if (output->input->length > UINT_MAX)
                result = io_fs_error(ERR_INVALID_OPERATION, "shell: input is too large");
            else if (output->input->length && uv_pipe(write_end, 0, UV_NONBLOCK_PIPE) < 0)
                result = io_fs_error(ERR_INVALID_OPERATION, "shell: cannot create stdin pipe");
        }
    }
    if (!item_is_error(result) && get_type_id(stderr_path.get()) != LMD_TYPE_NULL) {
        StrBuf* path = get_type_id(stderr_path.get()) == LMD_TYPE_PATH
            ? lambda_get_local_path_from_item(stderr_path.get()) : NULL;
        if (path) {
            if (lambda_create_output_parent_dirs(path->str) == 0)
                stderr_file = open_file(path->str, UV_FS_O_WRONLY | UV_FS_O_CREAT | UV_FS_O_TRUNC);
            strbuf_free(path);
        }
        if (stderr_file < 0) result = io_fs_error(ERR_FILE_WRITE_ERROR, "shell: cannot open stderr destination");
    }
    if (!item_is_error(result)) {
        int status = uv_pipe(read_end, 0, 0);
        if (status < 0) result = io_fs_error(ERR_INVALID_OPERATION, "shell: cannot create stdout pipe: %s", uv_strerror(status));
    }
    if (!item_is_error(result)) {
        uv_stdio_container_t streams[3] = {};
        streams[0].data.fd = write_end[0] >= 0 ? write_end[0] : stdin_file;
        streams[0].flags = streams[0].data.fd >= 0 ? UV_INHERIT_FD : UV_IGNORE;
        streams[1].flags = UV_INHERIT_FD;
        streams[1].data.fd = read_end[1];
        streams[2].flags = UV_INHERIT_FD;
        streams[2].data.fd = stderr_file >= 0 ? stderr_file : 2;
        uv_process_options_t options = {};
        options.file = argv[0];
        options.args = argv;
        options.exit_cb = process_exit;
        options.flags = UV_PROCESS_DETACHED;
        options.stdio = streams;
        options.stdio_count = 3;
        output->process.data = output;
        int status = uv_spawn(lambda_uv_loop(), &output->process, &options);
        ++output->refs;
        if (status < 0) {
            close_handle((uv_handle_t*)&output->process);
            result = io_fs_error(ERR_INVALID_OPERATION, "shell: cannot launch %s: %s", argv[0], uv_strerror(status));
        } else {
            output->running = true;
            output->output = read_end[0];
            read_end[0] = -1;
            if (write_end[1] >= 0) {
                status = attach_pipe(&output->writer, output, &write_end[1]);
                if (status == 0) {
                    output->writer_ready = true;
                    status = protect_pipe_writes(output);
                    if (status == 0 && !output->feed_records) {
                        status = write_stdin(output);
                        // The process exit decides whether a closed stdin was expected.
                        if (status == UV_EPIPE) status = 0;
                    }
                }
                if (status < 0) result = io_fs_error(ERR_INVALID_OPERATION, "shell: cannot write child stdin: %s", uv_strerror(status));
            }
        }
    }
    close_file(read_end[0]);
    close_file(read_end[1]);
    close_file(write_end[0]);
    close_file(write_end[1]);
    close_file(stdin_file);
    close_file(stderr_file);
    for (int64_t i = 0; i <= length; ++i) mem_free(argv[i]);
    mem_free(argv);
    if (!item_is_error(result)) {
        handle.set(register_output(task, output));
        result = handle.get();
    }
    if (!item_is_error(result)) {
        output->start_ms = previous && previous->start_ms ? previous->start_ms : uv_hrtime() / 1000000;
        if (previous) previous->following = output;
        int status = start_deadline(output, (uint64_t)limit_ms);
        if (status < 0)
            result = io_fs_error(ERR_INVALID_OPERATION, "shell: cannot start pipeline deadline: %s", uv_strerror(status));
    }
    if (!item_is_error(result) && output->feed_records) {
        output->feeder = lambda_task_create(context->scheduler, feed_records, handle.get());
        if (output->feeder) {
            ++output->refs;
            if (!lambda_task_on_complete(output->feeder, feed_completed, output, release_output)) {
                lambda_task_cancel(output->feeder);
                output->feeder = NULL;
                release_output(output);
                result = ItemError;
            }
        } else result = ItemError;
    }
    if (item_is_error(result)) result = finish_chain(output, result);
    else result = handle.get();
    release_output(output);
    return result;
}

Item shell_output_file(Item value, Item destination, bool append, bool atomic) {
    ShellOutput* output = as_output(value);
    Item error = claim_output(output, lambda_task_running());
    if (item_is_error(error)) return error;
    output->claimed = true;
    if (!io_fs_allow_unbrokered("output", &error)) return finish_chain(output, error);
    StrBuf* path = lambda_get_local_path_from_item(destination);
    if (path && lambda_create_output_parent_dirs(path->str) == 0) {
        if (atomic && !append) {
            char* parent = file_path_dirname(path->str);
            if (parent) {
                output->anchor = file_anchor_open(parent, NULL);
                mem_free(parent);
            }
            if (output->anchor) {
                const char* leaf = file_path_basename(path->str);
                FileRevision revision;
                bool existing = file_anchor_stat(output->anchor, leaf, &revision);
                if (existing || errno == ENOENT)
                    output->staged_file = file_anchor_write_begin(output->anchor, leaf,
                        existing ? &revision : NULL);
            }
        } else {
            output->file = fopen(path->str, append ? "ab" : "wb");
        }
    }
    if (path) strbuf_free(path);
    if (!output->file && !output->staged_file)
        return finish_chain(output, io_fs_error(ERR_FILE_WRITE_ERROR, "output: cannot open shell output destination"));
    RootFrame roots(1);
    Rooted<Item> result(roots, pump_output(output, output->file));
    if (output->staged_file && !item_is_error(result.get())) {
        FileAtomicWriter* writer = output->staged_file;
        output->staged_file = NULL;
        int status = file_anchor_write_commit(writer);
        if (status != 0)
            result.set(io_fs_error(ERR_FILE_WRITE_ERROR, status > 0
                ? "output: shell output published but durability is uncertain"
                : "output: cannot publish shell output"));
    }
    if (output->file) {
        int status = fclose(output->file);
        output->file = NULL;
        if (status != 0 && !item_is_error(result.get()))
            result.set(io_fs_error(ERR_FILE_WRITE_ERROR, "output: cannot close shell output destination"));
    }
    abort_file(output);
    return result.get();
}

static Item shell_output_console(Item value) {
    ShellOutput* output = as_output(value);
    Item result = claim_output(output, lambda_task_running());
    if (item_is_error(result)) return result;
    output->claimed = true;
    int protected_status = protect_pipe_writes(output);
    if (protected_status < 0) return finish_chain(output,
        io_fs_error(ERR_INVALID_OPERATION, "shell: cannot protect stdout pipe: %s", uv_strerror(protected_status)));
#ifdef _WIN32
    fflush(stdout);
    int old_mode = _setmode(_fileno(stdout), _O_BINARY);
    if (old_mode < 0) return finish_chain(output,
        io_fs_error(ERR_FILE_WRITE_ERROR, "shell: cannot write binary stdout"));
#endif
    result = pump_output(output, stdout);
    int flushed = fflush(stdout);
#ifdef _WIN32
    _setmode(_fileno(stdout), old_mode);
#endif
    if (flushed != 0 && !item_is_error(result))
        return io_fs_error(ERR_FILE_WRITE_ERROR, "shell: cannot flush stdout");
    return item_is_error(result) ? result : ItemNull;
}
void shell_task_finish() {
    LambdaTask* task = lambda_task_running_if_present();
    if (!task) return;
    auto* outputs = (ShellOutput*)lambda_task_attachment_take(task, &task_outputs_key);
    if (!outputs) return;
    for (ShellOutput* stage = outputs; stage; stage = stage->next_owned) stop_output(stage);
    bool was_masked = lambda_task_set_cleanup_masked(task, true);
    for (ShellOutput* stage = outputs; stage; stage = stage->next_owned) {
        while (stage->running || stage->writing || stage->feeder) {
            Item waited = wait_stage(stage);
            if (item_is_error(waited)) break;
        }
    }
    lambda_task_set_cleanup_masked(task, was_masked);
    release_outputs(outputs);
}

Item shell_script_result(Runtime* runtime, Item value, bool run_main) {
    if (!run_main || !runtime || !runtime->cli_shell_output) return value;
    if (!lambda_task_running_if_present()) return value;
    // Terminals and cleanup may suspend and collect before the runner
    // publishes the procedure's result in its permanent result root.
    RootFrame roots(1);
    Rooted<Item> result(roots, value);
    if (fn_shell_is_stream(result.get())) {
        runtime->cli_shell_output_consumed = true;
        result.set(shell_output_console(result.get()));
    }
    shell_task_finish();
    return result.get();
}
#endif
