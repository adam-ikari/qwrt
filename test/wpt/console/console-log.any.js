// META: global=shell

test(() => {
  assert_equals(typeof console.log, 'function');
  assert_equals(typeof console.error, 'function');
  assert_equals(typeof console.warn, 'function');
  assert_equals(typeof console.info, 'function');
  assert_equals(typeof console.debug, 'function');
}, 'console methods exist');

test(() => {
  console.log("test message");
  console.log("test", 123, true, null, undefined);
  console.log({key: "value"});
}, 'console.log accepts various argument types');
