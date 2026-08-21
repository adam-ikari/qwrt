/**
 * qwrt polyfill: CacheStorage / Cache
 *
 * Simplified WHATWG Cache API: in-memory Map-based implementation.
 * caches.open → Cache, put/match/delete keys by URL string.
 *
 * Pure JS — no PAL primitives needed.
 * Requires: Request, Response (fetch.js).
 */

export function setupCacheStorage() {
  function urlKey(request) {
    if (typeof request === 'string') return request;
    if (request instanceof Request) return request.url;
    return String(request);
  }

  class Cache {
    constructor(name) {
      this._name = name;
      this._map = new Map();
    }

    put(request, response) {
      if (!(response instanceof Response)) {
        return Promise.reject(new TypeError('Cache.put: response must be a Response'));
      }
      var key = urlKey(request);
      this._map.set(key, response);
      return Promise.resolve();
    }

    match(request) {
      var key = urlKey(request);
      var r = this._map.get(key);
      return Promise.resolve(r ? r.clone() : undefined);
    }

    matchAll(request) {
      var key = request ? urlKey(request) : null;
      var results = [];
      this._map.forEach(function(v, k) {
        if (!key || k === key) results.push(v.clone());
      });
      return Promise.resolve(results);
    }

    delete(request) {
      var key = urlKey(request);
      return Promise.resolve(this._map.delete(key));
    }

    keys(request) {
      var key = request ? urlKey(request) : null;
      var results = [];
      this._map.forEach(function(v, k) {
        if (!key || k === key) results.push(k);
      });
      return Promise.resolve(results);
    }
  }

  class CacheStorage {
    constructor() {
      this._caches = new Map();
    }

    open(name) {
      if (!this._caches.has(name)) {
        this._caches.set(name, new Cache(name));
      }
      return Promise.resolve(this._caches.get(name));
    }

    has(name) {
      return Promise.resolve(this._caches.has(name));
    }

    delete(name) {
      return Promise.resolve(this._caches.delete(name));
    }

    keys() {
      return Promise.resolve(Array.from(this._caches.keys()));
    }
  }

  globalThis.Cache = Cache;
  globalThis.CacheStorage = CacheStorage;
  globalThis.caches = new CacheStorage();
}