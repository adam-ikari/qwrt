/**
 * qwrt polyfill: performance
 *
 * Implements performance.now() using pal.hrtime() (nanosecond precision)
 * or pal.timeNow() (millisecond precision) as fallback.
 * Also provides basic performance.mark/measure/getEntries.
 */

export function setupPerformance(pal) {
  const marks = new Map();
  const measures = [];

  /* Use hrtime (nanoseconds) if available for sub-ms precision,
   * otherwise fall back to timeNow (milliseconds). */
  const hasHrtime = typeof pal.hrtime === 'function';
  let _hrtimeOrigin = 0;
  if (hasHrtime) {
    _hrtimeOrigin = pal.hrtime();
  }

  function nowMs() {
    if (hasHrtime) {
      return (pal.hrtime() - _hrtimeOrigin) / 1e6;
    }
    return pal.timeNow();
  }

  const performance = {
    /**
     * Returns a high-resolution timestamp in milliseconds.
     */
    now: function() {
      return nowMs();
    },

    /**
     * Get the time origin (approximation - time when runtime started)
     * Since we don't track this precisely, we use 0 as baseline.
     */
    get timeOrigin() {
      return 0;
    },

    /**
     * Create a named performance mark.
     */
    mark: function(name, options) {
      if (typeof name !== 'string' || name === '') {
        throw new TypeError('Mark name must be a non-empty string');
      }
      marks.set(name, {
        name: name,
        entryType: 'mark',
        startTime: nowMs(),
        duration: 0
      });
    },

    /**
     * Create a named performance measure between two marks.
     */
    measure: function(name, startMark, endMark) {
      if (typeof name !== 'string' || name === '') {
        throw new TypeError('Measure name must be a non-empty string');
      }

      let startTime, endTime;

      // Handle different argument patterns
      if (typeof startMark === 'object' && startMark !== null) {
        // measure(name, options)
        const options = startMark;
        startTime = options.start !== undefined ?
          (marks.get(options.start)?.startTime ?? options.start) :
          0;
        endTime = options.end !== undefined ?
          (marks.get(options.end)?.startTime ?? options.end) :
          nowMs();
      } else {
        // measure(name, startMark?, endMark?)
        if (startMark) {
          const startEntry = marks.get(startMark);
          if (!startEntry) {
            throw new Error(`Mark '${startMark}' not found`);
          }
          startTime = startEntry.startTime;
        } else {
          startTime = 0;
        }

        if (endMark) {
          const endEntry = marks.get(endMark);
          if (!endEntry) {
            throw new Error(`Mark '${endMark}' not found`);
          }
          endTime = endEntry.startTime;
        } else {
          endTime = nowMs();
        }
      }

      measures.push({
        name: name,
        entryType: 'measure',
        startTime: startTime,
        duration: endTime - startTime
      });
    },

    /**
     * Remove a mark by name.
     */
    clearMarks: function(name) {
      if (name) {
        marks.delete(name);
      } else {
        marks.clear();
      }
    },

    /**
     * Remove measures by name.
     */
    clearMeasures: function(name) {
      if (name) {
        for (let i = measures.length - 1; i >= 0; i--) {
          if (measures[i].name === name) {
            measures.splice(i, 1);
          }
        }
      } else {
        measures.length = 0;
      }
    },

    /**
     * Get all performance entries.
     */
    getEntries: function() {
      const result = [];
      marks.forEach(entry => result.push({...entry}));
      measures.forEach(entry => result.push({...entry}));
      return result.sort((a, b) => a.startTime - b.startTime);
    },

    /**
     * Get entries by name.
     */
    getEntriesByName: function(name, type) {
      return this.getEntries().filter(entry =>
        entry.name === name && (!type || entry.entryType === type)
      );
    },

    /**
     * Get entries by type.
     */
    getEntriesByType: function(type) {
      return this.getEntries().filter(entry => entry.entryType === type);
    }
  };

  globalThis.performance = performance;
}
