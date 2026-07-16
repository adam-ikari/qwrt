/*
 * qwrt WPT shell report — uses add_result_callback (fires per-test, does
 * NOT depend on tests.all_done() which requires async ShellTestEnvironment
 * completion). add_completion_callback may never fire if all_done conditions
 * aren't met in the QuickJS shell. Per-test results are more reliable.
 */
add_result_callback(function(test) {
    var st = "SKIP";
    if (test.status === 0) st = "PASS";
    else if (test.status === 1) st = "FAIL";
    else if (test.status === 2) st = "TIMEOUT";
    else if (test.status === 3) st = "NOTRUN";
    print(st + " | " + test.name + " | " + (test.message || ""));
});
