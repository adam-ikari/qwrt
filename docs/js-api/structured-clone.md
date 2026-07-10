# structuredClone

Deep cloning of JavaScript values using the structured clone algorithm. Handles circular references, typed arrays, Date, Map, Set, ArrayBuffer, and more.

## Global

| Global | Description |
|--------|-------------|
| `structuredClone(value, options?)` | Deep clone any structured-cloneable value |

## Basic Usage

```js
let original = { name: 'Alice', scores: [95, 87, 92] };
let cloned = structuredClone(original);

cloned.scores[0] = 100;
console.log(original.scores[0]); // 95 — unchanged
```

## Supported Types

### Primitives

```js
structuredClone(42);           // 42
structuredClone('hello');      // "hello"
structuredClone(true);         // true
structuredClone(null);         // null
structuredClone(undefined);    // undefined
structuredClone(123n);         // 123n (BigInt)
```

### Objects and Arrays

```js
structuredClone({ a: 1, b: { c: 2 } });
structuredClone([1, 2, [3, 4]]);
structuredClone(new Date());
```

### Keyed Collections

```js
structuredClone(new Map([['key', 'value']]));
structuredClone(new Set([1, 2, 3]));
```

### Binary Data

```js
structuredClone(new ArrayBuffer(8));
structuredClone(new Uint8Array([1, 2, 3]));
structuredClone(new Int32Array([100, 200]));
structuredClone(new DataView(new ArrayBuffer(4)));

// All typed arrays: Int8Array, Uint8Array, Uint8ClampedArray,
// Int16Array, Uint16Array, Int32Array, Uint32Array,
// Float32Array, Float64Array, BigInt64Array, BigUint64Array
```

### Special Values

```js
structuredClone(new RegExp('pattern', 'gi'));
structuredClone(new Error('Something failed'));
// Error subclasses: TypeError, RangeError, SyntaxError, ReferenceError, EvalError, URIError
```

### Circular References

```js
let obj = { name: 'circle' };
obj.self = obj;  // circular

let cloned = structuredClone(obj);
console.log(cloned.self === cloned); // true
```

### Complex Example

```js
let original = {
    id: 42,
    name: 'Document',
    tags: new Set(['important', 'draft']),
    metadata: new Map([['author', 'Alice'], ['version', 2]]),
    created: new Date(),
    data: new Uint8Array([0xDE, 0xAD, 0xBE, 0xEF]),
    children: [
        { id: 1, parent: null }  // will be resolved for circular refs
    ]
};
original.children[0].parent = original;  // circular

let cloned = structuredClone(original);
console.log(cloned.children[0].parent === cloned); // true
```

## Unsupported Types

These throw `DataCloneError`:

```js
structuredClone(() => {});           // Functions
structuredClone(Symbol('test'));     // Symbols
structuredClone(new WeakMap());      // WeakMap
structuredClone(new WeakSet());      // WeakSet
structuredClone(new Promise(()=>{}));// Promises
structuredClone(document);           // DOM nodes (not available anyway)
```

## transfer Option

Transfer ownership of `ArrayBuffer` objects (the original becomes detached):

```js
let buffer = new ArrayBuffer(1024);
let cloned = structuredClone({ data: buffer }, {
    transfer: [buffer]
});

console.log(buffer.byteLength);  // 0 — transferred
console.log(cloned.data.byteLength);  // 1024
```

## Custom Error Cloning

`Error` objects are cloned with their properties:

```js
let err = new TypeError('Invalid value');
err.code = 'ERR_INVALID';
err.status = 400;

let cloned = structuredClone(err);
console.log(cloned.name);     // "TypeError"
console.log(cloned.message);  // "Invalid value"
console.log(cloned.code);     // "ERR_INVALID"
console.log(cloned.status);   // 400
console.log(cloned.stack);    // stack trace is preserved
```

## Notes

- `structuredClone` is a polyfill implementation — not the browser's native algorithm
- All typed array variants are supported
- `RegExp` flags (`g`, `i`, `m`, `s`, `u`, `y`) are preserved
- `RegExp.lastIndex` is reset to 0 in the clone
- `Date` timezone offset is preserved (millisecond precision)
- No `Blob` or `File` cloning support (these types are limited in qwrt)
