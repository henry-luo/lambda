// Run the real scheduler with a short watchdog in a separate process.
import utf8: lambda.io.utf8
pn main() {
    let tested = io.exec("./lambda.exe", ["run", "test/lambda/conc/mod_root_long_wait.ls", "--no-log"],
        {env:{LAMBDA_TEST_ROOT_WAIT_WATCHDOG_MS:"100"}})^
    if (tested.exit_code != 0) raise error(utf8.decode(tested.stderr)^)
    let output = utf8.decode(tested.stdout)^
    return [contains(output,"timer passed"), contains(output,"process passed")]
}
