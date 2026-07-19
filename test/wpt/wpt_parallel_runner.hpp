#pragma once

// Shared bounded worker queue for WPT runners whose cases execute isolated
// lambda.exe children. Keeping the queue here prevents each conformance runner
// from growing its own subtly different thread scheduler.

#include <algorithm>
#include <atomic>
#include <cstdlib>
#include <cstring>
#include <mutex>
#include <string>
#include <thread>
#include <vector>

inline int wpt_parallel_jobs(const char* suite_env, const char* legacy_env) {
    const char* value = suite_env && suite_env[0] ? getenv(suite_env) : NULL;
    if ((!value || !value[0]) && legacy_env && legacy_env[0]) value = getenv(legacy_env);
    if (!value || !value[0]) value = getenv("LAMBDA_WPT_JOBS");
    if (value && value[0]) {
        int jobs = atoi(value);
        if (jobs > 0) return jobs;
    }

    unsigned int cpus = std::thread::hardware_concurrency();
    if (cpus <= 1) return 1;
    return (int)cpus - 1;
}

inline bool wpt_arg_starts_with(const char* text, const char* prefix) {
    return strncmp(text, prefix, strlen(prefix)) == 0;
}

inline bool wpt_has_filtered_gtest_arg(int argc, char** argv,
                                       const char* whole_suite_filter) {
    for (int i = 1; i < argc; i++) {
        if (strcmp(argv[i], "--gtest_list_tests") == 0) return true;
        if (wpt_arg_starts_with(argv[i], "--gtest_filter=")) {
            const char* filter = argv[i] + strlen("--gtest_filter=");
            if (strcmp(filter, "*") != 0 && strcmp(filter, whole_suite_filter) != 0)
                return true;
        }
    }
    return false;
}

inline std::string wpt_gtest_json_path(int argc, char** argv) {
    for (int i = 1; i < argc; i++) {
        const char* prefix = "--gtest_output=json:";
        if (wpt_arg_starts_with(argv[i], prefix)) {
            return std::string(argv[i] + strlen(prefix));
        }
    }
    return "";
}

template <typename Param, typename Result, typename RunCase,
          typename CaseStarted, typename CaseFinished>
static void wpt_run_cases_parallel(const std::vector<Param>& params,
                                   std::vector<Result>& results,
                                   int jobs,
                                   RunCase run_case,
                                   CaseStarted case_started,
                                   CaseFinished case_finished) {
    results.resize(params.size());
    if (params.empty()) return;

    int worker_count = std::max(1, std::min(jobs, (int)params.size()));
    std::atomic<size_t> next_index(0);
    std::mutex output_mutex;
    std::vector<std::thread> workers;
    workers.reserve(worker_count);

    for (int worker_index = 0; worker_index < worker_count; worker_index++) {
        workers.emplace_back([&]() {
            while (true) {
                size_t index = next_index.fetch_add(1);
                if (index >= params.size()) break;

                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    case_started(index, params[index]);
                }

                Result result = run_case(params[index]);
                results[index] = result;

                {
                    std::lock_guard<std::mutex> lock(output_mutex);
                    case_finished(index, params[index], result);
                }
            }
        });
    }

    for (auto& worker : workers) worker.join();
}
