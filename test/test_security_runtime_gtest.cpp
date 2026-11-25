#include <gtest/gtest.h>
#include "test_lambda_native_compiler.hpp"
#include "../lambda/runtime/transpiler.hpp"
#include "../lambda/runtime/security_context.h"
#include "../lambda/runtime/context_capsule.h"
#include "../lambda/runtime/doc_context.hpp"
#include "../lambda/runtime/sysinfo.h"
#include "../lambda/runtime/write_set.hpp"
#include "../lambda/runtime/interp.hpp"
#include "../lambda/runtime/lambda-error.h"
#include "../lambda/runtime/lambda-root-frame.hpp"
#include "lambda/input/input.hpp"
#include "lambda/io/fs_node.hpp"
#include "lambda/core/mark_reader.hpp"
#include "../lambda/host/safety/file_broker.h"
#include "lib/file_utils.h"
#include "lib/host_paths.h"
#include "lib/log.h"
#include "lib/strbuf.h"
#include "lib/str.h"
#include "lib/shell.h"
#include "lib/url.h"
#include "lib/atomic.h"
#include "lib/rdb.h"
#include "../lambda/runtime/module_registry.h"
#include "lambda/input/input-script-cache.h"
#include <string.h>
#include <time.h>
#ifndef _WIN32
#include <sys/stat.h>
#include <unistd.h>

struct SecurityRuntimeCapture {
    SecurityIOClient transport;
    SecurityIOResult last_write;
    unsigned writes;
    bool lose_write_reply;
    atomic_int32 source_reads;
    atomic_int32 data_reads;
    atomic_int32 denied_reads;
    atomic_int32 metadata_reads;
    atomic_int32 enumerations;
    atomic_int32 watched_reads;
    const char* watched_read_path;
    bool restrict_after_data_read;
    const char* read_started_path;
    const char* read_release_path;
};

struct ScopedContextEnvironment {
    char cwd[4096];
    char* home;
    char* scratch;
    bool armed;
    ~ScopedContextEnvironment() {
        if (armed) {
            chdir(cwd);
            if (home) setenv("HOME", home, 1); else unsetenv("HOME");
            if (scratch) setenv("TMPDIR", scratch, 1); else unsetenv("TMPDIR");
        }
        mem_free(home); mem_free(scratch);
    }
    bool capture() {
        if (!getcwd(cwd, sizeof(cwd))) return false;
        const char* old_home = getenv("HOME");
        const char* old_scratch = getenv("TMPDIR");
        home = old_home ? mem_strdup(old_home, MEM_CAT_TEMP) : nullptr;
        scratch = old_scratch ? mem_strdup(old_scratch, MEM_CAT_TEMP) : nullptr;
        armed = (!old_home || home) && (!old_scratch || scratch);
        return armed;
    }
};

class SecurityRuntimeTest : public ::testing::TestWithParam<LambdaTier> {
protected:
    Runtime runtime = {};
    LambdaTestNativeCompiler compiler;
    SecurityFileBroker* broker = nullptr;
    SecurityRuntimeCapture capture = {};
    bool runtime_live = false;
    char* temporary = nullptr;
    char* fixture_directory = nullptr;
    char* directory = nullptr;
    char* script_path = nullptr;
    uint64_t clock_value = 100;
    Input* last_run = nullptr;

    static uint64_t clock(void* context) { return *(uint64_t*)context; }
    static SecurityIOResult capture_write(void* context, const char* path, const void* data,
            size_t size, bool append, bool atomic) {
        SecurityRuntimeCapture* test = (SecurityRuntimeCapture*)context;
        test->writes++;
        test->last_write = test->transport.write(test->transport.context, path, data, size, append, atomic);
        if (test->lose_write_reply && test->last_write.committed) {
            test->lose_write_reply = false;
            test->last_write.system_error = EIO;
            test->last_write.committed = false;
            test->last_write.outcome_unknown = true;
        }
        return test->last_write;
    }
    static SecurityIOResult forward_read(void* context, const char* path, bool source,
            uint64_t limit, MemCategory category, char** data, size_t* size) {
        SecurityRuntimeCapture* test = (SecurityRuntimeCapture*)context;
        atomic_inc32(source ? &test->source_reads : &test->data_reads);
        if (test->watched_read_path && !strcmp(path, test->watched_read_path)) atomic_inc32(&test->watched_reads);
        SecurityIOResult result = test->transport.read(test->transport.context, path, source, limit, category, data, size);
        if (result.decision != SECURITY_ALLOWED || result.system_error) atomic_inc32(&test->denied_reads);
        if (!source && test->restrict_after_data_read && result.decision == SECURITY_ALLOWED && !result.system_error) {
            test->restrict_after_data_read = false;
            // Trusted fixture hook: revoke after acquisition but before delivery.
            EXPECT_TRUE(security_file_broker_restrict((SecurityFileBroker*)test->transport.context, SECURITY_LOAD_SOURCE));
        }
        if (!source && test->read_started_path && result.decision == SECURITY_ALLOWED && !result.system_error) {
            EXPECT_EQ(write_binary_file(test->read_started_path, "", 0), 0);
            for (unsigned i = 0; i < 10000 && !file_exists(test->read_release_path); i++) {
                timespec pause = {0, 1000000}; nanosleep(&pause, nullptr);
            }
            EXPECT_TRUE(file_exists(test->read_release_path));
        }
        return result;
    }
    static SecurityIOResult forward_source_path(void* context, const char* path, char** canonical) {
        SecurityRuntimeCapture* test = (SecurityRuntimeCapture*)context;
        SecurityIOResult result = test->transport.canonical_source(test->transport.context, path, canonical);
        if (result.decision != SECURITY_ALLOWED || result.system_error) atomic_inc32(&test->denied_reads);
        return result;
    }
    static SecurityIOResult forward_input_kind(void* context, const char* path, int32_t* kind) {
        SecurityRuntimeCapture* test = (SecurityRuntimeCapture*)context;
        SecurityIOResult result = test->transport.input_kind(test->transport.context, path, kind);
        if (result.decision != SECURITY_ALLOWED || result.system_error) atomic_inc32(&test->denied_reads);
        return result;
    }
    static SecurityIOResult forward_enumerate(void* context, const char* path,
            uint32_t max_entries, SecurityDirectory* listing) {
        SecurityRuntimeCapture* test = (SecurityRuntimeCapture*)context;
        atomic_inc32(&test->enumerations);
        return test->transport.enumerate(test->transport.context, path, max_entries, listing);
    }
    static SecurityIOResult forward_stat(void* context, const char* path, FileStat* metadata) {
        SecurityRuntimeCapture* test = (SecurityRuntimeCapture*)context;
        atomic_inc32(&test->metadata_reads);
        SecurityIOResult result = test->transport.stat(test->transport.context, path, metadata);
        if (result.decision != SECURITY_ALLOWED || result.system_error) atomic_inc32(&test->denied_reads);
        return result;
    }
    static SecurityIOResult forward_stamp(void* context, SecurityAuthorityStamp* stamp) {
        SecurityRuntimeCapture* test = (SecurityRuntimeCapture*)context;
        return test->transport.authority_stamp(test->transport.context, stamp);
    }
    static SecurityIOClient binding_for(SecurityRuntimeCapture* capture) {
        return {1, capture, forward_read, capture_write, forward_source_path,
            forward_input_kind, forward_enumerate, forward_stat, forward_stamp};
    }

    SecurityFileBroker* create_broker(uint64_t principal = 41, uint64_t session = 1041,
            SecurityOperations ceiling = SECURITY_ALL_OPERATIONS) {
        SecurityFileBrokerConfig config = {{principal, session, ceiling, 64, 64},
            1024 * 1024, 16 * 1024 * 1024, 4 * 1024 * 1024, 16, 128, 60, clock, &clock_value};
        return security_file_broker_create(&config);
    }

    void start_runtime() {
        capture.transport = security_file_broker_client(broker);
        runtime_init(&runtime); runtime_live = true;
        EvalContext* owner = runtime_get_eval_context(&runtime); ASSERT_NE(owner, nullptr);
        SecurityIOClient binding = binding_for(&capture);
        ASSERT_TRUE(eval_context_bind_security_io(owner, &binding));
        EXPECT_FALSE(eval_context_bind_security_io(owner, &binding));
    }

    bool replace_admission(const SecurityFileRootSpec* root, uint64_t principal, uint64_t session) {
        // Called before loading or after Runtime cleanup has joined prebuild work.
        security_file_broker_destroy(broker);
        broker = create_broker(principal, session);
        if (!broker) return false;
        uint64_t id = security_file_broker_admit_root(broker, root);
        capture.transport = security_file_broker_client(broker);
        return id != 0;
    }

    void SetUp() override {
        log_init(nullptr);
        compiler.select(GetParam());
        temporary = dir_temp_create("security_runtime"); ASSERT_NE(temporary, nullptr);
        fixture_directory = file_realpath(temporary); ASSERT_NE(fixture_directory, nullptr);
        directory = file_path_join(fixture_directory, "project"); ASSERT_NE(directory, nullptr);
        ASSERT_EQ(mkdir(directory, 0700), 0);
        script_path = file_path_join(directory, "entry.ls"); ASSERT_NE(script_path, nullptr);
        broker = create_broker(); ASSERT_NE(broker, nullptr);
        SecurityFileRootSpec root = {directory, SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
            0, 0, true, true, nullptr};
        ASSERT_NE(security_file_broker_admit_root(broker, &root), 0U);
        start_runtime();
    }

    void TearDown() override {
        if (runtime_live) runtime_cleanup(&runtime);
        security_file_broker_destroy(broker);
        if (temporary) dir_delete(temporary);
        mem_free(script_path); mem_free(directory); mem_free(fixture_directory); mem_free(temporary);
    }

    void seed(const char* relative, const char* statement, bool outside = false) {
        char* path = file_path_join(outside ? fixture_directory : directory, relative);
        ASSERT_NE(path, nullptr);
        char* parent = file_path_dirname(path); ASSERT_NE(parent, nullptr);
        ASSERT_TRUE(create_dir(parent)); mem_free(parent);
        StrBuf* source = strbuf_create(statement); ASSERT_NE(source, nullptr);
        ASSERT_TRUE(rewrite_source(source));
        EXPECT_EQ(write_binary_file(path, source->str, source->length), 0);
        strbuf_free(source); mem_free(path);
    }

    bool rewrite_source(StrBuf* source) {
        (void)strbuf_replace_all(source, "@ROOT@", directory);
        if (!strstr(source->str, "@PATH@")) return true;
        StrBuf* path = strbuf_create("(\\.file.localhost)");
        if (!path) return false;
        bool ok = true;
        for (const char* part = directory + 1; ok && *part;) {
            const char* slash = strchr(part, '/');
            size_t length = slash ? (size_t)(slash - part) : strlen(part);
            size_t escaped = str_escape_len(part, length, STR_ESC_LAMBDA);
            char* text = (char*)mem_alloc(escaped + 1, MEM_CAT_TEMP);
            if (!text) { ok = false; break; }
            str_escape(text, part, length, STR_ESC_LAMBDA); text[escaped] = 0;
            strbuf_append_format(path, "[\"%s\"]", text);
            mem_free(text);
            if (!slash) break;
            part = slash + 1;
        }
        if (ok) ok = strbuf_replace_all(source, "@PATH@", path->str);
        strbuf_free(path); return ok;
    }

    Input* load_file(const char* relative = "entry.ls") {
        char* path = file_path_join(directory, relative);
        Input* result = path ? run_script_mir(&runtime, nullptr, path, false) : nullptr;
        mem_free(path); return result;
    }

    uint64_t admit_dependency() {
        char* dependency = file_path_join(fixture_directory, "dependency");
        SecurityFileRootSpec root = {dependency, SECURITY_SCOPE_SHIPPED,
            SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, false, nullptr};
        uint64_t id = security_file_broker_admit_root(broker, &root);
        mem_free(dependency); return id;
    }

    uint64_t admit_shipped_packages() {
        lambda_home_init();
        char* path = host_path_join(HOST_PATH_HOME, "package");
        char* canonical = path ? file_realpath(path) : nullptr;
        mem_free(path);
        if (!canonical) return 0;
        SecurityFileRootSpec shipped = {canonical, SECURITY_SCOPE_SHIPPED,
            SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, false, nullptr};
        uint64_t id = security_file_broker_admit_root(broker, &shipped);
        mem_free(canonical);
        return id;
    }

    void expect_failed(Input* input) {
        ASSERT_NE(input, nullptr);
        EXPECT_EQ(get_type_id(input->root), LMD_TYPE_ERROR);
    }

    void run(const char* statement, char* path = nullptr) {
        // output paths follow process CWD; explicitly address the admitted fixture.
        StrBuf* source = strbuf_create(statement);
        ASSERT_NE(source, nullptr);
        ASSERT_TRUE(rewrite_source(source));
        Script* script = nullptr;
        Input* output = run_script_mir(&runtime, source->str, path ? path : script_path, true, &script);
        compiler.expect_mvp_admitted(script);
        last_run = output;
        strbuf_free(source);
        ASSERT_NE(output, nullptr);
    }

    void expect_denied_calls(const char* const* calls, size_t count, const char* label, const char* imports = "") {
        for (unsigned dry = 0; dry < 2; dry++) {
            runtime.dry_run = dry != 0;
            for (unsigned i = 0; i < count; i++) {
                SCOPED_TRACE(calls[i]);
                StrBuf* source = strbuf_new(); StrBuf* name = strbuf_new();
                strbuf_append_format(source, "%spn main() { %s^ }\n", imports, calls[i]);
                strbuf_append_format(name, "%s/%s-%u-%u.ls", directory, label, dry, i);
                run(source->str, name->str);
                strbuf_free(source); strbuf_free(name);
                expect_failed(last_run);
                LambdaError* error = it2err(last_run->root); ASSERT_NE(error, nullptr);
                EXPECT_EQ(error->code, ERR_FILE_ACCESS_DENIED);
            }
        }
        runtime.dry_run = false;
    }

    void expect_text(const char* relative, const char* expected) {
        char* path = file_path_join(directory, relative);
        char* bytes = nullptr; size_t size = 0;
        ASSERT_TRUE(file_read_all(path, MEM_CAT_TEMP, &bytes, &size));
        EXPECT_EQ(size, strlen(expected));
        EXPECT_EQ(memcmp(bytes, expected, size), 0);
        mem_free(bytes); mem_free(path);
    }
};

TEST_P(SecurityRuntimeTest, RealOutputCreatesNewFilesAndChallengesExistingFiles) {
    run("pn main() { output(\"first\", \"@ROOT@/new/report.txt\")^ }\n");
    EXPECT_EQ(capture.last_write.decision, SECURITY_ALLOWED);
    EXPECT_TRUE(capture.last_write.committed);
    expect_text("new/report.txt", "first");
    // A different script source prevents the loaded-script registry reusing main.
    char* second = file_path_join(directory, "second.ls");
    run("pn main() { output(\"second\", \"@ROOT@/new/report.txt\", {atomic: true})^ }\n", second);
    EXPECT_EQ(capture.last_write.decision, SECURITY_APPROVAL_REQUIRED);
    EXPECT_FALSE(capture.last_write.committed);
    expect_text("new/report.txt", "first");
    SecurityIOResult approved = security_file_broker_approve(broker, capture.last_write.approval);
    EXPECT_EQ(approved.system_error, 0);
    EXPECT_TRUE(approved.committed);
    expect_text("new/report.txt", "second");
    mem_free(second);
}

TEST_P(SecurityRuntimeTest, PathNodeKindLeavesFilePayloadUnread) {
    ASSERT_TRUE(eval_context_bind_security_directory(runtime_get_eval_context(&runtime), directory));
    seed("data.json", "{\"n\":41}");
    run("pn main() { let p = \\.'data.json'; output(p# is file, \"kind.json\", 'json')^ }\n");
    expect_text("kind.json", "true");
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);

    char* script = file_path_join(directory, "content.ls");
    run("pn main() { let p = \\.'data.json'; let d = p#; output(d.n, \"number.json\", 'json')^ }\n", script);
    mem_free(script);
    expect_text("number.json", "41");
    EXPECT_EQ(atomic_load32(&capture.data_reads), 1);
}

TEST_P(SecurityRuntimeTest, HostDirectoryResolvesRelativeDataAndTaskPathsWithoutChangingProcessCwd) {
    EvalContext* owner = runtime_get_eval_context(&runtime);
    EXPECT_FALSE(eval_context_bind_security_directory(owner, "relative"));
    ASSERT_TRUE(eval_context_bind_security_directory(owner, directory));
    EXPECT_FALSE(eval_context_bind_security_directory(owner, directory));
    char before[4096], after[4096]; ASSERT_TRUE(file_getcwd_into(before, sizeof(before)));
    seed("data.json", "{\"n\":41}"); seed("data.txt", "payload");
    run("pn reader() { io.read(\"data.txt\")^ }\n"
        "pn main() { let data = input(\"data.json\")^; let h = start(reader); let text = wait(h)^; "
        "output(text, \"relative.txt\")^; output(data.n, \"relative.json\", 'json')^ }\n");
    ASSERT_NE(last_run, nullptr); EXPECT_NE(get_type_id(last_run->root), LMD_TYPE_ERROR);
    EXPECT_EQ(capture.last_write.decision, SECURITY_ALLOWED); EXPECT_TRUE(capture.last_write.committed);
    expect_text("relative.txt", "payload"); expect_text("relative.json", "41");
    ASSERT_TRUE(file_getcwd_into(after, sizeof(after))); EXPECT_STREQ(before, after);
}

TEST_P(SecurityRuntimeTest, AnUnknownWriteOutcomeIsReportedWithoutAnAutomaticRetry) {
    struct Case { const char* script; const char* target; } cases[] = {
        {"pn main() { output(\"payload\", \"@ROOT@/unknown.txt\")^ }\n", "unknown.txt"},
        {"pn main() { output(b'\\x0001FF', \"@ROOT@/unknown.bin\")^ }\n", "unknown.bin"},
        {"pn main() { output({value: 42}, \"@ROOT@/unknown.json\", 'json')^ }\n", "unknown.json"},
        {"pn main() { output(<fs kind: 'dir', <fs name: \"data\", kind: 'file', \"payload\">>, \"@ROOT@/unknown.zip\", 'zip')^ }\n", "unknown.zip"}
    };
    for (unsigned i = 0; i < sizeof(cases) / sizeof(cases[0]); i++) {
        capture.lose_write_reply = true;
        char* source = file_path_join(directory, cases[i].target);
        run(cases[i].script, source); mem_free(source);
        char* target = file_path_join(directory, cases[i].target);
        FileStat stat = file_stat(target); mem_free(target);
        EXPECT_TRUE(stat.exists); EXPECT_GT(stat.size, 0);
        EXPECT_EQ(capture.writes, i + 1);
        ASSERT_NE(last_run, nullptr); expect_failed(last_run);
        LambdaError* error = it2err(last_run->root); ASSERT_NE(error, nullptr);
        EXPECT_EQ(error->code, ERR_FILE_WRITE_ERROR);
        EXPECT_TRUE(capture.last_write.outcome_unknown);
    }
}

TEST_P(SecurityRuntimeTest, DeniedOutputDoesNotCreateParentsBeforeAuthorization) {
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_READ));
    run("pn main() { output(\"forbidden\", \"@ROOT@/denied/child/file\")^ }\n");
    EXPECT_EQ(capture.last_write.decision, SECURITY_DENIED);
    char* path = file_path_join(directory, "denied");
    EXPECT_FALSE(file_exists(path)); mem_free(path);
}

TEST_P(SecurityRuntimeTest, BinaryAndStructuredOutputUseTheSameBroker) {
    run("pn main() { output(b'\\x0001FF', \"@ROOT@/binary.dat\")^; output({value: 42}, \"@ROOT@/object.json\", 'json')^ }\n");
    EXPECT_EQ(capture.last_write.decision, SECURITY_ALLOWED);
    EXPECT_TRUE(capture.last_write.committed);
    char* path = file_path_join(directory, "binary.dat");
    char* bytes = nullptr; size_t size = 0;
    ASSERT_TRUE(file_read_all(path, MEM_CAT_TEMP, &bytes, &size));
    ASSERT_EQ(size, 3U);
    EXPECT_EQ((unsigned char)bytes[0], 0); EXPECT_EQ((unsigned char)bytes[1], 1); EXPECT_EQ((unsigned char)bytes[2], 255);
    mem_free(bytes); mem_free(path);
    path = file_path_join(directory, "object.json");
    ASSERT_TRUE(file_read_all(path, MEM_CAT_TEMP, &bytes, &size));
    EXPECT_NE(strstr(bytes, "42"), nullptr);
    mem_free(bytes); mem_free(path);
}

TEST_P(SecurityRuntimeTest, ZipOutputCannotBypassTheBroker) {
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_READ));
    run("pn main() { output(<fs kind: 'dir', <fs name: \"data.txt\", kind: 'file', \"payload\">>, \"@ROOT@/denied/archive.zip\", 'zip')^ }\n");
    EXPECT_EQ(capture.last_write.decision, SECURITY_DENIED);
    char* path = file_path_join(directory, "denied"); EXPECT_FALSE(file_exists(path)); mem_free(path);
}

TEST_P(SecurityRuntimeTest, ReusedCodeDoesNotRetainAnotherContextsAuthority) {
    const char* statement = "pn main() { output(\"first\", \"@ROOT@/report\")^ }\n";
    run(statement);
    ASSERT_TRUE(capture.last_write.committed);
    expect_text("report", "first");
    runtime_cleanup(&runtime); runtime_live = false;
    SecurityFileBroker* other = create_broker(42, 1042, SECURITY_READ);
    ASSERT_NE(other, nullptr);
    SecurityFileRootSpec root = {directory, SECURITY_SCOPE_PROJECT, SECURITY_READ,
        0, 0, true, true, nullptr};
    EXPECT_NE(security_file_broker_admit_root(other, &root), 0U);
    Runtime second = {}; runtime_init(&second);
    SecurityIOClient restricted = security_file_broker_client(other);
    SecurityRuntimeCapture other_capture = {restricted, {}, 0};
    SecurityIOClient other_binding = binding_for(&other_capture);
    EXPECT_TRUE(eval_context_bind_security_io(runtime_get_eval_context(&second), &other_binding));
    StrBuf* source = strbuf_create(statement);
    EXPECT_TRUE(strbuf_replace_all(source, "@ROOT@", directory));
    capture.last_write.decision = SECURITY_INVALID_REQUEST;
    Input* output = run_script_mir(&second, source->str, script_path, true);
    EXPECT_NE(output, nullptr);
    // The first context's capture transport must never be called by reused code.
    EXPECT_EQ(capture.last_write.decision, SECURITY_INVALID_REQUEST);
    EXPECT_EQ(other_capture.writes, 1U);
    EXPECT_EQ(other_capture.last_write.decision, SECURITY_DENIED);
    expect_text("report", "first");
    strbuf_free(source); runtime_cleanup(&second); security_file_broker_destroy(other);
}

TEST_P(SecurityRuntimeTest, FileBackedDiamondAndParentImportsUseTheClient) {
    seed("shared.ls", "pub let answer = 42\n");
    seed("sub/left.ls", "import shared: ~~.shared\npub let answer = shared.answer + 1\n");
    seed("right.ls", "import shared: .shared\npub let answer = shared.answer + 2\n");
    seed("entry.ls", "import left: .sub.left\nimport right: .right\nleft.answer + right.answer\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT);
    EXPECT_EQ(it2i(result->root), 87);
    EXPECT_GE(atomic_load32(&capture.source_reads), 4);
    EXPECT_EQ(atomic_load32(&capture.denied_reads), 0);
}

TEST_P(SecurityRuntimeTest, OutsideRootAndLinkedSourcesAreDenied) {
    seed("outside.ls", "pub let answer = 42\n", true);
    seed("entry.ls", "import outside: ~~.outside\noutside.answer\n");
    expect_failed(load_file());
    seed("external/secret.ls", "pub let answer = 43\n", true);
    char* target = file_path_join(fixture_directory, "external");
    char* link = file_path_join(directory, "link");
    ASSERT_EQ(symlink(target, link), 0);
    seed("linked.ls", "import secret: .link.secret\nsecret.answer\n");
    expect_failed(load_file("linked.ls"));
    EXPECT_GE(atomic_load32(&capture.denied_reads), 2);
    mem_free(target); mem_free(link);
}

TEST_P(SecurityRuntimeTest, LooseEntryCannotLoadAnUnselectedSibling) {
    seed("entry.ls", "import sibling: .sibling\nsibling.answer\n");
    seed("sibling.ls", "pub let answer = 42\n");
    SecurityFileRootSpec root = {directory, SECURITY_SCOPE_ENTRY,
        SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, true, "entry.ls"};
    ASSERT_TRUE(replace_admission(&root, 41, 2041));
    expect_failed(load_file());
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, SourceDenialDoesNotTryTheAmbientJavaScriptFallback) {
    seed("entry.ls", "import fallback: .fallback\nfallback.answer\n");
    seed("fallback.js", "export const answer = 42;\n");
    expect_failed(load_file());
    char* path = file_path_join(directory, "fallback.js");
    EXPECT_EQ(module_get_for_runtime(&runtime, path), nullptr);
    mem_free(path);
}

TEST_P(SecurityRuntimeTest, RevokedDependencyBlocksSameRuntimeCacheHits) {
    seed("dependency/base.ls", "pub let answer = 7\n", true);
    seed("entry.ls", "import base: ~~.dependency.base\nbase.answer\n");
    uint64_t dependency = admit_dependency(); ASSERT_NE(dependency, 0U);
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 7);
    ASSERT_TRUE(security_file_broker_revoke_root(broker, dependency));
    expect_failed(load_file());
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, ChangedDependencyRefreshesExactBytesThroughTheClient) {
    seed("dependency/base.ls", "pub let answer = 7\n", true);
    seed("entry.ls", "import base: ~~.dependency.base\nbase.answer\n");
    ASSERT_NE(admit_dependency(), 0U);
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 7);
    seed("dependency/base.ls", "pub let answer = 9\n", true);
    result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 9);
}

TEST_P(SecurityRuntimeTest, AnotherPrincipalCannotReuseAnUnadmittedCachedDependency) {
    seed("dependency/base.ls", "pub let answer = 7\n", true);
    seed("entry.ls", "import base: ~~.dependency.base\nbase.answer\n");
    ASSERT_NE(admit_dependency(), 0U);
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 7);
    runtime_cleanup(&runtime); runtime_live = false;
    SecurityFileRootSpec root = {directory, SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
        0, 0, true, true, nullptr};
    ASSERT_TRUE(replace_admission(&root, 42, 1042));
    start_runtime();
    InputScriptCacheStats before = {}, after = {};
    input_script_cache_get_stats(input_manager_global_script_cache(), &before);
    expect_failed(load_file());
    input_script_cache_get_stats(input_manager_global_script_cache(), &after);
    EXPECT_GT(after.ast_hits + after.mir_hits, before.ast_hits + before.mir_hits);
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, SourceCaseAliasesKeepOneModuleInstance) {
    seed("base.ls", "pub let answer = 42\n");
    char* alias = file_path_join(directory, "BASE.ls");
    bool case_alias = file_exists(alias); mem_free(alias);
    if (!case_alias) GTEST_SKIP() << "case-sensitive filesystem";
    seed("entry.ls", "import first: .base\nimport second: .BASE\nfirst.answer + second.answer\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 84);
    Script* script = load_script(&runtime, script_path, nullptr, false);
    ASSERT_NE(script, nullptr); ASSERT_NE(script->direct_imports, nullptr);
    ASSERT_EQ(script->direct_imports->length, 2);
    EXPECT_EQ(script->direct_imports->data[0], script->direct_imports->data[1]);
}

TEST_P(SecurityRuntimeTest, LooseFileSelectionKeepsItsCanonicalLeafIdentity) {
    seed("entry.ls", "42\n");
    char* alias = file_path_join(directory, "ENTRY.ls");
    bool case_alias = file_exists(alias); mem_free(alias);
    if (!case_alias) GTEST_SKIP() << "case-sensitive filesystem";
    SecurityFileRootSpec root = {directory, SECURITY_SCOPE_ENTRY,
        SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, true, "ENTRY.ls"};
    ASSERT_TRUE(replace_admission(&root, 41, 3041));
    Input* result = load_file("ENTRY.ls"); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 42);
}

TEST_P(SecurityRuntimeTest, RootAdmissionAcceptsItsCanonicalSpelling) {
    seed("entry.ls", "42\n");
    char* alias = file_path_join(fixture_directory, "PROJECT");
    if (!file_is_dir(alias)) { mem_free(alias); GTEST_SKIP() << "case-sensitive filesystem"; }
    SecurityFileRootSpec root = {alias, SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
        0, 0, true, true, nullptr};
    ASSERT_TRUE(replace_admission(&root, 41, 4041));
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 42);
    mem_free(alias);
}

TEST_P(SecurityRuntimeTest, InputInModuleInitializersUsesDataAuthority) {
    seed("data.json", "{\"answer\":42}");
    seed("reader.ls", "pub let data = input(\"@ROOT@/data.json\", 'json')^\n");
    seed("entry.ls", "import reader: .reader\nreader.data.answer\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 42);
    EXPECT_GE(atomic_load32(&capture.data_reads), 1);
    EXPECT_EQ(atomic_load32(&capture.denied_reads), 0);
}

TEST_P(SecurityRuntimeTest, InputRejectsOutsideLinkedNetworkAndSystemTargets) {
    seed("secret.json", "{\"answer\":42}", true);
    seed("entry.ls", "input(\"@ROOT@/../secret.json\", 'json')^\n");
    expect_failed(load_file());
    char* secret = file_path_join(fixture_directory, "secret.json");
    char* linked = file_path_join(directory, "linked.json");
    ASSERT_EQ(symlink(secret, linked), 0); mem_free(secret); mem_free(linked);
    seed("link.ls", "input(\"@ROOT@/linked.json\", 'json')^\n");
    expect_failed(load_file("link.ls"));
    seed("network.ls", "input(\"http://127.0.0.1:9/private\", 'json')^\n");
    expect_failed(load_file("network.ls"));
    seed("system.ls", "input(\"sys://system\")^\n");
    expect_failed(load_file("system.ls"));
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    EXPECT_GE(atomic_load32(&capture.denied_reads), 2);
}

TEST_P(SecurityRuntimeTest, InputDirectoryUsesBoundEnumeration) {
    seed("data.json", "{\"answer\":42}");
    seed("entry.ls", "len(input(\"@ROOT@\")^)\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 2);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
}

TEST_P(SecurityRuntimeTest, SourceAuthorityDoesNotPermitInitializerDataReads) {
    seed("data.json", "{\"answer\":42}");
    seed("entry.ls", "input(\"@ROOT@/data.json\", 'json')^\n");
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE));
    expect_failed(load_file());
    EXPECT_GE(atomic_load32(&capture.source_reads), 1);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, InlineParsingCarriesResourceAuthority) {
    // The Script engine needs its shipped source and kernel independently of document grants.
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("part.tex", "SAFE");
    seed("outside/part.tex", "FORBIDDEN", true);
    seed("entry.ls", "contains(parse(\"\\\\input{part}\\\\end\", "
        "{type: 'tex', base: \"@ROOT@\"})^.text, \"SAFE\")^\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL) <<
        (runtime_get_eval_context(&runtime)->last_error ? runtime_get_eval_context(&runtime)->last_error->message : "no runtime diagnostic");
    EXPECT_TRUE(result->root.bool_val);
    seed("relative.ls", "contains(parse(\"\\\\input{part}\\\\end\", "
        "{type: 'tex', base: \".\"})^.text, \"SAFE\")^\n");
    result = load_file("relative.ls"); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL); EXPECT_TRUE(result->root.bool_val);
    seed("denied.ls", "contains(parse(\"\\\\input{part}\\\\end\", "
        "{type: 'tex', base: \"@ROOT@/../outside\"})^.text, \"FORBIDDEN\")^\n");
    result = load_file("denied.ls"); ASSERT_NE(result, nullptr);
    // Denial propagates as an error instead of masquerading as an absent file (D11.1.2).
    expect_failed(result);
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

static StrBuf* security_single_member_zip(const char* name, const char* source) {
    ZipOutputEntry entry = {name, (const uint8_t*)source, strlen(source), 0100644, 0, false};
    ArrayList* entries = arraylist_new(1);
    StrBuf* bytes = strbuf_new();
    if (!entries || !bytes) { if (entries) arraylist_free(entries); strbuf_free(bytes); return nullptr; }
    arraylist_append(entries, &entry);
    ZipError error = {};
    bool ok = zip_write_entries(entries, nullptr, bytes, &error);
    arraylist_free(entries);
    if (!ok) { strbuf_free(bytes); return nullptr; }
    return bytes;
}

TEST_P(SecurityRuntimeTest, CapturedMemberParsingCannotRetainExternalResourceAuthority) {
    const char* source = "\\RequirePackage{safety-private}\\privatevalue\\input{secret}\\end";
    seed("safety-private.sty", "\\def\\privatevalue{EXTERNAL}");
    seed("secret.tex", "FORBIDDEN");
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("control.ls", "let opts={type:'tex', base:\"@ROOT@\", resources:{bundled:"
        "{'safety-private.sty':{data:\"\\\\def\\\\privatevalue{EXTERNAL}\"}}}}\n"
        "let text=parse(\"\\\\RequirePackage{safety-private}\\\\privatevalue\\\\input{secret}\\\\end\", opts)^.text\n"
        "contains(text, \"EXTERNAL\")^ and contains(text, \"FORBIDDEN\")^\n");
    Input* control = load_file("control.ls"); ASSERT_NE(control, nullptr);
    ASSERT_EQ(get_type_id(control->root), LMD_TYPE_BOOL); ASSERT_TRUE(control->root.bool_val);
    Input* owner = InputManager::create_input(nullptr); ASSERT_NE(owner, nullptr);
    MarkBuilder builder(owner);
    String* type = builder.createString("tex");

    StrBuf* bytes = security_single_member_zip("doc.tex", source); ASSERT_NE(bytes, nullptr);
    char* archive_path = file_path_join(directory, "member.zip"); ASSERT_NE(archive_path, nullptr);
    ASSERT_EQ(write_binary_file(archive_path, bytes->str, bytes->length), 0);
    mem_free(archive_path);
    Input* archive = input_from_source_n(bytes->str, bytes->length, nullptr, nullptr, nullptr);
    strbuf_free(bytes);
    ASSERT_NE(archive, nullptr);
    Item children = fs_node_content(archive->root);
    ASSERT_EQ(get_type_id(children), LMD_TYPE_VARRAY);
    ASSERT_EQ(varray_count(children.varray), 1);
    Item node = varray_get(children.varray, 0);
    // Standalone Input cannot execute the Script parser or retain external grants.
    for (int repeat = 0; repeat < 2; repeat++) {
        Item report = fs_node_input(node, type, nullptr);
        EXPECT_EQ(get_type_id(report), LMD_TYPE_ERROR);
    }
    seed("member.ls", "let archive=input(\"@ROOT@/member.zip\")^\n"
        "let member=content(archive)^[0]\n"
        "let first=input(member, 'tex')^\n"
        "let second=parse(member, {type:'tex', base:\"@ROOT@\", resources:{bundled:"
        "{'safety-private.sty':{data:\"\\\\def\\\\privatevalue{EXTERNAL}\"}}}})^\n"
        "first == second and not contains(first.text, \"EXTERNAL\")^ and "
        "not contains(first.text, \"FORBIDDEN\")^\n");
    Input* result = load_file("member.ls"); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL); EXPECT_TRUE(result->root.bool_val);
}

TEST_P(SecurityRuntimeTest, LazyTexMemberCannotAcquireExternalResources) {
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("secret.tex", "FORBIDDEN");
    char* secret_path = file_path_join(directory, "secret.tex"); ASSERT_NE(secret_path, nullptr);
    struct WatchedReadScope {
        SecurityRuntimeCapture* capture;
        char* path;
        ~WatchedReadScope() { capture->watched_read_path = nullptr; mem_free(path); }
    } watched = {&capture, secret_path};
    capture.watched_read_path = secret_path;
    StrBuf* bytes = security_single_member_zip("doc.tex",
        "\\begin{document}Before \\input{secret} After\\end{document}");
    ASSERT_NE(bytes, nullptr);
    char* path = file_path_join(directory, "lazy.zip"); ASSERT_NE(path, nullptr);
    ASSERT_EQ(write_binary_file(path, bytes->str, bytes->length), 0);
    mem_free(path); strbuf_free(bytes);
    seed("entry.ls", "let archive=input(\"@ROOT@/lazy.zip\")^\n"
        "let member=content(archive)^[0]\n"
        "let lazy=content(member)^\n"
        "let explicit=input(member, 'latex')^\n"
        "let serialized=format(lazy[0], 'json')^\n"
        "len(lazy)==1 and lazy[0]==explicit and "
        "not contains(serialized, \"FORBIDDEN\")\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL); EXPECT_TRUE(result->root.bool_val);
    EXPECT_EQ(atomic_load32(&capture.watched_reads), 0);
}

TEST_P(SecurityRuntimeTest, TexLocalIncludesUseTheSameDataGrant) {
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("docs/doc.tex", "\\input{part}\\end"); seed("docs/part.tex", "SAFE");
    seed("entry.ls", "contains(input(\"@ROOT@/docs/doc.tex\", 'tex')^.text, \"SAFE\")^\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL); EXPECT_TRUE(result->root.bool_val);
    seed("docs/auto-doc.tex", "\\begin{document}\\input{part}\\end{document}");
    seed("auto.ls", "input(\"@ROOT@/docs/auto-doc.tex\")^ == input(\"@ROOT@/docs/auto-doc.tex\", 'latex')^\n");
    result = load_file("auto.ls"); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL); EXPECT_TRUE(result->root.bool_val);
    EXPECT_GE(atomic_load32(&capture.data_reads), 2);
    seed("outside.tex", "FORBIDDEN", true);
    char* outside = file_path_join(fixture_directory, "outside.tex");
    char* linked = file_path_join(directory, "docs/linked.tex");
    ASSERT_EQ(symlink(outside, linked), 0); mem_free(outside); mem_free(linked);
    seed("docs/linked-doc.tex", "\\input{linked}\\end");
    seed("denied.ls", "contains(input(\"@ROOT@/docs/linked-doc.tex\", 'tex')^.text, \"FORBIDDEN\")^\n");
    result = load_file("denied.ls"); ASSERT_NE(result, nullptr);
    expect_failed(result);
    seed("auto-denied.ls", "input(\"@ROOT@/docs/linked-doc.tex\")^\n");
    result = load_file("auto-denied.ls"); ASSERT_NE(result, nullptr);
    expect_failed(result);
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, TexLocalPackagesUseTheSameDataGrant) {
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("local-only.sty", "\\def\\localvalue{EXTERNAL}");
    seed("entry.ls", "contains(parse(\"\\\\RequirePackage{local-only}\\\\localvalue\\\\end\", "
        "{type:'tex', base:\"@ROOT@\"})^.text, \"EXTERNAL\")^\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL) <<
        (runtime_get_eval_context(&runtime)->last_error ? runtime_get_eval_context(&runtime)->last_error->message : "no runtime diagnostic");
    EXPECT_TRUE(result->root.bool_val);
}

TEST_P(SecurityRuntimeTest, StructurizrIncludesRetainTheirNarrowerDocumentRoot) {
    // Script parser dependencies use the same read-only shipped grant as other packages (D11.2.1).
    lambda_home_init();
    struct HomeScope {
        char* saved = mem_strdup(host_path(HOST_PATH_HOME), MEM_CAT_SYSTEM);
        ~HomeScope() { if (saved) host_paths_set(HOST_PATH_HOME, saved); mem_free(saved); }
    } home_scope;
    ASSERT_NE(home_scope.saved, nullptr);
    char* asset_root = file_realpath(home_scope.saved);
    ASSERT_NE(asset_root, nullptr);
    bool configured = host_paths_set(HOST_PATH_HOME, asset_root);
    mem_free(asset_root);
    ASSERT_TRUE(configured);
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("docs/model.dsl", "workspace { model {\n!include part.dsl\n!include ../secret.dsl\n} }\n");
    seed("docs/part.dsl", "person \"SAFE\"\n"); seed("secret.dsl", "person \"FORBIDDEN\"\n");
    seed("entry.ls", "let graph = input(\"@ROOT@/docs/model.dsl\", {type: 'graph', flavor: 'structurizr'})^\n"
        "let text = format(graph, 'json')^\ncontains(text, \"SAFE\") and not contains(text, \"FORBIDDEN\")\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL); EXPECT_TRUE(result->root.bool_val);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 2);
    EXPECT_GT(atomic_load32(&capture.source_reads), 1);
}

TEST_P(SecurityRuntimeTest, TexKernelCannotBypassRevokedReadAuthority) {
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("entry.ls", "contains(parse(\"\\\\ExplSyntaxOn\\\\int_eval:n{2+3}\\\\ExplSyntaxOff\", 'tex')^.text, \"5\")^\n");
    Input* trusted = load_file(); ASSERT_NE(trusted, nullptr);
    ASSERT_EQ(get_type_id(trusted->root), LMD_TYPE_BOOL); ASSERT_TRUE(trusted->root.bool_val);
    // Reusing the initialized Script package must not reuse an earlier data grant.
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE));
    expect_failed(load_file());
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, MathKernelCacheCannotBypassRevokedReadAuthority) {
    ASSERT_NE(admit_shipped_packages(), 0U);
    seed("entry.ls", "parse(\"x\", 'math')^ is <math>\n");
    Input* trusted = load_file(); ASSERT_NE(trusted, nullptr);
    ASSERT_EQ(get_type_id(trusted->root), LMD_TYPE_BOOL); ASSERT_TRUE(trusted->root.bool_val);
    // A cached kernel cannot extend the authority used to obtain its bytes.
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE));
    expect_failed(load_file());
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, PathMetadataAndExistsHaveIndependentAuthority) {
    seed("data.json", "{\"answer\":42}");
    seed("entry.ls", "let p = @PATH@[\"data.json\"]\np.size\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 13);
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE | SECURITY_READ));
    seed("denied.ls", "let p = @PATH@[\"data.json\"]\np.size\n");
    expect_failed(load_file("denied.ls"));
    seed("exists.ls", "exists(\"@ROOT@/data.json\")\n");
    result = load_file("exists.ls"); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_BOOL); EXPECT_FALSE(result->root.bool_val);
    EXPECT_GE(atomic_load32(&capture.metadata_reads), 3);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
}

TEST_P(SecurityRuntimeTest, DocumentPrefixProbesNeedReadRatherThanMetadata) {
    seed("data.json", "{\"answer\":42}");
    seed("entry.ls", "let p = @PATH@[\"data.json\"][\"answer\"]\np#\n");
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE | SECURITY_READ));
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 42);
    EXPECT_GE(atomic_load32(&capture.data_reads), 1);
    EXPECT_EQ(atomic_load32(&capture.metadata_reads), 0);
}

TEST_P(SecurityRuntimeTest, RecursiveWalksAndForcingUseEnumerationWithoutReadingFiles) {
    seed("tree/a.json", "{\"name\":\"A\"}"); seed("tree/sub/b.json", "{\"name\":\"B\"}");
    seed("entry.ls", "len((@PATH@[\"tree\"]).**#)^\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 4);
    seed("iteration.ls", "len([for (p in (@PATH@[\"tree\"]).**) p])^\n");
    result = load_file("iteration.ls"); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 4);
    EXPECT_EQ(atomic_load32(&capture.enumerations), 2); // the context-owned snapshot is reused
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE | SECURITY_READ));
    seed("denied.ls", "len((@PATH@[\"tree\"]).**#)^\n");
    expect_failed(load_file("denied.ls"));
    seed("denied-iteration.ls", "len([for (p in (@PATH@[\"tree\"]).**) p])^\n");
    result = load_file("denied-iteration.ls"); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 0);
    EXPECT_GT(atomic_load32(&capture.enumerations), 2); // refusal is rechecked, not served from the old snapshot
}

TEST_P(SecurityRuntimeTest, OutsidePathsAndSystemForcesCannotUseAnAmbientProvider) {
    seed("secret.json", "{\"answer\":42}", true);
    seed("entry.ls", "let p = (@PATH@).~~[\"secret.json\"]\np#\n");
    expect_failed(load_file());
    seed("system.ls", "sys.proc.self.pid#\n"); expect_failed(load_file("system.ls"));
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
}

TEST_P(SecurityRuntimeTest, ARepeatedDocumentForceStillChecksCurrentAuthority) {
    seed("data.json", "{\"answer\":42}");
    seed("entry.ls", "let p = @PATH@[\"data.json\"]\nlet first = p#answer\np#answer\n");
    capture.restrict_after_data_read = true;
    expect_failed(load_file());
    EXPECT_EQ(atomic_load32(&capture.data_reads), 1);
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, ParkedTaskReadsKeepTheirExplicitClient) {
    seed("data.txt", "task payload");
    run("pn reader() { sleep(1)^; io.read(\"@ROOT@/data.txt\")^ }\n"
        "pn main() { let h = start(reader); let text = wait(h)^; output(text, \"@ROOT@/task-result.txt\")^ }\n");
    expect_text("task-result.txt", "task payload");
    EXPECT_EQ(atomic_load32(&capture.data_reads), 1);
}

TEST_P(SecurityRuntimeTest, TaskReadsCannotOpenOutsideTheSelectedRoot) {
    seed("outside.txt", "private", true);
    run("pn reader() { io.read(\"@ROOT@/../outside.txt\")^ }\n"
        "pn main() { let h = start(reader); let text = wait(h)^; output(text, \"@ROOT@/leak.txt\")^ }\n");
    expect_failed(last_run);
    EXPECT_EQ(capture.writes, 0U); EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, TaskReadRevocationBeforeDeliverySuppressesTheResult) {
    seed("data.txt", "private"); capture.restrict_after_data_read = true;
    run("pn reader() { io.read(\"@ROOT@/data.txt\")^ }\n"
        "pn main() { let h = start(reader); let text = wait(h)^; output(text, \"@ROOT@/leak.txt\")^ }\n");
    expect_failed(last_run);
    EXPECT_EQ(capture.writes, 0U); EXPECT_EQ(atomic_load32(&capture.data_reads), 1);
    EXPECT_GE(atomic_load32(&capture.denied_reads), 1);
}

TEST_P(SecurityRuntimeTest, ProcessExecutionIsDeniedBeforeCreatingARequest) {
    run("pn main() { io.exec(\"/usr/bin/touch\", [\"@ROOT@/spawned\"])^ }\n");
    ASSERT_NE(last_run, nullptr); expect_failed(last_run);
    LambdaError* error = it2err(last_run->root); ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_FILE_ACCESS_DENIED);
    char* spawned = file_path_join(directory, "spawned");
    EXPECT_FALSE(file_exists(spawned)); mem_free(spawned);
}

TEST_P(SecurityRuntimeTest, UnsupportedMutationsCannotBypassTheBrokerEvenInDryRun) {
    seed("original", "original");
    char* original = file_path_join(directory, "original");
    FileRevision before = file_revision(original);
    const char* calls[] = {
        "io.copy(\"@ROOT@/original\", \"@ROOT@/side\")",
        "io.move(\"@ROOT@/original\", \"@ROOT@/side\")",
        "io.delete(\"@ROOT@/original\")", "io.mkdir(\"@ROOT@/side\")",
        "io.touch(\"@ROOT@/original\")",
        "io.create_file(\"@ROOT@/side\", \"never-stored\")",
        "io.symlink(\"@ROOT@/original\", \"@ROOT@/side\")",
        "io.chmod(\"@ROOT@/original\", 511)",
        "io.rename(\"@ROOT@/original\", \"@ROOT@/side\")",
        "cmd(\"/usr/bin/touch @ROOT@/side\")",
        "cmd(\"/usr/bin/touch\", [\"@ROOT@/side\"])"
    };
    expect_denied_calls(calls, sizeof(calls) / sizeof(calls[0]), "mutation");
    FileRevision after = file_revision(original); mem_free(original);
    EXPECT_TRUE(file_revision_equal(&before, &after)); expect_text("original", "original");
    char* side = file_path_join(directory, "side"); EXPECT_FALSE(file_exists(side)); mem_free(side);
    EXPECT_EQ(capture.writes, 0U);
}

TEST_P(SecurityRuntimeTest, UnsupportedSearchNetworkAndAccountServicesFailBeforeNativeJobs) {
    const char* calls[] = {
        "fetch(\"http://127.0.0.1:1/\", null)",
        "io.fetch(\"@ROOT@/entry.ls\")",
        "io.fetch(\"http://127.0.0.1:1/\", null)",
        "io.copy(\"http://127.0.0.1:1/\", \"@ROOT@/side\")",
        "io.grep(\"@ROOT@\", \"private\")",
        "io.grep(\"@ROOT@\", \"private\", {hidden: true})",
        "io.text_search(\"@ROOT@\", \"private\")",
        "io.text_search(\"@ROOT@\", \"private\", {hidden: true})",
        "secret.get(\"lambda-safety-denied\", \"unused\")",
        "secret.set(\"lambda-safety-denied\", \"unused\", \"never-stored\")",
        "secret.delete(\"lambda-safety-denied\", \"unused\")",
        "loop.listen()", "loop.accept(null, 1, null)",
        "sqlite.execute(\"@ROOT@/denied.db\", \"CREATE TABLE t(n INTEGER)\")",
        "sqlite.query(\"@ROOT@/denied.db\", \"SELECT 1\")",
        "sqlite.preimage(null, \"@ROOT@/denied.db\")",
        "mail.open(\"imap://127.0.0.1:1\", null)",
        "mail.send(\"smtp://127.0.0.1:1\", null, \"never-sent\")",
        "mail.command(null, \"NOOP\")",
        "mail.command(null, \"NOOP\", [])",
        "mail.command(null, \"NOOP\", [], null)", "mail.idle(null, 1)"
    };
    expect_denied_calls(calls, sizeof(calls) / sizeof(calls[0]), "service",
        "import secret: lambda.io.secret\nimport loop: lambda.io.loopback\nimport mail: lambda.io.mail\nimport sqlite: lambda.io.sqlite\n");
    char* denied_database = file_path_join(directory, "denied.db");
    EXPECT_FALSE(file_exists(denied_database)); mem_free(denied_database);
    EXPECT_EQ(capture.writes, 0U); EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    EXPECT_EQ(atomic_load32(&capture.metadata_reads), 0); EXPECT_EQ(atomic_load32(&capture.enumerations), 0);
}

TEST_P(SecurityRuntimeTest, ScopedSqliteDeniesBeforeOpeningOrCreatingTheFile) {
    run("import sqlite: lambda.io.sqlite\n"
        "pn main() { let target = sqlite.database(\"@ROOT@/denied-scoped.db\")^; "
        "open tx = target { sqlite.query(tx, \"SELECT 1\")^ }; return null }\n");
    expect_failed(last_run);
    char* path = file_path_join(directory, "denied-scoped.db");
    ASSERT_NE(path, nullptr);
    EXPECT_FALSE(file_exists(path));
    run("import sqlite: lambda.io.sqlite\n"
        "pn main() { let target = sqlite.existing(\"@ROOT@/denied-scoped.db\")^; "
        "open tx = target { sqlite.query(tx, \"SELECT 1\")^ }; return null }\n");
    expect_failed(last_run);
    EXPECT_FALSE(file_exists(path));
    run("import sqlite: lambda.io.sqlite\n"
        "pn main() { let target = sqlite.create(\"@ROOT@/denied-scoped.db\")^; "
        "open tx = target { sqlite.execute(tx, \"CREATE TABLE t(n INTEGER)\")^ }; return null }\n");
    expect_failed(last_run);
    EXPECT_FALSE(file_exists(path));
    run("import sqlite: lambda.io.sqlite\n"
        "pn main() { let target = sqlite.clone(\"@ROOT@/missing-source.db\", \"@ROOT@/denied-scoped.db\")^; "
        "open tx = target { sqlite.query(tx, \"SELECT 1\")^ }; return null }\n");
    expect_failed(last_run);
    EXPECT_FALSE(file_exists(path));
    mem_free(path);
}

TEST_P(SecurityRuntimeTest, ScopedRdbDeniesBeforeOpeningTheFile) {
    run("import rdb: lambda.io.rdb\n"
        "pn main() { let target = rdb.database(\"@ROOT@/denied-rdb.db\", \"sqlite\")^; "
        "open session = target { rdb.schema(session)^ }; return null }\n");
    expect_failed(last_run);
    char* path = file_path_join(directory, "denied-rdb.db");
    ASSERT_NE(path, nullptr);
    EXPECT_FALSE(file_exists(path));
    mem_free(path);
}

TEST_P(SecurityRuntimeTest, DatabaseHostHookDeniesBeforeOpeningOrCreatingTheFile) {
    run("pn main() { null }\n");
    char* path = file_path_join(directory, "denied.db"); ASSERT_NE(path, nullptr);
    RdbConn* connection = rdb_open(runtime_get_eval_context(&runtime)->pool, path, "sqlite", false);
    EXPECT_EQ(connection, nullptr);
    if (connection) rdb_close(connection);
    EXPECT_FALSE(file_exists(path)); mem_free(path);
}

TEST_P(SecurityRuntimeTest, ProcedureHandlersExposeDenialsAndResumeWithTheirSelectedValues) {
    seed("payload.txt", "payload");
    run("pn bad() { io.touch(\"@ROOT@/side\")^ }\n"
        "pn good() { 7 }\n"
        "pn reader() { io.read(\"@ROOT@/payload.txt\")^ }\n"
        "pn final_handler() { 9; bad() ^ { ^.code } }\n"
        "pn yields_null() { null }\n"
        "pn final_null() { 9; yields_null() ^ { 0 } }\n"
        "pn main() { let denied = bad() ^ { ^.code }; "
        "let codes = [bad() ^ { ^.code }, good() ^ { 0 }, (good() ^ { 0 }) + 1]; "
        "let resumed = reader() ^ { ^.code }; "
        "{denied: denied, codes: codes, resumed: resumed, "
        "final: final_handler(), replaced: final_null() is null} }\n");
    ASSERT_NE(last_run, nullptr); EXPECT_NE(get_type_id(last_run->root), LMD_TYPE_ERROR);
    MapReader map = MapReader::fromItem(last_run->root);
    EXPECT_EQ(map.get("denied").asInt(), ERR_FILE_ACCESS_DENIED);
    ArrayReader codes = ArrayReader::fromItem(map.get("codes").item());
    ASSERT_EQ(codes.length(), 3); EXPECT_EQ(codes.get(0).asInt(), ERR_FILE_ACCESS_DENIED);
    EXPECT_EQ(codes.get(1).asInt(), 7); EXPECT_EQ(codes.get(2).asInt(), 8);
    String* resumed = map.get("resumed").asString(); ASSERT_NE(resumed, nullptr);
    EXPECT_EQ(resumed->len, 7U); EXPECT_EQ(memcmp(resumed->chars, "payload", 7), 0);
    EXPECT_EQ(map.get("final").asInt(), ERR_FILE_ACCESS_DENIED); EXPECT_TRUE(map.get("replaced").asBool());
    char* side = file_path_join(directory, "side"); EXPECT_FALSE(file_exists(side)); mem_free(side);
}

TEST_P(SecurityRuntimeTest, DynamicSnapshotCapsuleReleasesRootsWithItsHeap) {
    run("pn main() { null }\n");
    EvalContext* owner = runtime_get_eval_context(&runtime);
    Path* path = path_extend(owner->pool, path_new(owner->pool, PATH_SCHEME_REL), "cache-case");
    ASSERT_NE(path, nullptr);
    SecurityAuthorityStamp stamp;
    ASSERT_TRUE(security_io_authority_stamp(eval_context_security_io(owner), &stamp, nullptr));
    RootFrame roots(1);
    Rooted<Array*> value(roots, array_plain());
    ASSERT_TRUE(lambda_security_path_cache_store(path, &stamp, ((Item){.array = value.get()}).item));
    Item cached;
    ASSERT_TRUE(lambda_security_path_cache_get(path, &stamp, &cached.item));
    EXPECT_EQ(cached.array, value.get());
    context_capsule_release_heap(owner);
    EXPECT_FALSE(lambda_security_path_cache_get(path, &stamp, &cached.item));
    context_capsule_release_heap(owner);
    EXPECT_FALSE(lambda_security_path_cache_get(path, &stamp, &cached.item));
}

TEST_P(SecurityRuntimeTest, ALooseEntryCanImportOnlyItsSelectedSiblingDependency) {
    seed("entry.ls", "import dep: .dep\ndep.answer\n");
    seed("dep.ls", "pub let answer = 42\n");
    SecurityFileRootSpec entry = {directory, SECURITY_SCOPE_ENTRY,
        SECURITY_READ | SECURITY_METADATA | SECURITY_LOAD_SOURCE, 0, 0, true, true, "entry.ls"};
    ASSERT_TRUE(replace_admission(&entry, 41, 2041));
    SecurityFileRootSpec dependency = {directory, SECURITY_SCOPE_SELECTED,
        SECURITY_READ | SECURITY_LOAD_SOURCE, SECURITY_READ | SECURITY_LOAD_SOURCE,
        0, true, false, "dep.ls"};
    uint64_t selected = security_file_broker_admit_root(broker, &dependency);
    ASSERT_NE(selected, 0U);
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_EQ(get_type_id(result->root), LMD_TYPE_INT); EXPECT_EQ(it2i(result->root), 42);
    ASSERT_TRUE(security_file_broker_revoke_root(broker, selected));
    expect_failed(load_file());
}

TEST_P(SecurityRuntimeTest, RecursiveWalksShareTheirEntryBudgetAcrossSiblingSubtrees) {
    // Each directory fits the per-request cap; their combined closure does not.
    for (unsigned i = 0; i < SECURITY_IO_DIRECTORY_LIMIT - 3; i++) {
        StrBuf* name = strbuf_new(); strbuf_append_format(name, "large/a-nested/file-%u", i);
        seed(name->str, ""); strbuf_free(name);
    }
    seed("large/after-one", ""); seed("large/after-two", ""); seed("large/after-three", "");
    seed("entry.ls", "(@PATH@[\"large\"]).**#\n");
    expect_failed(load_file());
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
}

TEST_P(SecurityRuntimeTest, CancellingAnInFlightReadJoinsTheBorrowedTransport) {
    seed("data.txt", "private");
    char* started = file_path_join(directory, "read-started");
    char* release = file_path_join(directory, "read-release");
    capture.read_started_path = started; capture.read_release_path = release;
    run("pn reader(parent) { send(parent, \"ready\")^; io.read(\"@ROOT@/data.txt\")^ }\n"
        "pn main() { let h = start(reader, [self()]); receive()^; var polls = 0; "
        "while (not exists(\"@ROOT@/read-started\") and polls < 5000) { sleep(1)^; polls = polls + 1 }; "
        "cancel(h); output(\"release\", \"@ROOT@/read-release\")^; wait(h)^ }\n");
    EXPECT_EQ(atomic_load32(&capture.data_reads), 1);
    EXPECT_TRUE(file_exists(started)); EXPECT_TRUE(file_exists(release));
    ASSERT_NE(last_run, nullptr); expect_failed(last_run);
    LambdaError* error = it2err(last_run->root); ASSERT_NE(error, nullptr);
    EXPECT_EQ(error->code, ERR_CANCELLED);
    capture.read_started_path = nullptr; capture.read_release_path = nullptr;
    mem_free(started); mem_free(release);
}

TEST_P(SecurityRuntimeTest, InMemoryDocumentIterationNeedsNoExternalAuthority) {
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE));
    seed("entry.ls", "let doc = temp('local_rows', [{n: 3}, {n: 4}])^;\n"
        "[len(doc), len([for (row in temp.'local_rows') row]), "
        "len([for (n in temp.'local_rows'.*.n) n])]\n");
    Input* result = load_file(); ASSERT_NE(result, nullptr);
    ASSERT_TRUE(get_type_id(result->root) == LMD_TYPE_ARRAY ||
        get_type_id(result->root) == LMD_TYPE_ARRAY_NUM);
    ArrayReader value = ArrayReader::fromItem(result->root);
    ASSERT_EQ(value.length(), 3);
    EXPECT_EQ(value.get(0).asInt(), 2);
    EXPECT_EQ(value.get(1).asInt(), 2);
    EXPECT_EQ(value.get(2).asInt(), 2);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    EXPECT_EQ(atomic_load32(&capture.metadata_reads), 0);
    EXPECT_EQ(atomic_load32(&capture.enumerations), 0);
}

TEST_P(SecurityRuntimeTest, SuppliedDocumentReferenceSurvivesFurtherInput) {
    run("temp('supplied_input', {body: {answer: 42}})^\n");
    ASSERT_NE(last_run, nullptr);
    ASSERT_NE(doc_context_set_supplied(last_run->root), nullptr);
    ASSERT_TRUE(security_file_broker_restrict(broker, SECURITY_LOAD_SOURCE));
    run("let ref = \\.~doc; let other = temp('other_input', {answer: 99})^; "
        "[~doc.body.answer, (ref ++ \\.body.answer)#, other.answer, ~doc.body.answer]\n");
    ASSERT_NE(last_run, nullptr);
    ArrayReader values = ArrayReader::fromItem(last_run->root);
    ASSERT_EQ(values.length(), 4);
    EXPECT_EQ(values.get(0).asInt(), 42);
    EXPECT_EQ(values.get(1).asInt(), 42);
    EXPECT_EQ(values.get(2).asInt(), 99);
    EXPECT_EQ(values.get(3).asInt(), 42);
    Path* captured = doc_context_get_supplied()->path;
    run("temp('replacement_input', {answer: 7})^\n");
    ASSERT_NE(doc_context_set_supplied(last_run->root), nullptr);
    Item old_document = fn_force((Item){.path = captured});
    EXPECT_EQ(MapReader::fromItem(old_document).get("body").asMap().get("answer").asInt(), 42);
    run("~doc.answer\n");
    EXPECT_EQ(it2i(last_run->root), 7);
}

TEST_P(SecurityRuntimeTest, WorkingDirectoryReferenceUsesEvaluationSnapshot) {
    run("\\.~cwd\n");
    ASSERT_NE(last_run, nullptr);
    ASSERT_EQ(get_type_id(last_run->root), LMD_TYPE_PATH);
    Path* original = last_run->root.path;
    char previous[4096];
    ASSERT_NE(getcwd(previous, sizeof(previous)), nullptr);
    ASSERT_EQ(chdir(directory), 0);
    run("\\.~cwd\n");
    int restore_result = chdir(previous);
    ASSERT_EQ(restore_result, 0);
    ASSERT_NE(last_run, nullptr);
    ASSERT_EQ(get_type_id(last_run->root), LMD_TYPE_PATH);
    EXPECT_TRUE(path_equal(original, last_run->root.path));
}

TEST_P(SecurityRuntimeTest, TemporaryDirectoryUsesEvaluationSnapshot) {
    const char* original_value = getenv("TMPDIR");
    char* original = original_value ? mem_strdup(original_value, MEM_CAT_TEMP) : nullptr;
    run("null\n");
    ASSERT_EQ(setenv("TMPDIR", directory, 1), 0);
    run("\\.~temp\n");
    if (original) setenv("TMPDIR", original, 1);
    else unsetenv("TMPDIR");
    ASSERT_NE(last_run, nullptr);
    ASSERT_EQ(get_type_id(last_run->root), LMD_TYPE_PATH);
    StrBuf* actual = strbuf_new();
    ASSERT_NE(actual, nullptr);
    path_to_os_path(last_run->root.path, actual);
    const char* expected = original ? original : "/tmp";
    size_t length = strlen(expected);
    while (length > 1 && expected[length - 1] == '/') length--;
    EXPECT_EQ(actual->length, length);
    EXPECT_EQ(strncmp(actual->str, expected, length), 0);
    strbuf_free(actual);
    mem_free(original);
}

TEST_P(SecurityRuntimeTest, ContextDirectoriesResolveDistinctSentinels) {
    seed("cwd/cwd_marker", "");
    seed("home/home_marker", "");
    seed("scratch/temp_marker", "");
    char* cwd = file_path_join(directory, "cwd");
    char* home = file_path_join(directory, "home");
    char* scratch = file_path_join(directory, "scratch");
    ASSERT_NE(cwd, nullptr); ASSERT_NE(home, nullptr); ASSERT_NE(scratch, nullptr);

    // Restore process state even if a fixture assertion aborts this test.
    ScopedContextEnvironment previous = {};
    ASSERT_TRUE(previous.capture());
    ASSERT_EQ(chdir(cwd), 0);
    ASSERT_EQ(setenv("HOME", home, 1), 0);
    ASSERT_EQ(setenv("TMPDIR", scratch, 1), 0);
    mem_free(cwd); mem_free(home); mem_free(scratch);

    // Evaluation snapshots precede the first context-root observation.
    runtime_cleanup(&runtime); runtime_live = false;
    start_runtime();
    run("[~cwd.cwd_marker is file, ~home.home_marker is file, "
        "~temp.temp_marker is file, "
        "~home[\\.home_marker] is file, (\\.~home ++ \\.home_marker)# is file, "
        "(\\.~home ++ \\.missing_child) is path]\n");
    ASSERT_NE(last_run, nullptr);
    ArrayReader values = ArrayReader::fromItem(last_run->root);
    ASSERT_EQ(values.length(), 6);
    for (int64_t i = 0; i < values.length(); i++) EXPECT_TRUE(values.get(i).asBool());
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
}

TEST_P(SecurityRuntimeTest, DirectorySnapshotsAreIndependentAcrossEvaluations) {
    seed("home_a/first", ""); seed("home_b/second", "");
    seed("temp_a/first", ""); seed("temp_b/second", "");
    char* home_a = file_path_join(directory, "home_a");
    char* home_b = file_path_join(directory, "home_b");
    char* temp_a = file_path_join(directory, "temp_a");
    char* temp_b = file_path_join(directory, "temp_b");
    ASSERT_NE(home_a, nullptr); ASSERT_NE(home_b, nullptr);
    ASSERT_NE(temp_a, nullptr); ASSERT_NE(temp_b, nullptr);
    ScopedContextEnvironment previous = {};
    ASSERT_TRUE(previous.capture());
    ASSERT_EQ(setenv("HOME", home_a, 1), 0);
    ASSERT_EQ(setenv("TMPDIR", temp_a, 1), 0);
    runtime_cleanup(&runtime); runtime_live = false;
    start_runtime();
    run("null\n");
    ASSERT_EQ(setenv("HOME", home_b, 1), 0);
    ASSERT_EQ(setenv("TMPDIR", temp_b, 1), 0);
    run("[~home.first is file, ~temp.first is file]\n");
    ASSERT_NE(last_run, nullptr);
    ArrayReader original = ArrayReader::fromItem(last_run->root);
    ASSERT_EQ(original.length(), 2);
    EXPECT_TRUE(original.get(0).asBool()); EXPECT_TRUE(original.get(1).asBool());

    // A new evaluation sees the changed environment; the old one retains its snapshot.
    runtime_cleanup(&runtime); runtime_live = false;
    start_runtime();
    run("[~home.second is file, ~temp.second is file]\n");
    ASSERT_NE(last_run, nullptr);
    ArrayReader next = ArrayReader::fromItem(last_run->root);
    ASSERT_EQ(next.length(), 2);
    EXPECT_TRUE(next.get(0).asBool()); EXPECT_TRUE(next.get(1).asBool());
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    mem_free(home_a); mem_free(home_b); mem_free(temp_a); mem_free(temp_b);
}

TEST_P(SecurityRuntimeTest, ProcessContextUsesSystemProviderPermissions) {
    // S2.4.8 requires the alias and the original provider to share denial.
    run("~proc.pid\n");
    ASSERT_NE(last_run, nullptr);
    ASSERT_EQ(get_type_id(last_run->root), LMD_TYPE_ERROR);
    LambdaError* alias_error = it2err(last_run->root);
    ASSERT_NE(alias_error, nullptr);
    int alias_code = alias_error->code;
    run("sys.proc.self.pid\n");
    ASSERT_NE(last_run, nullptr);
    ASSERT_EQ(get_type_id(last_run->root), LMD_TYPE_ERROR);
    LambdaError* original_error = it2err(last_run->root);
    ASSERT_NE(original_error, nullptr);
    EXPECT_EQ(alias_code, original_error->code);
    EXPECT_EQ(alias_code, ERR_FILE_ACCESS_DENIED);
    run("\\.~proc.pid is path\n");
    ASSERT_NE(last_run, nullptr);
    EXPECT_EQ(last_run->root.item, b2it(BOOL_TRUE));
    run("(\\.~proc.pid)#\n");
    ASSERT_NE(last_run, nullptr);
    ASSERT_EQ(get_type_id(last_run->root), LMD_TYPE_ERROR);
    EXPECT_EQ(it2err(last_run->root)->code, alias_code);
}

TEST_P(SecurityRuntimeTest, CachedModuleRetainsDefiningScriptReference) {
    seed("helper.ls", "pub fn source() => \\.~script\n");
    seed("entry.ls", "import helper: .helper\nhelper.source() == (@PATH@ ++ \\.'helper.ls')\n");
    Input* first = load_file();
    ASSERT_NE(first, nullptr);
    ASSERT_EQ(first->root.item, b2it(BOOL_TRUE));
    runtime_cleanup(&runtime); runtime_live = false;
    start_runtime();
    InputScriptCacheStats before = {}, after = {};
    input_script_cache_get_stats(input_manager_global_script_cache(), &before);
    Input* second = load_file();
    ASSERT_NE(second, nullptr);
    EXPECT_EQ(second->root.item, b2it(BOOL_TRUE));
    input_script_cache_get_stats(input_manager_global_script_cache(), &after);
    EXPECT_GT(after.ast_hits + after.mir_hits, before.ast_hits + before.mir_hits);
}

TEST_P(SecurityRuntimeTest, InlineSourceRetainsItsDefiningFile) {
    seed("entry.ls", "null\n");
    run("[~script is file, \\.~script == (@PATH@ ++ \\.'entry.ls'), "
        "(\\.~script)# is file]\n");
    ASSERT_NE(last_run, nullptr);
    ArrayReader values = ArrayReader::fromItem(last_run->root);
    ASSERT_EQ(values.length(), 3);
    for (int64_t i = 0; i < values.length(); i++) EXPECT_TRUE(values.get(i).asBool());
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    run("[~script == null, \\.~script == null]\n", (char*)"<inline-source>");
    ASSERT_NE(last_run, nullptr);
    values = ArrayReader::fromItem(last_run->root);
    ASSERT_EQ(values.length(), 2);
    for (int64_t i = 0; i < values.length(); i++) EXPECT_TRUE(values.get(i).asBool());
}

TEST_P(SecurityRuntimeTest, InlineSourceFileUrlRetainsItsDefiningFile) {
    seed("source file.ls", "null\n");
    char* filename = file_path_join(directory, "source file.ls");
    ASSERT_NE(filename, nullptr);
    char* url = url_from_local_path(filename);
    mem_free(filename);
    ASSERT_NE(url, nullptr);
    run("[~script is file, \\.~script == (@PATH@ ++ \\.'source file.ls'), "
        "(\\.~script)# is file]\n", url);
    mem_free(url);
    ASSERT_NE(last_run, nullptr);
    ArrayReader values = ArrayReader::fromItem(last_run->root);
    ASSERT_EQ(values.length(), 3);
    for (int64_t i = 0; i < values.length(); i++) EXPECT_TRUE(values.get(i).asBool());
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);

    // A remote authority must survive capture without probing the filesystem.
    Pool* pool = runtime_get_eval_context(&runtime)->pool;
    int metadata_before = atomic_load32(&capture.metadata_reads);
    Path* remote = lambda_context_path(pool, LAMBDA_CONTEXT_PATH_SCRIPT,
        "file://other-host/share/source%20file.ls");
    Path* expected = path_new_authority(pool, PATH_SCHEME_FILE, "other-host");
    expected = path_extend(pool, expected, "share");
    expected = path_extend(pool, expected, "source file.ls");
    ASSERT_NE(remote, nullptr);
    EXPECT_TRUE(path_equal(remote, expected));
    EXPECT_EQ(atomic_load32(&capture.metadata_reads), metadata_before);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
}

TEST_P(SecurityRuntimeTest, WindowsUncCapturePreservesRemoteAuthority) {
    run("null\n");
    Pool* pool = runtime_get_eval_context(&runtime)->pool;
    int metadata_before = atomic_load32(&capture.metadata_reads);
    Path* captured = lambda_context_windows_unc_path(pool,
        "\\\\context-test.invalid\\share\\folder\\..\\source file.ls");
    Path* expected = path_new_authority(pool, PATH_SCHEME_FILE, "context-test.invalid");
    expected = path_extend(pool, expected, "share");
    expected = path_extend(pool, expected, "source file.ls");
    ASSERT_NE(captured, nullptr);
    EXPECT_TRUE(path_equal(captured, expected));
    EXPECT_FALSE(path_file_authority_is_local(captured));
    StrBuf* projected = strbuf_new();
    ASSERT_NE(projected, nullptr);
    path_to_windows_path(captured, projected);
    EXPECT_STREQ(projected->str, "\\\\context-test.invalid\\share\\source file.ls");
    strbuf_free(projected);
    EXPECT_EQ(atomic_load32(&capture.metadata_reads), metadata_before);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    char* local_hostname = shell_get_hostname();
    ASSERT_NE(local_hostname, nullptr);
    if (strcmp(local_hostname, "localhost") != 0) {
        Path* loopback = lambda_context_windows_unc_path(pool, "\\\\localhost\\share\\file");
        Path* local_share = path_new_authority(pool, PATH_SCHEME_FILE, local_hostname);
        local_share = path_extend(pool, local_share, "share");
        local_share = path_extend(pool, local_share, "file");
        ASSERT_NE(loopback, nullptr);
        EXPECT_TRUE(path_equal(loopback, local_share));
    }
    mem_free(local_hostname);
}

TEST_P(SecurityRuntimeTest, SuppliedDocumentPathLoadsOnlyWhenForced) {
    seed("document.json", "{\"answer\": 17}");
    run("null\n");
    char* filename = file_path_join(directory, "document.json");
    ASSERT_NE(filename, nullptr);
    RootFrame roots(1);
    Rooted<Item> target(roots, (Item){.item = s2it(heap_strcpy(filename, strlen(filename)))});
    mem_free(filename);
    ASSERT_NE(doc_context_set_supplied_target(target.get()), nullptr);
    run("\\.~doc is path\n");
    EXPECT_EQ(atomic_load32(&capture.data_reads), 0);
    run("~doc.answer\n");
    ASSERT_NE(last_run, nullptr);
    EXPECT_EQ(it2i(last_run->root), 17);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 1);
}

TEST_P(SecurityRuntimeTest, DocumentTransformPublishesSuppliedDocument) {
    seed("helper.ls", "pub fn render(source, options) => [~doc.answer, (\\.~doc)#.answer]\n");
    seed("document.json", "{\"answer\": 17}");
    char* filename = file_path_join(directory, "document.json");
    ASSERT_NE(filename, nullptr);
    LambdaDocumentTransformConfig config = {"json", ".helper", "render",
        LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PATH, false};
    Input* result = run_lambda_document_transform_with_options(&runtime, filename, &config, nullptr, 0);
    ASSERT_NE(result, nullptr);
    if (get_type_id(result->root) == LMD_TYPE_ERROR) {
        LambdaError* error = it2err(result->root);
        FAIL() << "transform failed: " << (error && error->message ? error->message : "unknown")
            << ", denied=" << atomic_load32(&capture.denied_reads)
            << ", source reads=" << atomic_load32(&capture.source_reads);
    }
    ArrayReader values = ArrayReader::fromItem(result->root);
    ASSERT_EQ(values.length(), 2);
    EXPECT_EQ(values.get(0).asInt(), 17);
    EXPECT_EQ(values.get(1).asInt(), 17);
    EXPECT_EQ(atomic_load32(&capture.data_reads), 1);
    config.source = LAMBDA_DOCUMENT_TRANSFORM_SOURCE_PARSED;
    result = run_lambda_document_transform_with_options(&runtime, filename, &config, nullptr, 0);
    mem_free(filename);
    ASSERT_NE(result, nullptr);
    values = ArrayReader::fromItem(result->root);
    ASSERT_EQ(values.length(), 2);
    EXPECT_EQ(values.get(0).asInt(), 17);
    EXPECT_EQ(values.get(1).asInt(), 17);
}

TEST_P(SecurityRuntimeTest, DocumentHeadsArePrivateAndSurviveAnotherContextsTeardown) {
    run("temp('shared_head', [7])^\n");
    EvalContext* owner = runtime_get_eval_context(&runtime);
    const char* key = "temp.shared_head";
    Document* original = doc_context_lookup(key, strlen(key));
    ASSERT_NE(original, nullptr);
    Runtime secondary = {};
    runtime_init(&secondary);
    EvalContext* peer = runtime_get_eval_context(&secondary);
    EXPECT_TRUE(eval_context_transfer_thread(peer));
    SecurityIOClient binding = binding_for(&capture);
    EXPECT_TRUE(eval_context_bind_security_io(peer, &binding));
    Input* result = run_script_mir(&secondary, "temp('shared_head', [9])^\n", script_path, true);
    EXPECT_NE(result, nullptr);
    Document* other = doc_context_lookup(key, strlen(key));
    EXPECT_NE(other, nullptr); EXPECT_NE(other, original);
    if (other) EXPECT_EQ(ArrayReader::fromItem(other->head).get(0).asInt(), 9);
    runtime_cleanup(&secondary);
    ASSERT_TRUE(eval_context_transfer_thread(owner));
    EXPECT_EQ(doc_context_lookup(key, strlen(key)), original);
    EXPECT_EQ(ArrayReader::fromItem(original->head).get(0).asInt(), 7);
}

TEST_P(SecurityRuntimeTest, AnotherEvaluatorCannotObserveOrClearAnOpenTransaction) {
    run("pn main() { null }\n");
    EvalContext* owner = runtime_get_eval_context(&runtime);
    write_set_begin_transaction();
    EXPECT_TRUE(write_set_transaction_open());
    Runtime secondary = {};
    runtime_init(&secondary);
    EvalContext* peer = runtime_get_eval_context(&secondary);
    EXPECT_TRUE(eval_context_transfer_thread(peer));
    EXPECT_FALSE(write_set_transaction_open());
    write_set_reset();
    runtime_cleanup(&secondary);
    EXPECT_TRUE(eval_context_transfer_thread(owner));
    EXPECT_TRUE(write_set_transaction_open());
    write_set_reset();
}

TEST_P(SecurityRuntimeTest, PendingEditsKeepTheirOwnersRootsThroughPeerExecutionAndCleanup) {
    run("temp('pending_head', [7])^\n");
    EvalContext* owner = runtime_get_eval_context(&runtime);
    const char* key = "temp.pending_head";
    Document* original = doc_context_lookup(key, strlen(key)); ASSERT_NE(original, nullptr);
    ASSERT_TRUE(write_set_begin_transaction()); write_set_set_confinement(original);
    const char* payload = "pending value retained only by its evaluation-owned write set";
    WriteEdit edit = {};
    edit.op = WRITE_OP_PUT; edit.anchor = original->head.container; edit.doc = original;
    edit.has_key = true; edit.key_index = 0; edit.value = {.item = s2it(heap_strcpy(payload, strlen(payload)))};
    ASSERT_TRUE(write_set_record(&edit)); edit.value = ItemNull;

    Runtime secondary = {}; runtime_init(&secondary);
    EvalContext* peer = runtime_get_eval_context(&secondary);
    EXPECT_TRUE(eval_context_transfer_thread(peer));
    SecurityIOClient binding = binding_for(&capture);
    EXPECT_TRUE(eval_context_bind_security_io(peer, &binding));
    Input* result = run_script_mir(&secondary, "temp('pending_head', [9])^\n", script_path, true);
    EXPECT_NE(result, nullptr); EXPECT_FALSE(write_set_transaction_open());
    EXPECT_EQ(write_set_confinement(), nullptr);
    EXPECT_FALSE(write_set_record(&edit)); // a foreign document cannot enter this owner's set
    EXPECT_TRUE(write_set_begin_transaction());
    EXPECT_TRUE(eval_context_transfer_thread(owner));
    // Explicit-subject teardown must not unregister roots from the active owner.
    context_capsule_release_heap(peer);
    EXPECT_TRUE(write_set_transaction_open()); EXPECT_EQ(write_set_confinement(), original);
    EXPECT_TRUE(eval_context_transfer_thread(peer));
    runtime_cleanup(&secondary);
    ASSERT_TRUE(eval_context_transfer_thread(owner));
    EXPECT_TRUE(write_set_transaction_open()); EXPECT_EQ(write_set_confinement(), original);
    for (unsigned i = 0; i < 64; i++) (void)heap_strcpy(payload, strlen(payload));
    EXPECT_EQ(ArrayReader::fromItem(original->head).get(0).asInt(), 7);
    ASSERT_TRUE(write_set_end_transaction(false));
    String* actual = ArrayReader::fromItem(original->head).get(0).asString();
    ASSERT_NE(actual, nullptr); EXPECT_EQ(actual->len, strlen(payload));
    EXPECT_EQ(memcmp(actual->chars, payload, strlen(payload)), 0);
    EXPECT_FALSE(write_set_transaction_open()); EXPECT_EQ(write_set_confinement(), nullptr);
}

INSTANTIATE_TEST_SUITE_P(InterpreterAndJit, SecurityRuntimeTest,
    ::testing::Values(LAMBDA_TIER_INTERP, LAMBDA_TIER_JIT));

class ProcessContextEnvironmentTest : public ::testing::TestWithParam<LambdaTier> {
protected:
    Runtime runtime = {};
    LambdaTestNativeCompiler compiler;
    void SetUp() override {
        log_init(nullptr);
        compiler.select(GetParam());
        runtime_init(&runtime);
    }
    void TearDown() override { runtime_cleanup(&runtime); }
};

TEST_P(ProcessContextEnvironmentTest, BoundAndDirectReadsKeepLongNames) {
    char name[301];
    memset(name, 'L', sizeof(name) - 1); name[sizeof(name) - 1] = 0;
    struct RestoreVariable {
        const char* name;
        char* previous;
        bool changed;
        ~RestoreVariable() {
            if (changed) {
                if (previous) shell_setenv(name, previous); else shell_unsetenv(name);
            }
            mem_free(previous);
        }
    } restore = {name, nullptr, false};
    const char* prior = shell_getenv(name);
    if (prior) {
        restore.previous = mem_strdup(prior, MEM_CAT_TEMP);
        ASSERT_NE(restore.previous, nullptr);
    }
    ASSERT_TRUE(shell_setenv(name, "fixture"));
    restore.changed = true;
    StrBuf* source = strbuf_new(); ASSERT_NE(source, nullptr);
    strbuf_append_format(source,
        "let proc = ~proc\n;[proc.env[\"%s\"] == \"fixture\", "
        "sys.proc.self.env.'%s' == \"fixture\"]\n", name, name);
    Script* script = nullptr;
    Input* result = run_script_mir(&runtime, source->str, (char*)"<long-env-test>", false, &script);
    strbuf_free(source);
    compiler.expect_mvp_admitted(script);
    ASSERT_NE(result, nullptr);
    ArrayReader values = ArrayReader::fromItem(result->root);
    ASSERT_EQ(values.length(), 2);
    EXPECT_TRUE(values.get(0).asBool()); EXPECT_TRUE(values.get(1).asBool());
}

INSTANTIATE_TEST_SUITE_P(InterpreterAndJit, ProcessContextEnvironmentTest,
    ::testing::Values(LAMBDA_TIER_INTERP, LAMBDA_TIER_JIT));
#endif
