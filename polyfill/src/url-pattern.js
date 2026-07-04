/**
 * qwrt polyfill: URLPattern
 *
 * TC55/ECMA-429 requires URLPattern for URL matching.
 *
 * Implements the URLPattern API with pattern syntax:
 *   - :name — named capture group (matches /[^/]+/)
 *   - :name? — optional capture
 *   - :name+ — one or more segments
 *   - :name* — zero or more segments
 *   - {group} — regex group
 *   - * — wildcard (matches any string)
 *
 * Pure JS - no PAL primitives needed.
 */

export function setupURLPattern() {

  /**
   * URLPattern
   *
   * Matches URLs against a pattern with named groups.
   */
  class URLPattern {
    constructor(input, baseURL) {
      var pattern;
      if (typeof input === 'string') {
        pattern = { pathname: input || '*' };
      } else {
        pattern = input || {};
      }

      this._protocol = compilePattern(pattern.protocol || '*');
      this._username = compilePattern(pattern.username || '*');
      this._password = compilePattern(pattern.password || '*');
      this._hostname = compilePattern(pattern.hostname || '*');
      this._port = compilePattern(pattern.port || '*');
      this._pathname = compilePattern(pattern.pathname || '*');
      this._search = compilePattern(pattern.search || '*');
      this._hash = compilePattern(pattern.hash || '*');
      this._baseURL = baseURL || pattern.baseURL || null;

      // Compile regex patterns
      this._protocolRegex = buildRegex(this._protocol);
      this._usernameRegex = buildRegex(this._username);
      this._passwordRegex = buildRegex(this._password);
      this._hostnameRegex = buildRegex(this._hostname);
      this._portRegex = buildRegex(this._port);
      this._pathnameRegex = buildRegex(this._pathname);
      this._searchRegex = buildRegex(this._search);
      this._hashRegex = buildRegex(this._hash);
    }

    get protocol() { return this._protocol.pattern; }
    get username() { return this._username.pattern; }
    get password() { return this._password.pattern; }
    get hostname() { return this._hostname.pattern; }
    get port() { return this._port.pattern; }
    get pathname() { return this._pathname.pattern; }
    get search() { return this._search.pattern; }
    get hash() { return this._hash.pattern; }

    test(input, baseURL) {
      return this.exec(input, baseURL) !== null;
    }

    exec(input, baseURL) {
      var url;
      if (typeof input === 'string') {
        try {
          url = parseURL(input, baseURL || this._baseURL);
        } catch (e) {
          return null;
        }
      } else {
        url = input;
      }

      var protocolResult = matchPattern(this._protocol, this._protocolRegex, url.protocol || '');
      var usernameResult = matchPattern(this._username, this._usernameRegex, url.username || '');
      var passwordResult = matchPattern(this._password, this._passwordRegex, url.password || '');
      var hostnameResult = matchPattern(this._hostname, this._hostnameRegex, url.hostname || '');
      var portResult = matchPattern(this._port, this._portRegex, url.port || '');
      var pathnameResult = matchPattern(this._pathname, this._pathnameRegex, url.pathname || '');
      var searchResult = matchPattern(this._search, this._searchRegex, url.search || '');
      var hashResult = matchPattern(this._hash, this._hashRegex, url.hash || '');

      if (!protocolResult || !usernameResult || !passwordResult ||
          !hostnameResult || !portResult || !pathnameResult ||
          !searchResult || !hashResult) {
        return null;
      }

      return {
        inputs: [input],
        protocol: protocolResult,
        username: usernameResult,
        password: passwordResult,
        hostname: hostnameResult,
        port: portResult,
        pathname: pathnameResult,
        search: searchResult,
        hash: hashResult,
      };
    }
  }

  /**
   * Compile a URLPattern pattern string into tokens.
   *
   * Converts the URLPattern syntax into a regex pattern string
   * and extracts named group information.
   */
  function compilePattern(patternStr) {
    if (patternStr === '*') {
      return {
        pattern: patternStr,
        regexStr: '(.*)',
        names: ['*'],
      };
    }

    var regexStr = '';
    var names = [];
    var i = 0;

    while (i < patternStr.length) {
      var ch = patternStr[i];

      if (ch === ':') {
        // Named capture
        i++;
        var name = '';
        while (i < patternStr.length && /[a-zA-Z0-9_]/.test(patternStr[i])) {
          name += patternStr[i];
          i++;
        }

        // Check for modifier
        var modifier = '';
        if (i < patternStr.length && (patternStr[i] === '?' || patternStr[i] === '+' || patternStr[i] === '*')) {
          modifier = patternStr[i];
          i++;
        }

        names.push(name);

        if (modifier === '?') {
          regexStr += '(?:([^/]*))?';
        } else if (modifier === '+') {
          regexStr += '(:(.+))';
        } else if (modifier === '*') {
          regexStr += '(:(.*))';
        } else {
          regexStr += '([^/]*)';
        }
      } else if (ch === '{') {
        // Regex group
        i++;
        var groupContent = '';
        var depth = 1;
        while (i < patternStr.length && depth > 0) {
          if (patternStr[i] === '{') depth++;
          if (patternStr[i] === '}') depth--;
          if (depth > 0) groupContent += patternStr[i];
          i++;
        }
        regexStr += '(' + groupContent + ')';
        names.push(groupContent);
      } else if (ch === '*') {
        // Wildcard
        regexStr += '(.*?)';
        names.push('*');
        i++;
      } else {
        // Literal character — escape regex special chars
        if (/[\\^$.|?+(){}[\]]/.test(ch)) {
          regexStr += '\\' + ch;
        } else {
          regexStr += ch;
        }
        i++;
      }
    }

    return {
      pattern: patternStr,
      regexStr: '^' + regexStr + '$',
      names: names,
    };
  }

  function buildRegex(compiled) {
    try {
      return new RegExp(compiled.regexStr, 'i');
    } catch (e) {
      return null;
    }
  }

  function matchPattern(compiled, regex, value) {
    if (!regex) return null;

    var match = value.match(regex);
    if (!match) return null;

    var groups = {};
    var input = match[0];

    // Named groups from capture groups
    for (var i = 0; i < compiled.names.length; i++) {
      var name = compiled.names[i];
      var value = match[i + 1];
      if (name && name !== '*') {
        groups[name] = value !== undefined ? value : '';
      }
    }

    return {
      input: input,
      groups: groups,
    };
  }

  /**
   * Simple URL parser (no dependency on URL class).
   */
  function parseURL(url, base) {
    var str = String(url);
    var result = {
      protocol: '',
      username: '',
      password: '',
      hostname: '',
      port: '',
      pathname: '/',
      search: '',
      hash: '',
    };

    var match = str.match(/^(?:([a-z][a-z0-9+.-]*):)?(?:\/\/(?:([^:@]*)(?::([^@]*))?@)?([^:/?#]*)(?::(\d+))?)?(\/?[^?#]*)?(?:\?([^#]*))?(?:#(.*))?$/i);
    if (!match) throw new TypeError('Invalid URL: ' + str);

    result.protocol = match[1] || '';
    result.username = match[2] || '';
    result.password = match[3] || '';
    result.hostname = match[4] || '';
    result.port = match[5] || '';
    result.pathname = match[6] || '/';
    result.search = match[7] || '';
    result.hash = match[8] || '';

    return result;
  }

  globalThis.URLPattern = URLPattern;
}
