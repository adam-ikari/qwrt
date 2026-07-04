/**
 * qwrt Polyfill - Key-Value Storage Extension API
 *
 * Provides a simple async key-value storage API based on PAL primitives.
 * PAL storageGet returns null when a key is not found (status=-2).
 *
 * Mounted on globalThis.storage
 */

export function setupStorage(pal) {
  var storage = {
    async get(key) {
      var value = await pal.storageGet(key);
      return value;  // null if not found
    },

    async set(key, value) {
      await pal.storageSet(key, String(value));
    },

    async delete(key) {
      await pal.storageDel(key);
    },
  };

  globalThis.storage = storage;
}
