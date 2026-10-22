// io_secret.cpp — lambda.io.secret: get, set and delete generic passwords in
// the OS secret store (D7.2.4v4; vibe/Lambda_Demo_Mail.md ML6, MG6).
//
// The OS may block a call on an unlock or access prompt, so each one runs on
// the offload pool while the calling task parks. Values are never logged, and
// native copies are wiped before they are freed; the string `get` returns is
// an ordinary Lambda value.

#include "io_file_search.hpp"
#include "concurrency.h"
#include "../../lib/log.h"
#include "../../lib/memtrack.h"
#include "../../lib/secret_store.h"
#include "../../lib/strbuf.h"

#include <string.h>

typedef enum SecretOp { SECRET_GET, SECRET_SET, SECRET_DELETE } SecretOp;

struct SecretJob {
    SecretOp op;
    char* service;
    char* account;
    char* value;            // set only
    size_t value_length;
    StrBuf* found;          // get only
    SecretStoreStatus status;
    char message[256];
};

static void secret_job_free(SecretJob* job) {
    if (!job) return;
    if (job->value) secret_store_wipe(job->value, job->value_length);
    if (job->found) {
        secret_store_wipe(job->found->str, job->found->length);
        strbuf_free(job->found);
    }
    mem_free(job->service);
    mem_free(job->account);
    mem_free(job->value);
    mem_free(job);
}

static void secret_job_run(LambdaOffload* offload, void* data) {
    (void)offload;
    SecretJob* job = (SecretJob*)data;
    switch (job->op) {
    case SECRET_GET:
        job->status = secret_store_get(job->service, job->account, job->found,
                                       job->message, sizeof(job->message));
        break;
    case SECRET_SET:
        job->status = secret_store_set(job->service, job->account, job->value, job->value_length,
                                       job->message, sizeof(job->message));
        break;
    case SECRET_DELETE:
        job->status = secret_store_delete(job->service, job->account,
                                          job->message, sizeof(job->message));
        break;
    }
}

static void secret_job_orphan(void* data) {
    secret_job_free((SecretJob*)data);
}

/** A name for the store: non-empty text with no NUL. */
static bool secret_read_name(const char* fn, const char* what, Item value, char** out, Item* error) {
    if (!is_text_type_id(get_type_id(value))) {
        *error = io_fs_error(ERR_TYPE_MISMATCH, "%s: the %s must be text", fn, what);
        return false;
    }
    const char* s = value.get_chars();
    size_t n = value.get_len();
    if (n == 0 || memchr(s, 0, n)) {
        *error = io_fs_error(ERR_INVALID_OPERATION, "%s: the %s must be non-empty text without NUL", fn, what);
        return false;
    }
    *out = mem_dup_n(s, n, MEM_CAT_SYSTEM);
    return true;
}

static SecretJob* secret_job_new(const char* fn, SecretOp op, Item service, Item account, Item* error) {
    SecretJob* job = (SecretJob*)mem_calloc(1, sizeof(SecretJob), MEM_CAT_SYSTEM);
    if (!job) {
        *error = io_fs_error(ERR_OUT_OF_MEMORY, "%s: out of memory", fn);
        return NULL;
    }
    job->op = op;
    if (!secret_read_name(fn, "service", service, &job->service, error) ||
        !secret_read_name(fn, "account", account, &job->account, error)) {
        secret_job_free(job);
        return NULL;
    }
    return job;
}

/** Runs the job while the task parks; ItemNull on success, else the error to raise. */
static Item secret_job_execute(const char* fn, SecretJob* job) {
    static const LambdaOffloadOps ops = {secret_job_run, NULL, secret_job_orphan};
    Item outcome = lambda_task_offload(&ops, job, 0);
    if (get_type_id(outcome) == LMD_TYPE_ERROR) return outcome;
    switch (job->status) {
    case SECRET_STORE_OK:
    case SECRET_STORE_NOT_FOUND:
        return ItemNull;
    case SECRET_STORE_UNSUPPORTED:
        return io_fs_error(ERR_NOT_IMPLEMENTED, "%s: %s", fn, job->message);
    case SECRET_STORE_DENIED:
        return io_fs_error(ERR_FILE_ACCESS_DENIED, "%s: access denied: %s", fn, job->message);
    case SECRET_STORE_FAILED:
    default:
        return io_fs_error(ERR_IO_ERROR, "%s: %s", fn, job->message);
    }
}

// get(service, account): the stored text, or null when there is none
extern "C" Item pn_io_secret_get(Item service, Item account) {
    const char* fn = "io.secret.get";
    Item error = ItemNull;
    SecretJob* job = secret_job_new(fn, SECRET_GET, service, account, &error);
    if (!job) return error;
    job->found = strbuf_new();
    Item result = secret_job_execute(fn, job);
    if (get_type_id(result) != LMD_TYPE_ERROR) {
        result = job->status == SECRET_STORE_OK
            ? io_fs_make_string(job->found->str, job->found->length) : ItemNull;
    }
    log_debug("io.secret: get %s %s", job->service,
              get_type_id(result) == LMD_TYPE_ERROR ? "failed" : job->status == SECRET_STORE_OK ? "found" : "absent");
    secret_job_free(job);
    return result;
}

// set(service, account, value): stores value, replacing an earlier one
extern "C" Item pn_io_secret_set(Item service, Item account, Item value) {
    const char* fn = "io.secret.set";
    Item error = ItemNull;
    if (!is_text_type_id(get_type_id(value))) {
        return io_fs_error(ERR_TYPE_MISMATCH, "%s: the value must be text", fn);
    }
    SecretJob* job = secret_job_new(fn, SECRET_SET, service, account, &error);
    if (!job) return error;
    job->value_length = value.get_len();
    job->value = mem_dup_n(value.get_chars(), job->value_length, MEM_CAT_SYSTEM);
    Item result = secret_job_execute(fn, job);
    log_debug("io.secret: set %s %s", job->service, get_type_id(result) == LMD_TYPE_ERROR ? "failed" : "stored");
    secret_job_free(job);
    return result;
}

// delete(service, account): true when a value was removed, false when there was none
extern "C" Item pn_io_secret_delete(Item service, Item account) {
    const char* fn = "io.secret.delete";
    Item error = ItemNull;
    SecretJob* job = secret_job_new(fn, SECRET_DELETE, service, account, &error);
    if (!job) return error;
    Item result = secret_job_execute(fn, job);
    if (get_type_id(result) != LMD_TYPE_ERROR) result = (Item){.item = b2it(job->status == SECRET_STORE_OK)};
    secret_job_free(job);
    return result;
}
