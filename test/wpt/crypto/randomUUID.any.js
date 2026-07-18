// META: global=shell

test(() => {
  assert_equals(typeof crypto.randomUUID, 'function');
}, 'crypto.randomUUID exists');

test(() => {
  const uuid = crypto.randomUUID();
  assert_equals(typeof uuid, 'string');
  assert_regexp_match(uuid, /^[0-9a-f]{8}-[0-9a-f]{4}-4[0-9a-f]{3}-[89ab][0-9a-f]{3}-[0-9a-f]{12}$/);
}, 'crypto.randomUUID returns valid UUID v4');
