// Test sys.* path resolution - functional approach
// Returns a map with test results

{
    os_name: sys.os.name,
    os_platform: sys.os.platform,
    cpu_cores: sys.cpu.cores,
    cpu_threads: sys.cpu.threads,
    memory_total: sys.memory.total,
    proc_pid: sys.proc.self.pid,
    time_now: sys.time.now,
    lambda_version: sys.lambda.version,
    home: sys.home,
    temp: sys.temp
}
