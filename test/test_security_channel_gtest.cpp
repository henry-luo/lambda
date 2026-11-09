#include <gtest/gtest.h>
#include "lambda/io/security_channel.h"
#include "../lambda/host/safety/file_broker.h"
#include "lib/endian.h"
#include "lib/file_utils.h"
#include "lib/ipc_stream.h"
#include "lib/log.h"
#include "safety/channel_wire_fixture.h"
#include <errno.h>
#include <limits.h>
#include <string.h>
#include <stdlib.h>
#ifndef _WIN32
#include "lib/time_util.h"
#include <fcntl.h>
#include <pthread.h>
#include <sys/socket.h>
#include <unistd.h>

struct ChannelServer {
    SecurityChannel* channel;
    SecurityChannelConfig config;
    SecurityIOClient client;
    SecurityChannelReport report;
    bool success;
};
static void* serve_channel(void* data) {
    ChannelServer* server = (ChannelServer*)data;
    server->success = security_channel_dispatch(server->channel, &server->client, &server->report);
    security_channel_destroy(server->channel); server->channel = nullptr;
    return nullptr;
}
static uint64_t channel_test_clock(void*) { return 100; }

class SecurityChannelTest : public ::testing::Test {
protected:
    char* temporary = nullptr;
    char* root = nullptr;
    char* protected_path = nullptr;
    SecurityFileBroker* broker = nullptr;
    SecurityIOClient local = {}, remote = {};
    SecurityChannel* channel = nullptr;
    ChannelServer server = {};
    pthread_t thread;
    bool running = false;
    uint64_t root_id = 0;

    void SetUp() override {
        log_init(nullptr);
        temporary = dir_temp_create("security_channel"); ASSERT_NE(temporary, nullptr);
        root = file_realpath(temporary); ASSERT_NE(root, nullptr);
        protected_path = file_path_join(root, "protected"); ASSERT_TRUE(create_dir(protected_path));
        SecurityFileBrokerConfig config = {{51, 1051, SECURITY_ALL_OPERATIONS, 64, 64},
            4096, 1024 * 1024, 8192, 8, 128, 60, channel_test_clock, nullptr};
        broker = security_file_broker_create(&config); ASSERT_NE(broker, nullptr);
        ASSERT_TRUE(security_file_broker_protect(broker, protected_path, SECURITY_ALL_OPERATIONS));
        SecurityFileRootSpec spec = {root, SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
            0, 0, true, true, nullptr};
        root_id = security_file_broker_admit_root(broker, &spec); ASSERT_NE(root_id, 0U);
        local = security_file_broker_client(broker);
    }
    int start_server(int fd, SecurityChannelConfig config, SecurityIOClient client) {
        server = {nullptr, config, client, {}, false};
        // adopt before the peer can close; Darwin getpeername then rejects the
        // disconnected socket, while production also constructs before resume.
        server.channel = security_channel_create(fd, &server.config);
        if (!server.channel) { close(fd); return -1; }
        if (pthread_create(&thread, nullptr, serve_channel, &server)) {
            security_channel_destroy(server.channel); server.channel = nullptr; return -1;
        }
        running = true; return 0;
    }
    int start(bool raw = false, uint32_t timeout = 1000, uint32_t requests = 1024,
            uint64_t bytes = 8 * 1024 * 1024) {
        int pair[2];
        if (socketpair(AF_UNIX, SOCK_STREAM, 0, pair)) return -1;
        if (start_server(pair[0], {1051, bytes, 16384, requests, timeout}, local)) { close(pair[1]); return -1; }
        if (raw) return pair[1];
        channel = security_channel_create(pair[1], &server.config);
        if (!channel) { close(pair[1]); return -1; }
        remote = security_channel_client(channel); return 0;
    }
    void stop() {
        security_channel_destroy(channel); channel = nullptr;
        if (running) { pthread_join(thread, nullptr); running = false; }
    }
    void TearDown() override {
        stop(); security_file_broker_destroy(broker);
        if (temporary) dir_delete(temporary);
        mem_free(protected_path); mem_free(root); mem_free(temporary);
    }
    void seed(const char* name, const char* bytes) {
        char* path = file_path_join(root, name);
        ASSERT_EQ(write_binary_file(path, bytes, strlen(bytes)), 0); mem_free(path);
    }
    void expect_text(const char* name, const char* expected) {
        char* path = file_path_join(root, name); char* bytes = nullptr; size_t size = 0;
        ASSERT_TRUE(file_read_all(path, MEM_CAT_TEMP, &bytes, &size));
        EXPECT_EQ(size, strlen(expected)); EXPECT_EQ(memcmp(bytes, expected, size), 0);
        mem_free(bytes); mem_free(path);
    }
    void expect_read(const char* name, const char* expected, bool source = false) {
        char* bytes = nullptr; size_t size = 0; SecurityIOResult result;
        ASSERT_TRUE(security_io_read_bytes(&remote, name, source, 4096, MEM_CAT_TEMP, &bytes, &size, &result));
        EXPECT_EQ(size, strlen(expected)); EXPECT_EQ(memcmp(bytes, expected, size), 0); mem_free(bytes);
    }
    static void header(uint8_t* bytes, uint16_t operation, uint32_t size, uint64_t sequence = 1) {
        safety_channel_header(bytes, 1051, operation, size, sequence);
    }
};

static bool channel_test_receive(int fd, void* bytes, size_t size) {
    size_t offset = 0;
    while (offset < size) {
        ssize_t count = read(fd, (char*)bytes + offset, size - offset);
        if (count < 0 && errno == EINTR) continue;
        if (count <= 0) return false;
        offset += (size_t)count;
    }
    return true;
}
static unsigned channel_test_open_descriptors() {
    unsigned count = 0;
    for (int fd = 0; fd < 1024; fd++) if (fcntl(fd, F_GETFD) >= 0) count++;
    return count;
}

TEST_F(SecurityChannelTest, MissingOrInvalidChannelsCannotSelectAmbientAuthority) {
    seed("data", "private");
    int fd = open("/dev/null", O_RDONLY); ASSERT_GE(fd, 0);
    SecurityChannelConfig config = {1051, 1024 * 1024, 16384, 128, 1000};
    EXPECT_EQ(security_channel_create(fd, &config), nullptr);
    EXPECT_GE(fcntl(fd, F_GETFD), 0); close(fd);
    const int kinds[] = {SOCK_STREAM, SOCK_DGRAM};
    for (unsigned i = 0; i < sizeof(kinds) / sizeof(kinds[0]); i++) {
        int pair[2]; ASSERT_EQ(socketpair(AF_UNIX, kinds[i], 0, pair), 0);
        int flags = fcntl(pair[0], F_GETFL), descriptor_flags = fcntl(pair[0], F_GETFD);
        EXPECT_EQ(ipc_stream_is_connected_local(pair[0]), kinds[i] == SOCK_STREAM);
        EXPECT_EQ(fcntl(pair[0], F_GETFL), flags);
        EXPECT_EQ(fcntl(pair[0], F_GETFD), descriptor_flags);
        // Bootstrap inspection borrows the endpoint; subsequent IPC still owns it.
        EXPECT_EQ(write(pair[0], "x", 1), 1);
        char byte = 0; EXPECT_EQ(read(pair[1], &byte, 1), 1); EXPECT_EQ(byte, 'x');
        if (kinds[i] == SOCK_DGRAM) EXPECT_EQ(security_channel_create(pair[0], &config), nullptr);
        close(pair[0]); close(pair[1]);
    }
    fd = socket(AF_UNIX, SOCK_STREAM, 0); ASSERT_GE(fd, 0);
    EXPECT_FALSE(ipc_stream_is_connected_local(fd));
    EXPECT_EQ(security_channel_create(fd, &config), nullptr);
    EXPECT_GE(fcntl(fd, F_GETFD), 0); close(fd);
    SecurityIOClient unavailable = security_channel_client(nullptr);
    char* path = file_path_join(root, "data"); char* bytes = nullptr; size_t size = 0;
    EXPECT_FALSE(security_io_read_bytes(&unavailable, path, false, 4096, MEM_CAT_TEMP, &bytes, &size, nullptr));
    EXPECT_EQ(bytes, nullptr); EXPECT_EQ(size, 0U); mem_free(path);
}

TEST_F(SecurityChannelTest, EveryAcquisitionUsesTheBoundBroker) {
    seed("data", "payload"); seed("entry.ls", "42\n"); ASSERT_EQ(start(), 0);
    expect_read("data", "payload"); expect_read("entry.ls", "42\n", true);
    char* source = nullptr;
    ASSERT_TRUE(security_io_source_path(&remote, "entry.ls", &source, nullptr));
    char* expected = file_path_join(root, "entry.ls"); EXPECT_STREQ(source, expected);
    mem_free(source); mem_free(expected);
    int32_t kind = -1; ASSERT_TRUE(security_io_input_kind(&remote, "data", &kind, nullptr));
    EXPECT_EQ(kind, SECURITY_OBJECT_FILE);
    FileStat stat; ASSERT_TRUE(security_io_stat(&remote, "data", &stat, nullptr));
    EXPECT_TRUE(stat.exists); EXPECT_EQ(stat.size, 7);
    ASSERT_TRUE(security_io_stat(&remote, "missing", &stat, nullptr)); EXPECT_FALSE(stat.exists);
    SecurityDirectory directory = {};
    ASSERT_TRUE(security_io_enumerate(&remote, root, 32, &directory, nullptr));
    EXPECT_EQ(directory.count, 2U); security_io_directory_free(&directory);
    SecurityAuthorityStamp stamp;
    ASSERT_TRUE(security_io_authority_stamp(&remote, &stamp, nullptr)); EXPECT_EQ(stamp.session, 1051U);
    SecurityIOResult result = remote.write(remote.context, "new", "a\0b", 3, false, true);
    EXPECT_EQ(result.decision, SECURITY_ALLOWED); EXPECT_TRUE(result.committed); EXPECT_FALSE(result.outcome_unknown);
    result = remote.write(remote.context, "empty", "", 0, false, true); EXPECT_TRUE(result.committed);
    stop(); EXPECT_TRUE(server.success); EXPECT_EQ(server.report.requests, 10U);
}

TEST_F(SecurityChannelTest, ApprovalRemainsAHostOperationAndCannotBeReplayedByReopening) {
    seed("report", "old"); ASSERT_EQ(start(), 0);
    SecurityIOResult result = remote.write(remote.context, "report", "new", 3, false, true);
    ASSERT_EQ(result.decision, SECURITY_APPROVAL_REQUIRED); EXPECT_FALSE(result.committed);
    expect_text("report", "old");
    SecurityOverwriteChallenge challenge;
    ASSERT_TRUE(security_file_broker_describe(broker, result.approval, &challenge));
    EXPECT_TRUE(security_file_broker_approve(broker, result.approval).committed);
    expect_text("report", "new");
    EXPECT_EQ(remote.write(remote.context, "report", "x", 1, true, false).decision, SECURITY_APPROVAL_REQUIRED);
    stop(); EXPECT_TRUE(server.success);
}

TEST_F(SecurityChannelTest, ProtectedLinkedOutsideAndRevokedObjectsNeverFallBack) {
    seed("data", "private"); seed("protected/secret", "protected");
    char* link = file_path_join(root, "link"); ASSERT_EQ(symlink("data", link), 0); mem_free(link);
    ASSERT_EQ(start(), 0);
    const char* paths[] = {"../outside", "protected/secret", "link", "https://example.invalid/"};
    for (unsigned i = 0; i < sizeof(paths) / sizeof(paths[0]); i++) {
        const char* path = paths[i];
        char* bytes = nullptr; size_t size = 0; SecurityIOResult result;
        EXPECT_FALSE(security_io_read_bytes(&remote, path, false, 4096, MEM_CAT_TEMP, &bytes, &size, &result));
        EXPECT_EQ(bytes, nullptr); EXPECT_EQ(size, 0U);
    }
    expect_read("data", "private");
    ASSERT_TRUE(security_file_broker_revoke_root(broker, root_id));
    char* bytes = nullptr; size_t size = 0;
    EXPECT_FALSE(security_io_read_bytes(&remote, "data", false, 4096, MEM_CAT_TEMP, &bytes, &size, nullptr));
    stop(); EXPECT_TRUE(server.success);
}

struct ChannelReader { SecurityIOClient client; bool ok; };
static void* channel_reader(void* data) {
    ChannelReader* reader = (ChannelReader*)data; reader->ok = true;
    for (unsigned i = 0; i < 20; i++) {
        char* bytes = nullptr; size_t size = 0;
        bool ok = security_io_read_bytes(&reader->client, "data", false, 4096, MEM_CAT_TEMP, &bytes, &size, nullptr);
        reader->ok &= ok && size == 7 && !memcmp(bytes, "payload", 7); mem_free(bytes);
    }
    return nullptr;
}
TEST_F(SecurityChannelTest, ConcurrentTasksSerializeWholeTransactions) {
    seed("data", "payload"); ASSERT_EQ(start(), 0);
    pthread_t workers[4]; ChannelReader readers[4];
    for (unsigned i = 0; i < 4; i++) { readers[i] = {remote, false}; ASSERT_EQ(pthread_create(&workers[i], nullptr, channel_reader, &readers[i]), 0); }
    for (unsigned i = 0; i < 4; i++) { pthread_join(workers[i], nullptr); EXPECT_TRUE(readers[i].ok); }
    stop(); EXPECT_TRUE(server.success); EXPECT_EQ(server.report.requests, 80U);
}

TEST_F(SecurityChannelTest, InvalidEnvelopesCloseBeforeDispatch) {
    for (unsigned bad = 0; bad < 7; bad++) {
        int fd = start(true); ASSERT_GE(fd, 0); uint8_t bytes[32]; header(bytes, 7, 0);
        switch (bad) {
        case 0: bytes[0] = 'X'; break;
        case 1: write_le16(bytes + 4, 2); break;
        case 2: write_le16(bytes + 6, 8); break; // no approval/management opcode
        case 3: write_le64(bytes + 8, 2); break;
        case 4: write_le64(bytes + 16, 999); break;
        case 5: write_le32(bytes + 24, 16385); break;
        case 6: write_le32(bytes + 28, 1); break;
        }
        EXPECT_EQ(write(fd, bytes, sizeof(bytes)), (ssize_t)sizeof(bytes));
        close(fd); stop(); EXPECT_FALSE(server.success); EXPECT_EQ(server.report.requests, 0U);
    }
}

TEST_F(SecurityChannelTest, AReplayedRequestCannotExecuteAgain) {
    int fd = start(true); ASSERT_GE(fd, 0);
    uint8_t bytes[32]; header(bytes, 7, 0);
    ASSERT_EQ(write(fd, bytes, sizeof(bytes)), (ssize_t)sizeof(bytes));
    uint8_t response[80]; ASSERT_TRUE(channel_test_receive(fd, response, sizeof(response)));
    ASSERT_EQ(write(fd, bytes, sizeof(bytes)), (ssize_t)sizeof(bytes));
    close(fd); stop(); EXPECT_FALSE(server.success); EXPECT_EQ(server.report.requests, 1U);
    EXPECT_EQ(server.report.system_error, EPROTO);
}

TEST_F(SecurityChannelTest, RequestAndCumulativeByteBudgetsCloseWithoutFallback) {
    ASSERT_EQ(start(false, 1000, 1), 0);
    SecurityAuthorityStamp stamp;
    ASSERT_TRUE(security_io_authority_stamp(&remote, &stamp, nullptr));
    EXPECT_FALSE(security_io_authority_stamp(&remote, &stamp, nullptr));
    EXPECT_EQ(stamp.session, 0U); stop(); EXPECT_EQ(server.report.requests, 1U);
    ASSERT_EQ(start(false, 1000, 128, 128), 0);
    ASSERT_TRUE(security_io_authority_stamp(&remote, &stamp, nullptr));
    EXPECT_FALSE(security_io_authority_stamp(&remote, &stamp, nullptr));
    EXPECT_EQ(stamp.session, 0U); stop(); EXPECT_EQ(server.report.requests, 1U);
    EXPECT_LE(server.report.bytes, 128U);
}

TEST_F(SecurityChannelTest, AnOversizedLocalWriteDoesNotDispatchOrPoisonAValidRead) {
    seed("data", "payload"); ASSERT_EQ(start(), 0);
    char* bytes = (char*)mem_calloc(1, 20000, MEM_CAT_TEMP); ASSERT_NE(bytes, nullptr);
    SecurityIOResult result = remote.write(remote.context, "oversize", bytes, 20000, false, true);
    mem_free(bytes); EXPECT_NE(result.decision, SECURITY_ALLOWED); EXPECT_FALSE(result.outcome_unknown);
    expect_read("data", "payload"); stop(); EXPECT_EQ(server.report.requests, 1U);
    char* path = file_path_join(root, "oversize"); EXPECT_FALSE(file_exists(path)); mem_free(path);
}

TEST_F(SecurityChannelTest, NoMutationOccursWhenTheResultCannotFitTheRemainingBudget) {
    ASSERT_EQ(start(false, 1000, 128, 128), 0);
    char payload[70] = {};
    SecurityIOResult result = remote.write(remote.context, "new", payload, sizeof(payload), false, true);
    EXPECT_NE(result.decision, SECURITY_ALLOWED);
    stop(); EXPECT_FALSE(server.success); EXPECT_EQ(server.report.requests, 0U);
    char* path = file_path_join(root, "new"); EXPECT_FALSE(file_exists(path)); mem_free(path);
}

struct MalformedChannelPeer { int fd; unsigned mode; };
static void* malformed_channel_peer(void* data) {
    MalformedChannelPeer* peer = (MalformedChannelPeer*)data;
    uint8_t response[32 + 32 + 4 + 1] = {}, request[256];
    if (channel_test_receive(peer->fd, response, 32)) {
        uint32_t size = read_le32(response + 24);
        if (size <= sizeof(request) && channel_test_receive(peer->fd, request, size)) {
            uint32_t body = peer->mode == 1 ? 37 : 36;
            write_le32(response + 24, body); write_le32(response + 28, 1);
            if (peer->mode == 0) write_le64(response + 8, 99);
            if (peer->mode == 2) write_le32(response + 32, UINT32_MAX);
            if (peer->mode == 3) write_le32(response + 40, 1); // a read cannot claim a commit
            (void)write(peer->fd, response, 32 + body);
        }
    }
    close(peer->fd); return nullptr;
}
TEST_F(SecurityChannelTest, MalformedResponsesClearOutputsAndCloseTheChannel) {
    seed("data", "payload");
    for (unsigned mode = 0; mode < 4; mode++) {
        int pair[2]; ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
        MalformedChannelPeer peer = {pair[0], mode}; pthread_t peer_thread;
        ASSERT_EQ(pthread_create(&peer_thread, nullptr, malformed_channel_peer, &peer), 0);
        SecurityChannelConfig config = {1051, 1024 * 1024, 16384, 128, 1000};
        channel = security_channel_create(pair[1], &config); ASSERT_NE(channel, nullptr);
        remote = security_channel_client(channel);
        int32_t kind = 99;
        EXPECT_FALSE(security_io_input_kind(&remote, "data", &kind, nullptr)); EXPECT_EQ(kind, -1);
        EXPECT_FALSE(security_io_input_kind(&remote, "data", &kind, nullptr)); EXPECT_EQ(kind, -1);
        security_channel_destroy(channel); channel = nullptr;
        pthread_join(peer_thread, nullptr);
    }
}

TEST_F(SecurityChannelTest, MalformedMutationsDoNotReachTheBroker) {
    for (unsigned bad = 0; bad < 3; bad++) {
        int fd = start(true); ASSERT_GE(fd, 0);
        uint8_t bytes[32 + 4 + 3 + 4] = {}; header(bytes, 2, sizeof(bytes) - 32);
        write_le32(bytes + 32, 3); memcpy(bytes + 36, "new", 3);
        write_le32(bytes + 39, 0);
        if (bad == 0) bytes[37] = 0;
        if (bad == 1) write_le32(bytes + 39, 4);
        if (bad == 2) write_le32(bytes + 32, 4096);
        EXPECT_EQ(write(fd, bytes, sizeof(bytes)), (ssize_t)sizeof(bytes));
        close(fd); stop(); EXPECT_FALSE(server.success); EXPECT_EQ(server.report.requests, 0U);
        char* path = file_path_join(root, "new"); EXPECT_FALSE(file_exists(path)); mem_free(path);
    }
}

TEST_F(SecurityChannelTest, TruncatedFramesAndStalledPeersHaveFiniteDeadlines) {
    int fd = start(true, 30); ASSERT_GE(fd, 0); uint8_t bytes[32]; header(bytes, 7, 0);
    ASSERT_EQ(write(fd, bytes, 3), 3); close(fd); stop(); EXPECT_FALSE(server.success);
    fd = start(true, 30); ASSERT_GE(fd, 0);
    uint64_t begin = time_now_ms();
    pthread_join(thread, nullptr); running = false;
    EXPECT_FALSE(server.success); EXPECT_EQ(server.report.system_error, ETIMEDOUT);
    EXPECT_LT(time_now_ms() - begin, 1000U); close(fd);
}

TEST_F(SecurityChannelTest, DescriptorPassingIsRejectedAndTheReceivedDescriptorIsClosed) {
    int fd = start(true); ASSERT_GE(fd, 0);
    int other = open("/dev/null", O_RDONLY); ASSERT_GE(other, 0);
    unsigned before = channel_test_open_descriptors();
    uint8_t bytes[32]; header(bytes, 7, 0);
    iovec vector = {bytes, sizeof(bytes)};
    union { cmsghdr alignment; char bytes[CMSG_SPACE(sizeof(int))]; } control = {};
    msghdr message = {}; message.msg_iov = &vector; message.msg_iovlen = 1;
    message.msg_control = control.bytes; message.msg_controllen = sizeof(control.bytes);
    cmsghdr* h = CMSG_FIRSTHDR(&message); h->cmsg_level = SOL_SOCKET; h->cmsg_type = SCM_RIGHTS;
    h->cmsg_len = CMSG_LEN(sizeof(int)); memcpy(CMSG_DATA(h), &other, sizeof(other));
    ASSERT_EQ(sendmsg(fd, &message, 0), (ssize_t)sizeof(bytes));
    close(fd); stop(); EXPECT_FALSE(server.success); EXPECT_EQ(server.report.system_error, EPROTO);
    EXPECT_EQ(channel_test_open_descriptors(), before - 2); // both endpoints close; no received fd survives
    EXPECT_GE(fcntl(other, F_GETFD), 0); close(other);
}

struct LostWrite { SecurityIOClient client; int fd; };
static SecurityIOResult lose_write_response(void* data, const char* path, const void* bytes,
        size_t size, bool append, bool atomic) {
    LostWrite* lost = (LostWrite*)data;
    SecurityIOResult result = lost->client.write(lost->client.context, path, bytes, size, append, atomic);
    shutdown(lost->fd, SHUT_RDWR); return result;
}
TEST_F(SecurityChannelTest, LostWriteResponseHasAnUnknownOutcomeAndNeverRetries) {
    int pair[2]; ASSERT_EQ(socketpair(AF_UNIX, SOCK_STREAM, 0, pair), 0);
    LostWrite lost = {local, pair[0]};
    SecurityIOClient broken = local; broken.context = &lost; broken.write = lose_write_response;
    ASSERT_EQ(start_server(pair[0], {1051, 1024 * 1024, 16384, 128, 1000}, broken), 0);
    channel = security_channel_create(pair[1], &server.config); ASSERT_NE(channel, nullptr);
    remote = security_channel_client(channel);
    SecurityIOResult result = remote.write(remote.context, "new", "payload", 7, false, true);
    EXPECT_TRUE(result.decision != SECURITY_ALLOWED || result.system_error); EXPECT_TRUE(result.outcome_unknown);
    expect_text("new", "payload");
    result = remote.write(remote.context, "new", "second", 6, false, true);
    EXPECT_FALSE(result.outcome_unknown); expect_text("new", "payload"); stop(); EXPECT_FALSE(server.success);
}

// The signed-worker runner owns this helper's private endpoint and selects its
// consent fixture. This branch is test-only; there is no consent wire opcode.
static bool fixture_approve = false;
static uint64_t fixture_challenges = 0;
static uint64_t fixture_clock(void*) { return (uint64_t)time(nullptr); }
static SecurityIOResult fixture_write(void* context, const char* path, const void* bytes,
        size_t size, bool append, bool atomic) {
    SecurityFileBroker* broker = (SecurityFileBroker*)context;
    SecurityIOClient local = security_file_broker_client(broker);
    SecurityIOResult result = local.write(local.context, path, bytes, size, append, atomic);
    if (result.decision == SECURITY_APPROVAL_REQUIRED) {
        fixture_challenges++;
        if (fixture_approve) {
            SecurityOverwriteChallenge description;
            if (security_file_broker_describe(broker, result.approval, &description))
                result = security_file_broker_approve(broker, result.approval);
        }
    }
    return result;
}
static int serve_fixture(int argc, char** argv) {
    if (argc != 7) return 2;
    char* end = nullptr;
    long fd = strtol(argv[2], &end, 10); if (!end || *end || fd < 3 || fd > INT_MAX) return 2;
    unsigned long long session = strtoull(argv[3], &end, 10);
    if (!end || *end || !session || argv[3][0] == '-') return 2;
    if (strcmp(argv[6], "approve") && strcmp(argv[6], "deny")) return 2;
    fixture_approve = !strcmp(argv[6], "approve"); fixture_challenges = 0;
    SecurityFileBrokerConfig config = {{91, session, SECURITY_ALL_OPERATIONS, 256, 256},
        1024 * 1024, 16 * 1024 * 1024, 4 * 1024 * 1024, 16, 128, 60, fixture_clock, nullptr};
    SecurityFileBroker* broker = security_file_broker_create(&config);
    if (!broker) return 2;
    SecurityFileRootSpec root = {argv[4], SECURITY_SCOPE_PROJECT, SECURITY_ALL_OPERATIONS,
        0, 0, true, true, nullptr};
    if (!security_file_broker_protect(broker, argv[5], SECURITY_ALL_OPERATIONS) ||
            !security_file_broker_admit_root(broker, &root)) { security_file_broker_destroy(broker); return 2; }
    SecurityIOClient client = security_file_broker_client(broker); client.write = fixture_write;
    SecurityChannelConfig limits = {session, 64 * 1024 * 1024, 1024 * 1024, 8192, 10000};
    SecurityChannelReport report;
    bool success = security_channel_serve((int)fd, &limits, &client, &report);
    security_file_broker_destroy(broker);
    char output[256];
    int size = snprintf(output, sizeof(output),
        "{\"success\":%s,\"requests\":%llu,\"bytes\":%llu,\"error\":%d,\"challenges\":%llu}\n",
        success ? "true" : "false", (unsigned long long)report.requests,
        (unsigned long long)report.bytes, report.system_error, (unsigned long long)fixture_challenges);
    if (size <= 0 || (size_t)size >= sizeof(output) || write(STDOUT_FILENO, output, (size_t)size) != size) return 2;
    return success ? 0 : 3;
}
#endif

int main(int argc, char** argv) {
#ifndef _WIN32
    if (argc > 1 && !strcmp(argv[1], "--serve-fixture")) return serve_fixture(argc, argv);
#endif
    ::testing::InitGoogleTest(&argc, argv);
    return RUN_ALL_TESTS();
}
