/*
 * qwrt WPT shell report — registered as add_result_callback so each
 * test's result is printed immediately when the test completes (does not
 * depend on tests.all_done() or the async ShellTestEnvironment completion).
 *
 * Additionally register add_completion_callback to print a SUMMARY line
 * after all tests are done, so the runner can tally results even if some
 * callbacks were never triggered.
 */
add_result_callback(function(test) {
    var st = "SKIP";
    if (test.status === 0) st = "PASS";
    else if (test.status === 1) st = "FAIL";
    else if (test.status === 2) st = "TIMEOUT";
    else if (test.status === 3) st = "NOTRUN";
    print(st + " | " + test.name + " | " + (test.message || ""));
});

