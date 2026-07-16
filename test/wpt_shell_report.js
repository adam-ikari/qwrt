/*
 * qwrt WPT shell report — uses add_result_callback (fires per test)
 * instead of add_completion_callback (which depends on all_done() and
 * ShellTestEnvironment's async all_loaded). Per-test results are more
 * reliable in the QuickJS shell environment.
 */
add_result_callback(function(test) {
    var st = "SKIP";
    if (test.status === 0) st = "PASS";
    else if (test.status === 1) st = "FAIL";
    else if (test.status === 2) st = "TIMEOUT";
    else if (test.status === 3) st = "NOTRUN";
    print(st + " | " + test.name + " | " + (test.message || ""));
});