/**
 * qwrt polyfill: URL and URLSearchParams
 *
 * WHATWG URL Standard implementation.
 * Pure JS - no PAL primitives needed.
 *
 * This is a simplified implementation covering the most common use cases.
 * For full spec compliance, consider using a library like whatwg-url.
 */

export function setupURL() {
  /**
   * URLSearchParams class
   *
   * Utility for working with query strings.
   */
  class URLSearchParams {
    constructor(init) {
      this._params = [];

      if (init === undefined || init === null) {
        // Empty
      } else if (typeof init === 'string') {
        // Parse from string
        this._parse(init);
      } else if (init instanceof URLSearchParams) {
        // Copy from another URLSearchParams
        this._params = init._params.slice();
      } else if (typeof init === 'object') {
        // From object or sequence
        if (Symbol.iterator in init) {
          // Sequence of [key, value] pairs
          for (const pair of init) {
            if (Array.isArray(pair) && pair.length >= 2) {
              this._params.push([String(pair[0]), String(pair[1])]);
            }
          }
        } else {
          // Object with key-value pairs
          for (const key of Object.keys(init)) {
            this._params.push([key, String(init[key])]);
          }
        }
      }
    }

    _parse(str) {
      // Remove leading '?' if present
      if (str[0] === '?') {
        str = str.slice(1);
      }

      if (!str) return;

      const pairs = str.split('&');
      for (const pair of pairs) {
        if (!pair) continue;

        const eqIdx = pair.indexOf('=');
        if (eqIdx < 0) {
          // No value
          this._params.push([this._decode(pair), '']);
        } else {
          this._params.push([
            this._decode(pair.slice(0, eqIdx)),
            this._decode(pair.slice(eqIdx + 1))
          ]);
        }
      }
    }

    _decode(str) {
      // Decode percent-encoded string
      try {
        return decodeURIComponent(str.replace(/\+/g, ' '));
      } catch (e) {
        return str;
      }
    }

    _encode(str) {
      // Encode string for URL
      return encodeURIComponent(str)
        .replace(/%20/g, '+')
        .replace(/[!'()*]/g, function(c) {
          return '%' + c.charCodeAt(0).toString(16).toUpperCase();
        });
    }

    append(name, value) {
      this._params.push([String(name), String(value)]);
    }

    delete(name) {
      name = String(name);
      this._params = this._params.filter(function(p) {
        return p[0] !== name;
      });
    }

    get(name) {
      name = String(name);
      for (const p of this._params) {
        if (p[0] === name) return p[1];
      }
      return null;
    }

    getAll(name) {
      name = String(name);
      const result = [];
      for (const p of this._params) {
        if (p[0] === name) result.push(p[1]);
      }
      return result;
    }

    has(name) {
      name = String(name);
      for (const p of this._params) {
        if (p[0] === name) return true;
      }
      return false;
    }

    set(name, value) {
      name = String(name);
      value = String(value);

      let found = false;
      const result = [];

      for (const p of this._params) {
        if (p[0] === name) {
          if (!found) {
            result.push([name, value]);
            found = true;
          }
          // Skip duplicates
        } else {
          result.push(p);
        }
      }

      if (!found) {
        result.push([name, value]);
      }

      this._params = result;
    }

    sort() {
      this._params.sort(function(a, b) {
        return a[0].localeCompare(b[0]);
      });
    }

    toString() {
      const parts = [];
      for (const p of this._params) {
        parts.push(this._encode(p[0]) + '=' + this._encode(p[1]));
      }
      return parts.join('&');
    }

    forEach(callback, thisArg) {
      for (const p of this._params) {
        callback.call(thisArg, p[1], p[0], this);
      }
    }

    entries() {
      return this._params[Symbol.iterator]();
    }

    keys() {
      const params = this._params;
      let i = 0;
      return {
        next: function() {
          if (i < params.length) {
            return { value: params[i++][0], done: false };
          }
          return { done: true };
        },
        [Symbol.iterator]: function() { return this; }
      };
    }

    values() {
      const params = this._params;
      let i = 0;
      return {
        next: function() {
          if (i < params.length) {
            return { value: params[i++][1], done: false };
          }
          return { done: true };
        },
        [Symbol.iterator]: function() { return this; }
      };
    }

    [Symbol.iterator]() {
      return this.entries();
    }
  }

  /**
   * URL class
   *
   * WHATWG URL implementation.
   */
  class URL {
    constructor(url, base) {
      // Parse base if provided
      let baseUrl = null;
      if (base) {
        baseUrl = base instanceof URL ? base : new URL(base);
      }

      // Parse the URL
      this._parse(url, baseUrl);
    }

    _parse(url, baseUrl) {
      if (typeof url !== 'string') {
        throw new TypeError('URL must be a string');
      }

      url = url.trim();

      // Regex for URL parsing
      // protocol://[user:pass@]host[:port]/path[?query][#hash]
      const URL_REGEX = /^(?:([a-z][a-z0-9+.-]*):)?(?:\/\/(?:([^:@]*)(?::([^@]*))?@)?([^:/?#]*)(?::(\d+))?)?(\/?[^?#]*)?(?:\?([^#]*))?(?:#(.*))?$/i;

      let match = url.match(URL_REGEX);

      if (!match) {
        throw new TypeError('Invalid URL: ' + url);
      }

      let [, protocol, username, password, host, port, path, query, hash] = match;

      // If no protocol and no base, it's a relative URL without base
      if (!protocol && !baseUrl) {
        throw new TypeError('Relative URL without base: ' + url);
      }

      // If no protocol, inherit from base
      if (!protocol && baseUrl) {
        protocol = baseUrl._protocol;

        if (!host) {
          // Relative path
          host = baseUrl._host;
          port = baseUrl._port;
          username = baseUrl._username;
          password = baseUrl._password;

          if (path && path[0] !== '/') {
            // Relative path - resolve against base
            const basePath = baseUrl._pathname || '/';
            const baseDir = basePath.substring(0, basePath.lastIndexOf('/') + 1);
            path = this._resolvePath(baseDir + path);
          } else if (!path) {
            path = baseUrl._pathname;
          }
        }
      }

      // Store parsed components
      this._protocol = (protocol || '').toLowerCase();
      this._username = username || '';
      this._password = password || '';
      this._host = host || '';
      this._port = port || '';
      this._pathname = path || '/';
      this._search = query || '';
      this._hash = hash || '';

      // Create searchParams
      this._searchParams = new URLSearchParams(this._search);
    }

    _resolvePath(path) {
      // Normalize path (resolve . and ..)
      const parts = path.split('/');
      const result = [];

      for (const part of parts) {
        if (part === '..') {
          if (result.length > 0 && result[result.length - 1] !== '..') {
            result.pop();
          }
        } else if (part !== '.' && part !== '') {
          result.push(part);
        }
      }

      let resolved = result.join('/');
      if (path[0] === '/') resolved = '/' + resolved;
      if (path[path.length - 1] === '/' && resolved[resolved.length - 1] !== '/') {
        resolved += '/';
      }

      return resolved || '/';
    }

    // Getters
    get href() {
      return this.toString();
    }

    get origin() {
      if (!this._host) return '';
      return this._protocol + '//' + this._host + (this._port ? ':' + this._port : '');
    }

    get protocol() { return this._protocol + ':'; }
    get username() { return this._username; }
    get password() { return this._password; }
    get host() {
      return this._host + (this._port ? ':' + this._port : '');
    }
    get hostname() { return this._host; }
    get port() { return this._port; }
    get pathname() { return this._pathname; }
    get search() { return this._search ? '?' + this._search : ''; }
    get searchParams() { return this._searchParams; }
    get hash() { return this._hash ? '#' + this._hash : ''; }

    // Setters
    set protocol(v) {
      v = String(v).toLowerCase();
      if (v.endsWith(':')) v = v.slice(0, -1);
      this._protocol = v;
    }

    set username(v) {
      this._username = String(v);
    }

    set password(v) {
      this._password = String(v);
    }

    set host(v) {
      v = String(v);
      const colonIdx = v.lastIndexOf(':');
      if (colonIdx >= 0) {
        this._host = v.slice(0, colonIdx);
        this._port = v.slice(colonIdx + 1);
      } else {
        this._host = v;
        this._port = '';
      }
    }

    set hostname(v) {
      this._host = String(v);
    }

    set port(v) {
      this._port = String(v);
    }

    set pathname(v) {
      v = String(v);
      this._pathname = v[0] === '/' ? v : '/' + v;
    }

    set search(v) {
      v = String(v);
      this._search = v[0] === '?' ? v.slice(1) : v;
      this._searchParams = new URLSearchParams(this._search);
    }

    set hash(v) {
      v = String(v);
      this._hash = v[0] === '#' ? v.slice(1) : v;
    }

    set href(v) {
      this._parse(v, null);
    }

    toString() {
      let result = this._protocol + ':';

      if (this._host) {
        result += '//';
        if (this._username) {
          result += this._username;
          if (this._password) {
            result += ':' + this._password;
          }
          result += '@';
        }
        result += this._host;
        if (this._port) {
          result += ':' + this._port;
        }
      }

      result += this._pathname;

      if (this._search) {
        result += '?' + this._search;
      }

      if (this._hash) {
        result += '#' + this._hash;
      }

      return result;
    }

    toJSON() {
      return this.toString();
    }

    // Static method to check if a string is a valid URL
    static canParse(url, base) {
      try {
        new URL(url, base);
        return true;
      } catch (e) {
        return false;
      }
    }
  }

  // Register on globalThis
  globalThis.URL = URL;
  globalThis.URLSearchParams = URLSearchParams;
}
