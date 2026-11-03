#include "../../lib/confined_process.h"
#include "../../lib/process_sandbox.h"
#include "../../lambda/io/security_channel.h"
#include "../../lib/mem.h"
#include "../../lib/hex.h"
#include "../../lib/time_util.h"
#include "child_fixture.h"
#include "mach_fixture.hpp"
#include <arpa/inet.h>
#include <errno.h>
#include <fcntl.h>
#include <signal.h>
#include <spawn.h>
#include <stdio.h>
#include <stdlib.h>
#include <string.h>
#include <sys/mman.h>
#include <sys/socket.h>
#include <sys/wait.h>
#include <unistd.h>

static SecurityChannel* probe_channel(const char* session) {
    SecurityChannelConfig limits = {strtoull(session, nullptr, 10), 1048576, 8192, 8, 1000};
    return security_channel_create(3, &limits);
}

int main(int argc, char** argv) {
    if (argc == 3 && (!strcmp(argv[1], "mach-control") || !strcmp(argv[1], "mach-control-confined"))) {
        SafetyMachFixture ports;
        if (!ports.open()) return 2;
        SafetyChild child;
        char* mode = !strcmp(argv[1], "mach-control") ? (char*)"/mach-control.ls" : (char*)"/mach-ports.ls";
        char* args[] = {argv[2], (char*)"3", (char*)"1", mode, nullptr};
        char* env[] = {(char*)"PATH=/usr/bin:/bin", (char*)"LANG=en_US.UTF-8", nullptr};
        if (posix_spawn(&child.pid, argv[2], nullptr, nullptr, args, env)) { child.pid = -1; return 2; }
        int status = 0;
        if (child.reap(&status) || !WIFEXITED(status) || WEXITSTATUS(status)) return 2;
        char report[96];
        int size = snprintf(report, sizeof(report), "{\"messages\":%u,\"memory_changed\":%s}\n",
            ports.received(), ports.memory_changed() ? "true" : "false");
        return size > 0 && (size_t)size < sizeof(report) && write(1, report, size) == size ? 0 : 2;
    }
    if (argc == 6 && (!strcmp(argv[1], "monitor") || !strcmp(argv[1], "mach-monitor"))) {
        bool mach = !strcmp(argv[1], "mach-monitor");
        uint8_t digest[32];
        if (strlen(argv[3]) != 64 || !hex_decode(argv[3], 64, digest, nullptr)) return 2;
        ConfinedCodeSpec spec = {argv[2], "org.lambda-script.safety-worker", nullptr, digest};
        ConfinedCode* code = confined_code_open(&spec);
        SafetyMachFixture ports;
        if (mach && !ports.open()) { confined_code_close(code); return 2; }
        ConfinedProcess* child = code ? confined_process_spawn(code, argv[4], argv[5], 1) : nullptr;
        if (!child) { confined_code_close(code); return 2; }
        bool started = confined_process_resume(child);
        // Observe our sole child without reaping it. Its output fits both pipes,
        // guaranteeing pending chunks when the real monitor reaps the leader.
        bool observed = false; uint64_t deadline = time_now_ms() + 3000;
        while (started && time_now_ms() < deadline) {
            siginfo_t exited = {};
            if (!waitid(P_ALL, 0, &exited, WEXITED | WNOHANG | WNOWAIT) && exited.si_pid) { observed = true; break; }
            usleep(5000);
        }
        ConfinedProcessLimits limits = {5000, 3000, 536870912, 1048576};
        ConfinedProcessResult result = {};
        bool success = started && confined_process_wait(child, &limits, &result);
        char report[1024];
        int size = snprintf(report, sizeof(report),
            "{\"success\":%s,\"observed_exit\":%s,\"stop\":%d,\"error\":%d,\"output\":%zu,\"stderr\":%zu,\"exit\":%d,"
            "\"messages\":%u,\"memory_changed\":%s,\"mach\":%.*s}\n",
            success ? "true" : "false", observed ? "true" : "false", result.reason, result.system_error,
            result.output_size, result.error_size, result.exit_code, mach ? ports.received() : 0,
            mach && ports.memory_changed() ? "true" : "false",
            mach && result.output_size ? (int)result.output_size : 4,
            mach && result.output_size ? result.output : "null");
        confined_process_result_free(&result); confined_process_destroy(child); confined_code_close(code);
        return size > 0 && (size_t)size < sizeof(report) && write(1, report, size) == size ? 0 : 2;
    }
    if (argc != 4 || strcmp(argv[1], "3") || !confined_process_worker_limits()) return 2;
    const char* mode = strrchr(argv[3], '/'); mode = mode ? mode + 1 : argv[3];
    if (!strcmp(mode, "mach-ports.ls") || !strcmp(mode, "mach-control.ls"))
        return safety_mach_report(!strcmp(mode, "mach-ports.ls"));
    if (!strcmp(mode, "unconfined.ls")) {
        // A correctly signed fixture intentionally requests source before the
        // bootstrap. Host readback must deny it without returning source bytes.
        SecurityChannel* channel = probe_channel(argv[2]);
        if (!channel) return 2;
        SecurityIOClient client = security_channel_client(channel);
        char* bytes = nullptr; size_t size = 0;
        SecurityIOResult result = client.read(client.context, argv[3], true, 8192, MEM_CAT_SYSTEM, &bytes, &size);
        bool denied = result.decision == SECURITY_DENIED && result.system_error == EACCES && !bytes && !size;
        mem_free(bytes); security_channel_destroy(channel);
        return denied ? 0 : 2;
    }
    if (!process_sandbox_enter_offline()) return 2;
    if (!strcmp(mode, "temp-crash.ls") || !strcmp(mode, "temp-spin.ls")) {
        SecurityChannel* channel = probe_channel(argv[2]);
        if (!channel) return 2;
        SecurityIOClient client = security_channel_client(channel);
        if (!client.write(client.context, SECURITY_APP_TEMP_PATH "/unfinished", "scratch", 7, false, true).committed) return 2;
        if (!strcmp(mode, "temp-crash.ls")) __builtin_trap();
        volatile unsigned value = 0; while (true) value++;
    }
    if (!strcmp(mode, "drain.ls")) {
        // Leave multiple output chunks queued when the leader exits. Reaping
        // must happen once while the monitor continues to drain both pipes.
        char bytes[4096]; memset(bytes, 'x', sizeof(bytes)); bytes[sizeof(bytes) - 1] = '\n';
        for (unsigned i = 0; i < 3; i++)
            if (write(1, bytes, sizeof(bytes)) != sizeof(bytes) || write(2, bytes, sizeof(bytes)) != sizeof(bytes)) return 2;
        return 0;
    }
    if (!strcmp(mode, "wall.ls") || !strcmp(mode, "disconnect.ls")) {
        if (!strcmp(mode, "disconnect.ls")) close(3);
        sleep(10); return 0;
    }
    if (!strcmp(mode, "cpu.ls")) { volatile unsigned value = 0; while (true) value++; }
    if (!strcmp(mode, "memory.ls")) {
        size_t size = 128 * 1024 * 1024;
        volatile char* memory = (volatile char*)mmap(nullptr, size, PROT_READ | PROT_WRITE, MAP_ANON | MAP_PRIVATE, -1, 0);
        if (memory == MAP_FAILED) return 2;
        for (size_t i = 0; i < size; i += 4096) memory[i] = 1;
        sleep(10); return 0;
    }
    if (!strcmp(mode, "output.ls")) {
        char bytes[4096] = {};
        while (write(1, bytes, sizeof(bytes)) > 0) {}
        return 0;
    }
    if (!strcmp(mode, "forged.ls")) {
        char frame[32] = {}; (void)write(3, frame, sizeof(frame)); return 0;
    }
    unsigned unexpected = 0;
    for (int fd = 4; fd < 1024; fd++) if (fcntl(fd, F_GETFD) >= 0) unexpected++;
    int fd = open("/dev/tty", O_RDONLY | O_NONBLOCK); int tty_error = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    fd = open(argv[3], O_RDONLY); int read_error = fd < 0 ? errno : 0;
    if (fd >= 0) close(fd);
    int net_error = 0;
    fd = socket(AF_INET, SOCK_STREAM, 0);
    if (fd < 0) net_error = errno;
    else {
        sockaddr_in address = {}; address.sin_family = AF_INET; address.sin_port = htons(9);
        address.sin_addr.s_addr = htonl(INADDR_LOOPBACK);
        if (connect(fd, (sockaddr*)&address, sizeof(address))) net_error = errno;
        close(fd);
    }
    pid_t child = fork(); int fork_error = child < 0 ? errno : 0;
    if (!child) _exit(0);
    if (child > 0) waitpid(child, nullptr, 0);
    char json[384];
    const char* home = getenv("HOME");
    bool sandboxed = process_sandbox_is_applied(getpid());
    int length = snprintf(json, sizeof(json),
        "{\"unexpected_fds\":%u,\"fresh_session\":%s,\"clean_env\":%s,\"sandboxed\":%s,\"tty_errno\":%d,"
        "\"read_errno\":%d,\"network_errno\":%d,\"fork_errno\":%d}\n", unexpected,
        getsid(0) == getpid() ? "true" : "false",
        !getenv("SAFETY_SENTINEL") && !home && !getenv("LAMBDA_HOME") && !getenv("DYLD_LIBRARY_PATH") ? "true" : "false",
        sandboxed ? "true" : "false",
        tty_error, read_error, net_error, fork_error);
    if (length <= 0 || (size_t)length >= sizeof(json)) return 2;
    return write(1, json, length) == length ? 0 : 2;
}
