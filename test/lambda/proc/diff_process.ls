import io

pn delayed() { io.exec("python3",["-c","import time; time.sleep(10)"])^ }
pn main() {
    let exact = io.exec("python3",["-c","import sys; sys.stdout.buffer.write(b'a\\x00b\\n\\n'); sys.stderr.write('err\\n'); sys.exit(7)"])^
    let large = io.exec("python3",["-c","import sys; sys.stdout.write('x'*100000); sys.stderr.write('y'*100000)"])^
    let limited = io.exec("python3",["-c","print('x'*10000)"],{max_output_bytes:32})^
    let timeout = io.exec("python3",["-c","import time; time.sleep(10)"],{timeout_ms:20})^
    let env = io.exec("python3",["-c","import os; print(os.environ['LAMBDA_DIFF_EXEC_TEST'])"],{env:{LAMBDA_DIFF_EXEC_TEST:"value"}})^
    let empty = io.exec("python3",["-c","pass"])^
    let raw = io.exec("python3",["-c","import os,sys; sys.stdout.buffer.write(os.fsencode(sys.argv[1]))",b'\xFF'])^
    let stubborn = io.exec("python3",["-c","import os,signal,time; signal.signal(signal.SIGTERM,signal.SIG_IGN); os.close(1); os.close(2); time.sleep(10)"],{timeout_ms:80})^
    let task = start(delayed)
    sleep(20)^
    cancel(task)
    var cancelled = null
    wait(task) ^ { cancelled = ^ } ~ { cancelled = ~ };
    [exact.stdout == b'\x6100620A0A',exact.stderr == binary("err\n"),exact.exit_code == 7,
     len(large.stdout) == 100000 and len(large.stderr) == 100000,
     limited.truncated and len(limited.stdout) == 32,timeout.timed_out,
     env.stdout == binary("value\n"),cancelled is error and cancelled.message == "task cancelled",
     empty.stdout == null and empty.stderr == null,raw.stdout == b'\xFF',stubborn.timed_out and stubborn.exit_code != 0]
}
