/*
 * qwrt WPT shell report — completion callback that writes structured results
 * to the `print` global (qwrt runner provides it). The harness's default
 * Output (DOM-based) is never created in the ShellTestEnvironment path, so
 * this is the sole result output.
 *
 * Format per test:  STATUS | NAME | MESSAGE
 *   STATUS: PASS (0), FAIL (1), TIMEOUT (2), NOTRUN (3)
 * Summary line:  SUMMARY:TOTAL_COUNT:HARNESS_STATUS
 *   HARNESS_STATUS: 0=OK, 1=ERROR, 2=TIMEOUT, 3=PRECONDITION_FAILED
 */
add_completion_callback(function(tests, harness_status, asserts) {
    var lines = [];
    for (var i = 0; i < tests.length; i++) {
        var t = tests[i];
        var st = "SKIP";
        if (t.status === 0) st = "PASS";
        else if (t.status === 1) st = "FAIL";
        else if (t.status === 2) st = "TIMEOUT";
        else if (t.status === 3) st = "NOTRUN";
        lines.push(st + " | " + t.name + " | " + (t.message || ""));
    }
    lines.push("SUMMARY:" + tests.length + ":" + harness_status.status);
    print(lines.join("\n"));
});
