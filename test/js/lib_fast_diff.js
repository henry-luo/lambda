// fast-diff 1.3.0 Library Support Tests
// The package entry point is embedded so this fixture exercises the real API.
// Source: https://github.com/jhchen/fast-diff
globalThis.global = globalThis;
globalThis.self = globalThis;
var module = { exports: {} };
var exports = module.exports;
var fast_diff;
/**
 * This library modifies the diff-patch-match library by Neil Fraser
 * by removing the patch and match functionality and certain advanced
 * options in the diff function. The original license is as follows:
 *
 * ===
 *
 * Diff Match and Patch
 *
 * Copyright 2006 Google Inc.
 * http://code.google.com/p/google-diff-match-patch/
 *
 * Licensed under the Apache License, Version 2.0 (the "License");
 * you may not use this file except in compliance with the License.
 * You may obtain a copy of the License at
 *
 *   http://www.apache.org/licenses/LICENSE-2.0
 *
 * Unless required by applicable law or agreed to in writing, software
 * distributed under the License is distributed on an "AS IS" BASIS,
 * WITHOUT WARRANTIES OR CONDITIONS OF ANY KIND, either express or implied.
 * See the License for the specific language governing permissions and
 * limitations under the License.
 */

/**
 * The data structure representing a diff is an array of tuples:
 * [[DIFF_DELETE, 'Hello'], [DIFF_INSERT, 'Goodbye'], [DIFF_EQUAL, ' world.']]
 * which means: delete 'Hello', add 'Goodbye' and keep ' world.'
 */
var DIFF_DELETE = -1;
var DIFF_INSERT = 1;
var DIFF_EQUAL = 0;

/**
 * Find the differences between two texts.  Simplifies the problem by stripping
 * any common prefix or suffix off the texts before diffing.
 * @param {string} text1 Old string to be diffed.
 * @param {string} text2 New string to be diffed.
 * @param {Int|Object} [cursor_pos] Edit position in text1 or object with more info
 * @param {boolean} [cleanup] Apply semantic cleanup before returning.
 * @return {Array} Array of diff tuples.
 */
function diff_main(text1, text2, cursor_pos, cleanup, _fix_unicode) {
  // Check for equality
  if (text1 === text2) {
    if (text1) {
      return [[DIFF_EQUAL, text1]];
    }
    return [];
  }

  if (cursor_pos != null) {
    var editdiff = find_cursor_edit_diff(text1, text2, cursor_pos);
    if (editdiff) {
      return editdiff;
    }
  }

  // Trim off common prefix (speedup).
  var commonlength = diff_commonPrefix(text1, text2);
  var commonprefix = text1.substring(0, commonlength);
  text1 = text1.substring(commonlength);
  text2 = text2.substring(commonlength);

  // Trim off common suffix (speedup).
  commonlength = diff_commonSuffix(text1, text2);
  var commonsuffix = text1.substring(text1.length - commonlength);
  text1 = text1.substring(0, text1.length - commonlength);
  text2 = text2.substring(0, text2.length - commonlength);

  // Compute the diff on the middle block.
  var diffs = diff_compute_(text1, text2);

  // Restore the prefix and suffix.
  if (commonprefix) {
    diffs.unshift([DIFF_EQUAL, commonprefix]);
  }
  if (commonsuffix) {
    diffs.push([DIFF_EQUAL, commonsuffix]);
  }
  diff_cleanupMerge(diffs, _fix_unicode);
  if (cleanup) {
    diff_cleanupSemantic(diffs);
  }
  return diffs;
}

/**
 * Find the differences between two texts.  Assumes that the texts do not
 * have any common prefix or suffix.
 * @param {string} text1 Old string to be diffed.
 * @param {string} text2 New string to be diffed.
 * @return {Array} Array of diff tuples.
 */
function diff_compute_(text1, text2) {
  var diffs;

  if (!text1) {
    // Just add some text (speedup).
    return [[DIFF_INSERT, text2]];
  }

  if (!text2) {
    // Just delete some text (speedup).
    return [[DIFF_DELETE, text1]];
  }

  var longtext = text1.length > text2.length ? text1 : text2;
  var shorttext = text1.length > text2.length ? text2 : text1;
  var i = longtext.indexOf(shorttext);
  if (i !== -1) {
    // Shorter text is inside the longer text (speedup).
    diffs = [
      [DIFF_INSERT, longtext.substring(0, i)],
      [DIFF_EQUAL, shorttext],
      [DIFF_INSERT, longtext.substring(i + shorttext.length)],
    ];
    // Swap insertions for deletions if diff is reversed.
    if (text1.length > text2.length) {
      diffs[0][0] = diffs[2][0] = DIFF_DELETE;
    }
    return diffs;
  }

  if (shorttext.length === 1) {
    // Single character string.
    // After the previous speedup, the character can't be an equality.
    return [
      [DIFF_DELETE, text1],
      [DIFF_INSERT, text2],
    ];
  }

  // Check to see if the problem can be split in two.
  var hm = diff_halfMatch_(text1, text2);
  if (hm) {
    // A half-match was found, sort out the return data.
    var text1_a = hm[0];
    var text1_b = hm[1];
    var text2_a = hm[2];
    var text2_b = hm[3];
    var mid_common = hm[4];
    // Send both pairs off for separate processing.
    var diffs_a = diff_main(text1_a, text2_a);
    var diffs_b = diff_main(text1_b, text2_b);
    // Merge the results.
    return diffs_a.concat([[DIFF_EQUAL, mid_common]], diffs_b);
  }

  return diff_bisect_(text1, text2);
}

/**
 * Find the 'middle snake' of a diff, split the problem in two
 * and return the recursively constructed diff.
 * See Myers 1986 paper: An O(ND) Difference Algorithm and Its Variations.
 * @param {string} text1 Old string to be diffed.
 * @param {string} text2 New string to be diffed.
 * @return {Array} Array of diff tuples.
 * @private
 */
function diff_bisect_(text1, text2) {
  // Cache the text lengths to prevent multiple calls.
  var text1_length = text1.length;
  var text2_length = text2.length;
  var max_d = Math.ceil((text1_length + text2_length) / 2);
  var v_offset = max_d;
  var v_length = 2 * max_d;
  var v1 = new Array(v_length);
  var v2 = new Array(v_length);
  // Setting all elements to -1 is faster in Chrome & Firefox than mixing
  // integers and undefined.
  for (var x = 0; x < v_length; x++) {
    v1[x] = -1;
    v2[x] = -1;
  }
  v1[v_offset + 1] = 0;
  v2[v_offset + 1] = 0;
  var delta = text1_length - text2_length;
  // If the total number of characters is odd, then the front path will collide
  // with the reverse path.
  var front = delta % 2 !== 0;
  // Offsets for start and end of k loop.
  // Prevents mapping of space beyond the grid.
  var k1start = 0;
  var k1end = 0;
  var k2start = 0;
  var k2end = 0;
  for (var d = 0; d < max_d; d++) {
    // Walk the front path one step.
    for (var k1 = -d + k1start; k1 <= d - k1end; k1 += 2) {
      var k1_offset = v_offset + k1;
      var x1;
      if (k1 === -d || (k1 !== d && v1[k1_offset - 1] < v1[k1_offset + 1])) {
        x1 = v1[k1_offset + 1];
      } else {
        x1 = v1[k1_offset - 1] + 1;
      }
      var y1 = x1 - k1;
      while (
        x1 < text1_length &&
        y1 < text2_length &&
        text1.charAt(x1) === text2.charAt(y1)
      ) {
        x1++;
        y1++;
      }
      v1[k1_offset] = x1;
      if (x1 > text1_length) {
        // Ran off the right of the graph.
        k1end += 2;
      } else if (y1 > text2_length) {
        // Ran off the bottom of the graph.
        k1start += 2;
      } else if (front) {
        var k2_offset = v_offset + delta - k1;
        if (k2_offset >= 0 && k2_offset < v_length && v2[k2_offset] !== -1) {
          // Mirror x2 onto top-left coordinate system.
          var x2 = text1_length - v2[k2_offset];
          if (x1 >= x2) {
            // Overlap detected.
            return diff_bisectSplit_(text1, text2, x1, y1);
          }
        }
      }
    }

    // Walk the reverse path one step.
    for (var k2 = -d + k2start; k2 <= d - k2end; k2 += 2) {
      var k2_offset = v_offset + k2;
      var x2;
      if (k2 === -d || (k2 !== d && v2[k2_offset - 1] < v2[k2_offset + 1])) {
        x2 = v2[k2_offset + 1];
      } else {
        x2 = v2[k2_offset - 1] + 1;
      }
      var y2 = x2 - k2;
      while (
        x2 < text1_length &&
        y2 < text2_length &&
        text1.charAt(text1_length - x2 - 1) ===
          text2.charAt(text2_length - y2 - 1)
      ) {
        x2++;
        y2++;
      }
      v2[k2_offset] = x2;
      if (x2 > text1_length) {
        // Ran off the left of the graph.
        k2end += 2;
      } else if (y2 > text2_length) {
        // Ran off the top of the graph.
        k2start += 2;
      } else if (!front) {
        var k1_offset = v_offset + delta - k2;
        if (k1_offset >= 0 && k1_offset < v_length && v1[k1_offset] !== -1) {
          var x1 = v1[k1_offset];
          var y1 = v_offset + x1 - k1_offset;
          // Mirror x2 onto top-left coordinate system.
          x2 = text1_length - x2;
          if (x1 >= x2) {
            // Overlap detected.
            return diff_bisectSplit_(text1, text2, x1, y1);
          }
        }
      }
    }
  }
  // Diff took too long and hit the deadline or
  // number of diffs equals number of characters, no commonality at all.
  return [
    [DIFF_DELETE, text1],
    [DIFF_INSERT, text2],
  ];
}

/**
 * Given the location of the 'middle snake', split the diff in two parts
 * and recurse.
 * @param {string} text1 Old string to be diffed.
 * @param {string} text2 New string to be diffed.
 * @param {number} x Index of split point in text1.
 * @param {number} y Index of split point in text2.
 * @return {Array} Array of diff tuples.
 */
function diff_bisectSplit_(text1, text2, x, y) {
  var text1a = text1.substring(0, x);
  var text2a = text2.substring(0, y);
  var text1b = text1.substring(x);
  var text2b = text2.substring(y);

  // Compute both diffs serially.
  var diffs = diff_main(text1a, text2a);
  var diffsb = diff_main(text1b, text2b);

  return diffs.concat(diffsb);
}

/**
 * Determine the common prefix of two strings.
 * @param {string} text1 First string.
 * @param {string} text2 Second string.
 * @return {number} The number of characters common to the start of each
 *     string.
 */
function diff_commonPrefix(text1, text2) {
  // Quick check for common null cases.
  if (!text1 || !text2 || text1.charAt(0) !== text2.charAt(0)) {
    return 0;
  }
  // Binary search.
  // Performance analysis: http://neil.fraser.name/news/2007/10/09/
  var pointermin = 0;
  var pointermax = Math.min(text1.length, text2.length);
  var pointermid = pointermax;
  var pointerstart = 0;
  while (pointermin < pointermid) {
    if (
      text1.substring(pointerstart, pointermid) ==
      text2.substring(pointerstart, pointermid)
    ) {
      pointermin = pointermid;
      pointerstart = pointermin;
    } else {
      pointermax = pointermid;
    }
    pointermid = Math.floor((pointermax - pointermin) / 2 + pointermin);
  }

  if (is_surrogate_pair_start(text1.charCodeAt(pointermid - 1))) {
    pointermid--;
  }

  return pointermid;
}

/**
 * Determine if the suffix of one string is the prefix of another.
 * @param {string} text1 First string.
 * @param {string} text2 Second string.
 * @return {number} The number of characters common to the end of the first
 *     string and the start of the second string.
 * @private
 */
function diff_commonOverlap_(text1, text2) {
  // Cache the text lengths to prevent multiple calls.
  var text1_length = text1.length;
  var text2_length = text2.length;
  // Eliminate the null case.
  if (text1_length == 0 || text2_length == 0) {
    return 0;
  }
  // Truncate the longer string.
  if (text1_length > text2_length) {
    text1 = text1.substring(text1_length - text2_length);
  } else if (text1_length < text2_length) {
    text2 = text2.substring(0, text1_length);
  }
  var text_length = Math.min(text1_length, text2_length);
  // Quick check for the worst case.
  if (text1 == text2) {
    return text_length;
  }

  // Start by looking for a single character match
  // and increase length until no match is found.
  // Performance analysis: http://neil.fraser.name/news/2010/11/04/
  var best = 0;
  var length = 1;
  while (true) {
    var pattern = text1.substring(text_length - length);
    var found = text2.indexOf(pattern);
    if (found == -1) {
      return best;
    }
    length += found;
    if (
      found == 0 ||
      text1.substring(text_length - length) == text2.substring(0, length)
    ) {
      best = length;
      length++;
    }
  }
}

/**
 * Determine the common suffix of two strings.
 * @param {string} text1 First string.
 * @param {string} text2 Second string.
 * @return {number} The number of characters common to the end of each string.
 */
function diff_commonSuffix(text1, text2) {
  // Quick check for common null cases.
  if (!text1 || !text2 || text1.slice(-1) !== text2.slice(-1)) {
    return 0;
  }
  // Binary search.
  // Performance analysis: http://neil.fraser.name/news/2007/10/09/
  var pointermin = 0;
  var pointermax = Math.min(text1.length, text2.length);
  var pointermid = pointermax;
  var pointerend = 0;
  while (pointermin < pointermid) {
    if (
      text1.substring(text1.length - pointermid, text1.length - pointerend) ==
      text2.substring(text2.length - pointermid, text2.length - pointerend)
    ) {
      pointermin = pointermid;
      pointerend = pointermin;
    } else {
      pointermax = pointermid;
    }
    pointermid = Math.floor((pointermax - pointermin) / 2 + pointermin);
  }

  if (is_surrogate_pair_end(text1.charCodeAt(text1.length - pointermid))) {
    pointermid--;
  }

  return pointermid;
}

/**
 * Do the two texts share a substring which is at least half the length of the
 * longer text?
 * This speedup can produce non-minimal diffs.
 * @param {string} text1 First string.
 * @param {string} text2 Second string.
 * @return {Array.<string>} Five element Array, containing the prefix of
 *     text1, the suffix of text1, the prefix of text2, the suffix of
 *     text2 and the common middle.  Or null if there was no match.
 */
function diff_halfMatch_(text1, text2) {
  var longtext = text1.length > text2.length ? text1 : text2;
  var shorttext = text1.length > text2.length ? text2 : text1;
  if (longtext.length < 4 || shorttext.length * 2 < longtext.length) {
    return null; // Pointless.
  }

  /**
   * Does a substring of shorttext exist within longtext such that the substring
   * is at least half the length of longtext?
   * Closure, but does not reference any external variables.
   * @param {string} longtext Longer string.
   * @param {string} shorttext Shorter string.
   * @param {number} i Start index of quarter length substring within longtext.
   * @return {Array.<string>} Five element Array, containing the prefix of
   *     longtext, the suffix of longtext, the prefix of shorttext, the suffix
   *     of shorttext and the common middle.  Or null if there was no match.
   * @private
   */
  function diff_halfMatchI_(longtext, shorttext, i) {
    // Start with a 1/4 length substring at position i as a seed.
    var seed = longtext.substring(i, i + Math.floor(longtext.length / 4));
    var j = -1;
    var best_common = "";
    var best_longtext_a, best_longtext_b, best_shorttext_a, best_shorttext_b;
    while ((j = shorttext.indexOf(seed, j + 1)) !== -1) {
      var prefixLength = diff_commonPrefix(
        longtext.substring(i),
        shorttext.substring(j)
      );
      var suffixLength = diff_commonSuffix(
        longtext.substring(0, i),
        shorttext.substring(0, j)
      );
      if (best_common.length < suffixLength + prefixLength) {
        best_common =
          shorttext.substring(j - suffixLength, j) +
          shorttext.substring(j, j + prefixLength);
        best_longtext_a = longtext.substring(0, i - suffixLength);
        best_longtext_b = longtext.substring(i + prefixLength);
        best_shorttext_a = shorttext.substring(0, j - suffixLength);
        best_shorttext_b = shorttext.substring(j + prefixLength);
      }
    }
    if (best_common.length * 2 >= longtext.length) {
      return [
        best_longtext_a,
        best_longtext_b,
        best_shorttext_a,
        best_shorttext_b,
        best_common,
      ];
    } else {
      return null;
    }
  }

  // First check if the second quarter is the seed for a half-match.
  var hm1 = diff_halfMatchI_(
    longtext,
    shorttext,
    Math.ceil(longtext.length / 4)
  );
  // Check again based on the third quarter.
  var hm2 = diff_halfMatchI_(
    longtext,
    shorttext,
    Math.ceil(longtext.length / 2)
  );
  var hm;
  if (!hm1 && !hm2) {
    return null;
  } else if (!hm2) {
    hm = hm1;
  } else if (!hm1) {
    hm = hm2;
  } else {
    // Both matched.  Select the longest.
    hm = hm1[4].length > hm2[4].length ? hm1 : hm2;
  }

  // A half-match was found, sort out the return data.
  var text1_a, text1_b, text2_a, text2_b;
  if (text1.length > text2.length) {
    text1_a = hm[0];
    text1_b = hm[1];
    text2_a = hm[2];
    text2_b = hm[3];
  } else {
    text2_a = hm[0];
    text2_b = hm[1];
    text1_a = hm[2];
    text1_b = hm[3];
  }
  var mid_common = hm[4];
  return [text1_a, text1_b, text2_a, text2_b, mid_common];
}

/**
 * Reduce the number of edits by eliminating semantically trivial equalities.
 * @param {!Array.<!diff_match_patch.Diff>} diffs Array of diff tuples.
 */
function diff_cleanupSemantic(diffs) {
  var changes = false;
  var equalities = []; // Stack of indices where equalities are found.
  var equalitiesLength = 0; // Keeping our own length var is faster in JS.
  /** @type {?string} */
  var lastequality = null;
  // Always equal to diffs[equalities[equalitiesLength - 1]][1]
  var pointer = 0; // Index of current position.
  // Number of characters that changed prior to the equality.
  var length_insertions1 = 0;
  var length_deletions1 = 0;
  // Number of characters that changed after the equality.
  var length_insertions2 = 0;
  var length_deletions2 = 0;
  while (pointer < diffs.length) {
    if (diffs[pointer][0] == DIFF_EQUAL) {
      // Equality found.
      equalities[equalitiesLength++] = pointer;
      length_insertions1 = length_insertions2;
      length_deletions1 = length_deletions2;
      length_insertions2 = 0;
      length_deletions2 = 0;
      lastequality = diffs[pointer][1];
    } else {
      // An insertion or deletion.
      if (diffs[pointer][0] == DIFF_INSERT) {
        length_insertions2 += diffs[pointer][1].length;
      } else {
        length_deletions2 += diffs[pointer][1].length;
      }
      // Eliminate an equality that is smaller or equal to the edits on both
      // sides of it.
      if (
        lastequality &&
        lastequality.length <=
          Math.max(length_insertions1, length_deletions1) &&
        lastequality.length <= Math.max(length_insertions2, length_deletions2)
      ) {
        // Duplicate record.
        diffs.splice(equalities[equalitiesLength - 1], 0, [
          DIFF_DELETE,
          lastequality,
        ]);
        // Change second copy to insert.
        diffs[equalities[equalitiesLength - 1] + 1][0] = DIFF_INSERT;
        // Throw away the equality we just deleted.
        equalitiesLength--;
        // Throw away the previous equality (it needs to be reevaluated).
        equalitiesLength--;
        pointer = equalitiesLength > 0 ? equalities[equalitiesLength - 1] : -1;
        length_insertions1 = 0; // Reset the counters.
        length_deletions1 = 0;
        length_insertions2 = 0;
        length_deletions2 = 0;
        lastequality = null;
        changes = true;
      }
    }
    pointer++;
  }

  // Normalize the diff.
  if (changes) {
    diff_cleanupMerge(diffs);
  }
  diff_cleanupSemanticLossless(diffs);

  // Find any overlaps between deletions and insertions.
  // e.g: <del>abcxxx</del><ins>xxxdef</ins>
  //   -> <del>abc</del>xxx<ins>def</ins>
  // e.g: <del>xxxabc</del><ins>defxxx</ins>
  //   -> <ins>def</ins>xxx<del>abc</del>
  // Only extract an overlap if it is as big as the edit ahead or behind it.
  pointer = 1;
  while (pointer < diffs.length) {
    if (
      diffs[pointer - 1][0] == DIFF_DELETE &&
      diffs[pointer][0] == DIFF_INSERT
    ) {
      var deletion = diffs[pointer - 1][1];
      var insertion = diffs[pointer][1];
      var overlap_length1 = diff_commonOverlap_(deletion, insertion);
      var overlap_length2 = diff_commonOverlap_(insertion, deletion);
      if (overlap_length1 >= overlap_length2) {
        if (
          overlap_length1 >= deletion.length / 2 ||
          overlap_length1 >= insertion.length / 2
        ) {
          // Overlap found.  Insert an equality and trim the surrounding edits.
          diffs.splice(pointer, 0, [
            DIFF_EQUAL,
            insertion.substring(0, overlap_length1),
          ]);
          diffs[pointer - 1][1] = deletion.substring(
            0,
            deletion.length - overlap_length1
          );
          diffs[pointer + 1][1] = insertion.substring(overlap_length1);
          pointer++;
        }
      } else {
        if (
          overlap_length2 >= deletion.length / 2 ||
          overlap_length2 >= insertion.length / 2
        ) {
          // Reverse overlap found.
          // Insert an equality and swap and trim the surrounding edits.
          diffs.splice(pointer, 0, [
            DIFF_EQUAL,
            deletion.substring(0, overlap_length2),
          ]);
          diffs[pointer - 1][0] = DIFF_INSERT;
          diffs[pointer - 1][1] = insertion.substring(
            0,
            insertion.length - overlap_length2
          );
          diffs[pointer + 1][0] = DIFF_DELETE;
          diffs[pointer + 1][1] = deletion.substring(overlap_length2);
          pointer++;
        }
      }
      pointer++;
    }
    pointer++;
  }
}

var nonAlphaNumericRegex_ = /[^a-zA-Z0-9]/;
var whitespaceRegex_ = /\s/;
var linebreakRegex_ = /[\r\n]/;
var blanklineEndRegex_ = /\n\r?\n$/;
var blanklineStartRegex_ = /^\r?\n\r?\n/;

/**
 * Look for single edits surrounded on both sides by equalities
 * which can be shifted sideways to align the edit to a word boundary.
 * e.g: The c<ins>at c</ins>ame. -> The <ins>cat </ins>came.
 * @param {!Array.<!diff_match_patch.Diff>} diffs Array of diff tuples.
 */
function diff_cleanupSemanticLossless(diffs) {
  /**
   * Given two strings, compute a score representing whether the internal
   * boundary falls on logical boundaries.
   * Scores range from 6 (best) to 0 (worst).
   * Closure, but does not reference any external variables.
   * @param {string} one First string.
   * @param {string} two Second string.
   * @return {number} The score.
   * @private
   */
  function diff_cleanupSemanticScore_(one, two) {
    if (!one || !two) {
      // Edges are the best.
      return 6;
    }

    // Each port of this function behaves slightly differently due to
    // subtle differences in each language's definition of things like
    // 'whitespace'.  Since this function's purpose is largely cosmetic,
    // the choice has been made to use each language's native features
    // rather than force total conformity.
    var char1 = one.charAt(one.length - 1);
    var char2 = two.charAt(0);
    var nonAlphaNumeric1 = char1.match(nonAlphaNumericRegex_);
    var nonAlphaNumeric2 = char2.match(nonAlphaNumericRegex_);
    var whitespace1 = nonAlphaNumeric1 && char1.match(whitespaceRegex_);
    var whitespace2 = nonAlphaNumeric2 && char2.match(whitespaceRegex_);
    var lineBreak1 = whitespace1 && char1.match(linebreakRegex_);
    var lineBreak2 = whitespace2 && char2.match(linebreakRegex_);
    var blankLine1 = lineBreak1 && one.match(blanklineEndRegex_);
    var blankLine2 = lineBreak2 && two.match(blanklineStartRegex_);

    if (blankLine1 || blankLine2) {
      // Five points for blank lines.
      return 5;
    } else if (lineBreak1 || lineBreak2) {
      // Four points for line breaks.
      return 4;
    } else if (nonAlphaNumeric1 && !whitespace1 && whitespace2) {
      // Three points for end of sentences.
      return 3;
    } else if (whitespace1 || whitespace2) {
      // Two points for whitespace.
      return 2;
    } else if (nonAlphaNumeric1 || nonAlphaNumeric2) {
      // One point for non-alphanumeric.
      return 1;
    }
    return 0;
  }

  var pointer = 1;
  // Intentionally ignore the first and last element (don't need checking).
  while (pointer < diffs.length - 1) {
    if (
      diffs[pointer - 1][0] == DIFF_EQUAL &&
      diffs[pointer + 1][0] == DIFF_EQUAL
    ) {
      // This is a single edit surrounded by equalities.
      var equality1 = diffs[pointer - 1][1];
      var edit = diffs[pointer][1];
      var equality2 = diffs[pointer + 1][1];

      // First, shift the edit as far left as possible.
      var commonOffset = diff_commonSuffix(equality1, edit);
      if (commonOffset) {
        var commonString = edit.substring(edit.length - commonOffset);
        equality1 = equality1.substring(0, equality1.length - commonOffset);
        edit = commonString + edit.substring(0, edit.length - commonOffset);
        equality2 = commonString + equality2;
      }

      // Second, step character by character right, looking for the best fit.
      var bestEquality1 = equality1;
      var bestEdit = edit;
      var bestEquality2 = equality2;
      var bestScore =
        diff_cleanupSemanticScore_(equality1, edit) +
        diff_cleanupSemanticScore_(edit, equality2);
      while (edit.charAt(0) === equality2.charAt(0)) {
        equality1 += edit.charAt(0);
        edit = edit.substring(1) + equality2.charAt(0);
        equality2 = equality2.substring(1);
        var score =
          diff_cleanupSemanticScore_(equality1, edit) +
          diff_cleanupSemanticScore_(edit, equality2);
        // The >= encourages trailing rather than leading whitespace on edits.
        if (score >= bestScore) {
          bestScore = score;
          bestEquality1 = equality1;
          bestEdit = edit;
          bestEquality2 = equality2;
        }
      }

      if (diffs[pointer - 1][1] != bestEquality1) {
        // We have an improvement, save it back to the diff.
        if (bestEquality1) {
          diffs[pointer - 1][1] = bestEquality1;
        } else {
          diffs.splice(pointer - 1, 1);
          pointer--;
        }
        diffs[pointer][1] = bestEdit;
        if (bestEquality2) {
          diffs[pointer + 1][1] = bestEquality2;
        } else {
          diffs.splice(pointer + 1, 1);
          pointer--;
        }
      }
    }
    pointer++;
  }
}

/**
 * Reorder and merge like edit sections.  Merge equalities.
 * Any edit section can move as long as it doesn't cross an equality.
 * @param {Array} diffs Array of diff tuples.
 * @param {boolean} fix_unicode Whether to normalize to a unicode-correct diff
 */
function diff_cleanupMerge(diffs, fix_unicode) {
  diffs.push([DIFF_EQUAL, ""]); // Add a dummy entry at the end.
  var pointer = 0;
  var count_delete = 0;
  var count_insert = 0;
  var text_delete = "";
  var text_insert = "";
  var commonlength;
  while (pointer < diffs.length) {
    if (pointer < diffs.length - 1 && !diffs[pointer][1]) {
      diffs.splice(pointer, 1);
      continue;
    }
    switch (diffs[pointer][0]) {
      case DIFF_INSERT:
        count_insert++;
        text_insert += diffs[pointer][1];
        pointer++;
        break;
      case DIFF_DELETE:
        count_delete++;
        text_delete += diffs[pointer][1];
        pointer++;
        break;
      case DIFF_EQUAL:
        var previous_equality = pointer - count_insert - count_delete - 1;
        if (fix_unicode) {
          // prevent splitting of unicode surrogate pairs.  when fix_unicode is true,
          // we assume that the old and new text in the diff are complete and correct
          // unicode-encoded JS strings, but the tuple boundaries may fall between
          // surrogate pairs.  we fix this by shaving off stray surrogates from the end
          // of the previous equality and the beginning of this equality.  this may create
          // empty equalities or a common prefix or suffix.  for example, if AB and AC are
          // emojis, `[[0, 'A'], [-1, 'BA'], [0, 'C']]` would turn into deleting 'ABAC' and
          // inserting 'AC', and then the common suffix 'AC' will be eliminated.  in this
          // particular case, both equalities go away, we absorb any previous inequalities,
          // and we keep scanning for the next equality before rewriting the tuples.
          if (
            previous_equality >= 0 &&
            ends_with_pair_start(diffs[previous_equality][1])
          ) {
            var stray = diffs[previous_equality][1].slice(-1);
            diffs[previous_equality][1] = diffs[previous_equality][1].slice(
              0,
              -1
            );
            text_delete = stray + text_delete;
            text_insert = stray + text_insert;
            if (!diffs[previous_equality][1]) {
              // emptied out previous equality, so delete it and include previous delete/insert
              diffs.splice(previous_equality, 1);
              pointer--;
              var k = previous_equality - 1;
              if (diffs[k] && diffs[k][0] === DIFF_INSERT) {
                count_insert++;
                text_insert = diffs[k][1] + text_insert;
                k--;
              }
              if (diffs[k] && diffs[k][0] === DIFF_DELETE) {
                count_delete++;
                text_delete = diffs[k][1] + text_delete;
                k--;
              }
              previous_equality = k;
            }
          }
          if (starts_with_pair_end(diffs[pointer][1])) {
            var stray = diffs[pointer][1].charAt(0);
            diffs[pointer][1] = diffs[pointer][1].slice(1);
            text_delete += stray;
            text_insert += stray;
          }
        }
        if (pointer < diffs.length - 1 && !diffs[pointer][1]) {
          // for empty equality not at end, wait for next equality
          diffs.splice(pointer, 1);
          break;
        }
        if (text_delete.length > 0 || text_insert.length > 0) {
          // note that diff_commonPrefix and diff_commonSuffix are unicode-aware
          if (text_delete.length > 0 && text_insert.length > 0) {
            // Factor out any common prefixes.
            commonlength = diff_commonPrefix(text_insert, text_delete);
            if (commonlength !== 0) {
              if (previous_equality >= 0) {
                diffs[previous_equality][1] += text_insert.substring(
                  0,
                  commonlength
                );
              } else {
                diffs.splice(0, 0, [
                  DIFF_EQUAL,
                  text_insert.substring(0, commonlength),
                ]);
                pointer++;
              }
              text_insert = text_insert.substring(commonlength);
              text_delete = text_delete.substring(commonlength);
            }
            // Factor out any common suffixes.
            commonlength = diff_commonSuffix(text_insert, text_delete);
            if (commonlength !== 0) {
              diffs[pointer][1] =
                text_insert.substring(text_insert.length - commonlength) +
                diffs[pointer][1];
              text_insert = text_insert.substring(
                0,
                text_insert.length - commonlength
              );
              text_delete = text_delete.substring(
                0,
                text_delete.length - commonlength
              );
            }
          }
          // Delete the offending records and add the merged ones.
          var n = count_insert + count_delete;
          if (text_delete.length === 0 && text_insert.length === 0) {
            diffs.splice(pointer - n, n);
            pointer = pointer - n;
          } else if (text_delete.length === 0) {
            diffs.splice(pointer - n, n, [DIFF_INSERT, text_insert]);
            pointer = pointer - n + 1;
          } else if (text_insert.length === 0) {
            diffs.splice(pointer - n, n, [DIFF_DELETE, text_delete]);
            pointer = pointer - n + 1;
          } else {
            diffs.splice(
              pointer - n,
              n,
              [DIFF_DELETE, text_delete],
              [DIFF_INSERT, text_insert]
            );
            pointer = pointer - n + 2;
          }
        }
        if (pointer !== 0 && diffs[pointer - 1][0] === DIFF_EQUAL) {
          // Merge this equality with the previous one.
          diffs[pointer - 1][1] += diffs[pointer][1];
          diffs.splice(pointer, 1);
        } else {
          pointer++;
        }
        count_insert = 0;
        count_delete = 0;
        text_delete = "";
        text_insert = "";
        break;
    }
  }
  if (diffs[diffs.length - 1][1] === "") {
    diffs.pop(); // Remove the dummy entry at the end.
  }

  // Second pass: look for single edits surrounded on both sides by equalities
  // which can be shifted sideways to eliminate an equality.
  // e.g: A<ins>BA</ins>C -> <ins>AB</ins>AC
  var changes = false;
  pointer = 1;
  // Intentionally ignore the first and last element (don't need checking).
  while (pointer < diffs.length - 1) {
    if (
      diffs[pointer - 1][0] === DIFF_EQUAL &&
      diffs[pointer + 1][0] === DIFF_EQUAL
    ) {
      // This is a single edit surrounded by equalities.
      if (
        diffs[pointer][1].substring(
          diffs[pointer][1].length - diffs[pointer - 1][1].length
        ) === diffs[pointer - 1][1]
      ) {
        // Shift the edit over the previous equality.
        diffs[pointer][1] =
          diffs[pointer - 1][1] +
          diffs[pointer][1].substring(
            0,
            diffs[pointer][1].length - diffs[pointer - 1][1].length
          );
        diffs[pointer + 1][1] = diffs[pointer - 1][1] + diffs[pointer + 1][1];
        diffs.splice(pointer - 1, 1);
        changes = true;
      } else if (
        diffs[pointer][1].substring(0, diffs[pointer + 1][1].length) ==
        diffs[pointer + 1][1]
      ) {
        // Shift the edit over the next equality.
        diffs[pointer - 1][1] += diffs[pointer + 1][1];
        diffs[pointer][1] =
          diffs[pointer][1].substring(diffs[pointer + 1][1].length) +
          diffs[pointer + 1][1];
        diffs.splice(pointer + 1, 1);
        changes = true;
      }
    }
    pointer++;
  }
  // If shifts were made, the diff needs reordering and another shift sweep.
  if (changes) {
    diff_cleanupMerge(diffs, fix_unicode);
  }
}

function is_surrogate_pair_start(charCode) {
  return charCode >= 0xd800 && charCode <= 0xdbff;
}

function is_surrogate_pair_end(charCode) {
  return charCode >= 0xdc00 && charCode <= 0xdfff;
}

function starts_with_pair_end(str) {
  return is_surrogate_pair_end(str.charCodeAt(0));
}

function ends_with_pair_start(str) {
  return is_surrogate_pair_start(str.charCodeAt(str.length - 1));
}

function remove_empty_tuples(tuples) {
  var ret = [];
  for (var i = 0; i < tuples.length; i++) {
    if (tuples[i][1].length > 0) {
      ret.push(tuples[i]);
    }
  }
  return ret;
}

function make_edit_splice(before, oldMiddle, newMiddle, after) {
  if (ends_with_pair_start(before) || starts_with_pair_end(after)) {
    return null;
  }
  return remove_empty_tuples([
    [DIFF_EQUAL, before],
    [DIFF_DELETE, oldMiddle],
    [DIFF_INSERT, newMiddle],
    [DIFF_EQUAL, after],
  ]);
}

function find_cursor_edit_diff(oldText, newText, cursor_pos) {
  // note: this runs after equality check has ruled out exact equality
  var oldRange =
    typeof cursor_pos === "number"
      ? { index: cursor_pos, length: 0 }
      : cursor_pos.oldRange;
  var newRange = typeof cursor_pos === "number" ? null : cursor_pos.newRange;
  // take into account the old and new selection to generate the best diff
  // possible for a text edit.  for example, a text change from "xxx" to "xx"
  // could be a delete or forwards-delete of any one of the x's, or the
  // result of selecting two of the x's and typing "x".
  var oldLength = oldText.length;
  var newLength = newText.length;
  if (oldRange.length === 0 && (newRange === null || newRange.length === 0)) {
    // see if we have an insert or delete before or after cursor
    var oldCursor = oldRange.index;
    var oldBefore = oldText.slice(0, oldCursor);
    var oldAfter = oldText.slice(oldCursor);
    var maybeNewCursor = newRange ? newRange.index : null;
    editBefore: {
      // is this an insert or delete right before oldCursor?
      var newCursor = oldCursor + newLength - oldLength;
      if (maybeNewCursor !== null && maybeNewCursor !== newCursor) {
        break editBefore;
      }
      if (newCursor < 0 || newCursor > newLength) {
        break editBefore;
      }
      var newBefore = newText.slice(0, newCursor);
      var newAfter = newText.slice(newCursor);
      if (newAfter !== oldAfter) {
        break editBefore;
      }
      var prefixLength = Math.min(oldCursor, newCursor);
      var oldPrefix = oldBefore.slice(0, prefixLength);
      var newPrefix = newBefore.slice(0, prefixLength);
      if (oldPrefix !== newPrefix) {
        break editBefore;
      }
      var oldMiddle = oldBefore.slice(prefixLength);
      var newMiddle = newBefore.slice(prefixLength);
      return make_edit_splice(oldPrefix, oldMiddle, newMiddle, oldAfter);
    }
    editAfter: {
      // is this an insert or delete right after oldCursor?
      if (maybeNewCursor !== null && maybeNewCursor !== oldCursor) {
        break editAfter;
      }
      var cursor = oldCursor;
      var newBefore = newText.slice(0, cursor);
      var newAfter = newText.slice(cursor);
      if (newBefore !== oldBefore) {
        break editAfter;
      }
      var suffixLength = Math.min(oldLength - cursor, newLength - cursor);
      var oldSuffix = oldAfter.slice(oldAfter.length - suffixLength);
      var newSuffix = newAfter.slice(newAfter.length - suffixLength);
      if (oldSuffix !== newSuffix) {
        break editAfter;
      }
      var oldMiddle = oldAfter.slice(0, oldAfter.length - suffixLength);
      var newMiddle = newAfter.slice(0, newAfter.length - suffixLength);
      return make_edit_splice(oldBefore, oldMiddle, newMiddle, oldSuffix);
    }
  }
  if (oldRange.length > 0 && newRange && newRange.length === 0) {
    replaceRange: {
      // see if diff could be a splice of the old selection range
      var oldPrefix = oldText.slice(0, oldRange.index);
      var oldSuffix = oldText.slice(oldRange.index + oldRange.length);
      var prefixLength = oldPrefix.length;
      var suffixLength = oldSuffix.length;
      if (newLength < prefixLength + suffixLength) {
        break replaceRange;
      }
      var newPrefix = newText.slice(0, prefixLength);
      var newSuffix = newText.slice(newLength - suffixLength);
      if (oldPrefix !== newPrefix || oldSuffix !== newSuffix) {
        break replaceRange;
      }
      var oldMiddle = oldText.slice(prefixLength, oldLength - suffixLength);
      var newMiddle = newText.slice(prefixLength, newLength - suffixLength);
      return make_edit_splice(oldPrefix, oldMiddle, newMiddle, oldSuffix);
    }
  }

  return null;
}

function diff(text1, text2, cursor_pos, cleanup) {
  // only pass fix_unicode=true at the top level, not when diff_main is
  // recursively invoked
  return diff_main(text1, text2, cursor_pos, cleanup, true);
}

diff.INSERT = DIFF_INSERT;
diff.DELETE = DIFF_DELETE;
diff.EQUAL = DIFF_EQUAL;

module.exports = diff;

fast_diff = module.exports;


// === Test 1: package export and constants ===
console.log(typeof fast_diff);
console.log(fast_diff.INSERT, fast_diff.DELETE, fast_diff.EQUAL);

// === Test 2: equality and empty input ===
console.log(JSON.stringify(fast_diff("", "")));
console.log(JSON.stringify(fast_diff("same", "same")));

// === Test 3: insertion, deletion, and replacement ===
console.log(JSON.stringify(fast_diff("abc", "abXc")));
console.log(JSON.stringify(fast_diff("abc", "ac")));
console.log(JSON.stringify(fast_diff("abc", "axc")));

// === Test 4: common text and multiline input ===
console.log(JSON.stringify(fast_diff("hello world", "hello brave world")));
console.log(JSON.stringify(fast_diff("line one\nline two", "line 1\nline two")));

// === Test 5: cursor-aware insertion ===
console.log(JSON.stringify(fast_diff("abc", "abXc", 2)));

// === Test 6: selection-aware replacement ===
console.log(JSON.stringify(fast_diff(
  "hello",
  "goodbye",
  { oldRange: { index: 0, length: 5 }, newRange: { index: 0, length: 0 } }
)));

// === Test 7: semantic cleanup option ===
console.log(JSON.stringify(fast_diff("The c at came.", "The cat came.", undefined, true)));

// === Test 8: Unicode surrogate-pair handling ===
console.log(JSON.stringify(fast_diff("A😀B", "A😃B")));

// === Advanced validation helpers ===
function same_utf16_text(left, right) {
  if (left.length !== right.length) {
    return false;
  }
  for (var i = 0; i < left.length; i++) {
    if (left.charCodeAt(i) !== right.charCodeAt(i)) {
      return false;
    }
  }
  return true;
}

function assert_valid_diff(old_text, new_text, diffs, label) {
  var old_pos = 0;
  var rebuilt_old = "";
  var rebuilt_new = "";
  var previous_op = null;
  var inserts_since_last_equality = 0;
  var deletes_since_last_equality = 0;

  for (var i = 0; i < diffs.length; i++) {
    var tuple = diffs[i];
    if (!tuple || tuple.length !== 2 || tuple[1].length === 0) {
      throw new Error(label + ": empty or malformed tuple");
    }

    var op = tuple[0];
    var text = tuple[1];
    if (op !== fast_diff.EQUAL &&
        op !== fast_diff.DELETE &&
        op !== fast_diff.INSERT) {
      throw new Error(label + ": unknown operation " + op);
    }
    if (op === previous_op) {
      throw new Error(label + ": adjacent operations were not merged");
    }

    // Diff tuples must not split a UTF-16 surrogate pair at either boundary.
    var first_code = text.charCodeAt(0);
    var last_code = text.charCodeAt(text.length - 1);
    if ((first_code >= 0xdc00 && first_code <= 0xdfff) ||
        (last_code >= 0xd800 && last_code <= 0xdbff)) {
      throw new Error(label + ": tuple splits a surrogate pair " + JSON.stringify(diffs));
    }

    if (op === fast_diff.EQUAL) {
      inserts_since_last_equality = 0;
      deletes_since_last_equality = 0;
    } else if (op === fast_diff.DELETE) {
      if (deletes_since_last_equality) {
        throw new Error(label + ": multiple deletes between equalities");
      }
      if (inserts_since_last_equality) {
        throw new Error(label + ": delete follows insert");
      }
      deletes_since_last_equality++;
    } else {
      if (inserts_since_last_equality) {
        throw new Error(label + ": multiple inserts between equalities");
      }
      inserts_since_last_equality++;
    }
    if (op === fast_diff.EQUAL || op === fast_diff.DELETE) {
      if (!same_utf16_text(
        old_text.substring(old_pos, old_pos + text.length),
        text
      )) {
        throw new Error(label + ": tuple does not consume old text");
      }
      old_pos += text.length;
      rebuilt_old += text;
    }
    if (op === fast_diff.EQUAL || op === fast_diff.INSERT) {
      rebuilt_new += text;
    }
    previous_op = op;
  }

  if (old_pos !== old_text.length || !same_utf16_text(rebuilt_old, old_text)) {
    throw new Error(label + ": diff did not consume old text " + JSON.stringify(diffs));
  }
  if (!same_utf16_text(rebuilt_new, new_text)) {
    throw new Error(label + ": diff did not produce new text");
  }
}

function verify_diff(old_text, new_text, cursor_pos, cleanup, label) {
  var result = fast_diff(old_text, new_text, cursor_pos, cleanup);
  assert_valid_diff(old_text, new_text, result, label);
  return result;
}

function assert_same_diff(actual, expected, label) {
  if (actual.length !== expected.length) {
    throw new Error(label + ": unexpected tuple count");
  }
  for (var i = 0; i < actual.length; i++) {
    if (actual[i][0] !== expected[i][0] ||
        !same_utf16_text(actual[i][1], expected[i][1])) {
      throw new Error(label + ": unexpected diff " + JSON.stringify(actual));
    }
  }
}

function expect_diff(old_text, new_text, cursor_pos, cleanup, expected, label) {
  var result = verify_diff(old_text, new_text, cursor_pos, cleanup, label);
  if (JSON.stringify(result) !== expected) {
    throw new Error(label + ": unexpected diff " + JSON.stringify(result));
  }
}

// === Test 9: half-match, bisect, and repeated-text paths ===
verify_diff(
  "prefix--0123456789ABCDEFGHIJKLMNOPQRSTUVWXYZ--suffix",
  "prefix--01234abcdefghijKLMNOPQRSTUVWXYZ--suffix",
  null,
  false,
  "half-match"
);
verify_diff(
  "GATTACA-GATTACA-GATTACA-GATTACA",
  "GATTTCA-GATTACA-GAT-ACA-GATTACA",
  null,
  false,
  "repeated-text"
);
verify_diff(
  "abcdefghij0123456789klmnopqrst",
  "abXYefghij0123ZZ6789klmnoQQrst",
  null,
  false,
  "multiple-independent-edits"
);
expect_diff(
  "111xxxabc",
  "111defxxx",
  null,
  true,
  "[[0,\"111\"],[1,\"def\"],[0,\"xxx\"],[-1,\"abc\"]]",
  "semantic-overlap"
);

// === Test 10: cursor edits before, after, and around selections ===
var cursor_cases = [
  ["", 0, "a"],
  ["a", 0, "aa"],
  ["a", 1, "aa"],
  ["aa", 0, "a"],
  ["aa", 1, "a"],
  ["aa", 2, "a"],
  ["aaa", 1, "aa"],
  ["aaa", 3, "aa"],
  ["bob", 0, "bobob"],
  ["bob", 1, "bobob"],
  ["bob", 2, "bobob"],
  ["bobob", 3, "bob"],
];
for (var cursor_index = 0; cursor_index < cursor_cases.length; cursor_index++) {
  var cursor_case = cursor_cases[cursor_index];
  verify_diff(
    cursor_case[0],
    cursor_case[2],
    cursor_case[1],
    false,
    "cursor-" + cursor_index
  );
  verify_diff(
    cursor_case[0],
    cursor_case[2],
    cursor_case[1],
    true,
    "cursor-cleanup-" + cursor_index
  );
}

var selection_cases = [
  [
    "12345",
    "15",
    { oldRange: { index: 1, length: 2 }, newRange: { index: 1, length: 0 } }
  ],
  [
    "hello",
    "h",
    { oldRange: { index: 0, length: 5 }, newRange: { index: 1, length: 0 } }
  ],
  [
    "bobob",
    "bob",
    { oldRange: { index: 1, length: 3 }, newRange: { index: 2, length: 0 } }
  ],
];
for (var selection_index = 0; selection_index < selection_cases.length; selection_index++) {
  var selection_case = selection_cases[selection_index];
  verify_diff(
    selection_case[0],
    selection_case[1],
    selection_case[2],
    false,
    "selection-" + selection_index
  );
  verify_diff(
    selection_case[0],
    selection_case[1],
    selection_case[2],
    true,
    "selection-cleanup-" + selection_index
  );
}

// === Test 11: semantic cleanup shifts and overlap normalization ===
var cleanup_cases = [
  ["The came", "The cat came"],
  ["The cat came", "The came"],
  ["111xxxabc", "111defxxx"],
  ["111xxxabcd", "111defxxx"],
  ["A word boundary is useful.", "A word boundary can be useful."],
];
for (var cleanup_index = 0; cleanup_index < cleanup_cases.length; cleanup_index++) {
  verify_diff(
    cleanup_cases[cleanup_index][0],
    cleanup_cases[cleanup_index][1],
    null,
    true,
    "cleanup-" + cleanup_index
  );
}

// === Test 12: Unicode-heavy edits with shared surrogate halves ===
var unicode_cases = [
  ["😀😀😀😀", "😀😃😀😄"],
  ["A😀B😃C😄D", "A😄B😃C😀D"],
  ["😀x😃y😄z", "😀x😄y😃z"],
  ["start 😀 middle 😃 end", "start 😄 middle 😀 end"],
];
for (var unicode_index = 0; unicode_index < unicode_cases.length; unicode_index++) {
  verify_diff(
    unicode_cases[unicode_index][0],
    unicode_cases[unicode_index][1],
    null,
    false,
    "unicode-" + unicode_index
  );
  verify_diff(
    unicode_cases[unicode_index][0],
    unicode_cases[unicode_index][1],
    null,
    true,
    "unicode-cleanup-" + unicode_index
  );
}

// === Test 13: deterministic fuzzing of the diff invariants ===
var fuzz_seed = 1357911;
function next_fuzz_number() {
  fuzz_seed = (fuzz_seed * 48271) % 2147483647;
  return fuzz_seed;
}
function make_fuzz_text(length) {
  var alphabet = "GATTACA";
  var result = "";
  for (var i = 0; i < length; i++) {
    result += alphabet.charAt(next_fuzz_number() % alphabet.length);
  }
  return result;
}
for (var fuzz_index = 0; fuzz_index < 64; fuzz_index++) {
  var fuzz_old = make_fuzz_text(24 + fuzz_index % 17);
  var fuzz_new = make_fuzz_text(24 + (fuzz_index * 3) % 17);
  verify_diff(fuzz_old, fuzz_new, null, false, "fuzz-" + fuzz_index);
  if (fuzz_index % 4 === 0) {
    var fuzz_cursor = next_fuzz_number() % (fuzz_old.length + 1);
    verify_diff(
      fuzz_old,
      fuzz_new,
      fuzz_cursor,
      true,
      "fuzz-cursor-" + fuzz_index
    );
  }
}

// === Test 14: multi-line source-file diffs ===
console.log(JSON.stringify(fast_diff(
  "function greet(name) {\n  const message = \"Hello, \" + name;\n  return message;\n}\n",
  "function greet(name) {\n  const message = \"Hi, \" + name;\n  return message;\n}\n"
)));
console.log(JSON.stringify(fast_diff(
  "const items = [1, 2, 3];\nconst total = items.reduce(sum, 0);\n",
  "const items = [1, 2, 3];\nconst total = items.reduce(sum, 0);\nconsole.log(total);\n"
)));
console.log(JSON.stringify(fast_diff(
  "for (var i = 0; i < items.length; i++) {\n  process(items[i]);\n  log(i);\n}\n",
  "for (var i = 0; i < items.length; i++) {\n  process(items[i]);\n}\n"
)));
console.log(JSON.stringify(fast_diff(
  "if (enabled) {\r\n  start();\r\n  render();\r\n}\r\n",
  "if (enabled) {\r\n  start();\r\n  update();\r\n}\r\n"
)));
console.log(JSON.stringify(fast_diff(
  "",
  "import { parse } from \"compiler\";\n\nexport function build(source) {\n  return parse(source);\n}\n"
)));

var complex_source_cases = [
  [
    "/* setup */\nfunction run() {\n\n  return 1;\n}\n",
    "/* setup */\nfunction run() {\n  // fast path\n  return 2;\n}\n",
  ],
  [
    "const first = read();\nconst second = transform(first);\nconst third = write(second);\nreturn third;\n",
    "const first = read();\nconst second = transform(first);\nreturn write(second);\n",
  ],
  [
    "function render(data) {\n  if (data.ready) {\n    draw(data);\n  } else {\n    retry(data);\n  }\n}\n",
    "function render(data) {\n  if (data.ready) {\n    draw(data);\n    flush(data);\n  } else {\n    retry(data);\n  }\n}\n",
  ],
  [
    "const a = 1;\nconst b = 2;\nconst c = 3;\nconst d = 4;\n",
    "const c = 3;\nconst a = 1;\nconst d = 4;\nconst b = 2;\n",
  ],
  [
    "line one\nline two\nline three\nline four\n",
    "line zero\nline one\nline three changed\nline four\nline five\n",
  ],
];
for (var source_index = 0; source_index < complex_source_cases.length; source_index++) {
  verify_diff(
    complex_source_cases[source_index][0],
    complex_source_cases[source_index][1],
    null,
    false,
    "source-" + source_index
  );
  verify_diff(
    complex_source_cases[source_index][0],
    complex_source_cases[source_index][1],
    null,
    true,
    "source-cleanup-" + source_index
  );
}

console.log("MULTILINE_SOURCE_CASES_OK");
console.log("ADVANCED_CASES_OK");

// === Tests 15-20: ported from the upstream fast-diff test.js ===
function parse_fast_diff_spec(spec) {
  var tuples = [];
  if (!spec) {
    return tuples;
  }
  var start = 0;
  while (start < spec.length) {
    var symbol = spec.charAt(start);
    var end = start + 1;
    while (end < spec.length && "+-=".indexOf(spec.charAt(end)) === -1) {
      end++;
    }
    var operation = symbol === "+"
      ? fast_diff.INSERT
      : symbol === "-"
      ? fast_diff.DELETE
      : fast_diff.EQUAL;
    tuples.push([operation, spec.substring(start + 1, end)]);
    start = end;
  }
  return tuples;
}

function assert_fast_diff_case(old_text, new_text, cursor_pos, cleanup, expected, label) {
  var actual = fast_diff(old_text, new_text, cursor_pos, cleanup);
  assert_valid_diff(old_text, new_text, actual, label);
  assert_same_diff(actual, expected, label);
}

function prepend_fast_diff_tuple(tuples, text) {
  if (tuples.length > 0 && tuples[0][0] === fast_diff.EQUAL) {
    return [[fast_diff.EQUAL, text + tuples[0][1]]].concat(tuples.slice(1));
  }
  return [[fast_diff.EQUAL, text]].concat(tuples);
}

function append_fast_diff_tuple(tuples, text) {
  var last_tuple = tuples[tuples.length - 1];
  if (last_tuple && last_tuple[0] === fast_diff.EQUAL) {
    return tuples.slice(0, -1).concat([
      [fast_diff.EQUAL, last_tuple[1] + text],
    ]);
  }
  return tuples.concat([[fast_diff.EQUAL, text]]);
}

function shift_fast_diff_cursor(cursor_info, amount) {
  if (typeof cursor_info === "number") {
    return cursor_info + amount;
  }
  return {
    oldRange: {
      index: cursor_info.oldRange.index + amount,
      length: cursor_info.oldRange.length,
    },
    newRange: {
      index: cursor_info.newRange.index + amount,
      length: cursor_info.newRange.length,
    },
  };
}

function run_fast_diff_cursor_case(data, case_index) {
  var old_text = data[0];
  var new_text = data[2];
  var old_range = typeof data[1] === "number"
    ? { index: data[1], length: 0 }
    : { index: data[1][0], length: data[1][1] - data[1][0] };
  var new_range = typeof data[3] === "number"
    ? { index: data[3], length: 0 }
    : data[3] === null
    ? null
    : { index: data[3][0], length: data[3][1] - data[3][0] };
  if (new_range === null && typeof data[1] !== "number") {
    throw new Error("invalid upstream cursor test " + case_index);
  }
  var cursor_info = new_range === null
    ? data[1]
    : { oldRange: old_range, newRange: new_range };
  var expected = parse_fast_diff_spec(data[4]);

  assert_fast_diff_case(
    old_text,
    new_text,
    cursor_info,
    false,
    expected,
    "upstream-cursor-" + case_index
  );
  assert_fast_diff_case(
    "x" + old_text,
    "x" + new_text,
    shift_fast_diff_cursor(cursor_info, 1),
    false,
    prepend_fast_diff_tuple(expected, "x"),
    "upstream-cursor-prepend-" + case_index
  );
  assert_fast_diff_case(
    old_text + "x",
    new_text + "x",
    cursor_info,
    false,
    append_fast_diff_tuple(expected, "x"),
    "upstream-cursor-append-" + case_index
  );
  assert_fast_diff_case(
    old_text,
    new_text,
    cursor_info,
    true,
    expected,
    "upstream-cursor-cleanup-" + case_index
  );
  assert_fast_diff_case(
    "x" + old_text,
    "x" + new_text,
    shift_fast_diff_cursor(cursor_info, 1),
    true,
    prepend_fast_diff_tuple(expected, "x"),
    "upstream-cursor-cleanup-prepend-" + case_index
  );
  assert_fast_diff_case(
    old_text + "x",
    new_text + "x",
    cursor_info,
    true,
    append_fast_diff_tuple(expected, "x"),
    "upstream-cursor-cleanup-append-" + case_index
  );
}

// tests/test.js regression cases
var upstream_fast_diff_regressions = [
  ["GAATAAAAAAAGATTAACAT", "AAAAACTTGTAATTAACAAC"],
  ["🔘🤘🔗🔗", "🔗🤗🤗__🤗🤘🤘🤗🔗🤘🔗"],
  ["🔗🤗🤗__🤗🤘🤘🤗🔗🤘🔗", "🤗🤘🔘"],
  ["🤘🤘🔘🔘_🔘🔗🤘🤗🤗__🔗🤘", "🤘🔘🤘🔗🤘🤘🔗🤗🤘🔘🔘"],
  [
    "🤗🤘🤗🔘🤘🔘🤗_🤗🔗🤘🤗_🤘🔗🤗🤘🔗🤘🤘🤘🔗🤗🔗🔗🔗🤗_🤘🔗🤗🤗🔘🤗🤗🤘🤗",
    "_🤗🤘_🤘🤘🔘🤗🔘🤘_🔘🤗🔗🔘🔗🤘🔗🤘🤗🔗🔗🔗🤘🔘_🤗🤘🤘🤘__🤘_🔘🤘🤘_🔗🤘🔘",
  ],
  ["🔗🤘🤗🔘🔘🤗", "🤘🤘🤘🤗🔘🔗🔗"],
  ["🔘_🔗🔗🔗🤗🔗", "🤘🤗🔗🤗_🤘🔘_"],
];
for (var regression_index = 0;
     regression_index < upstream_fast_diff_regressions.length;
     regression_index++) {
  verify_diff(
    upstream_fast_diff_regressions[regression_index][0],
    upstream_fast_diff_regressions[regression_index][1],
    null,
    false,
    "upstream-regression-" + regression_index
  );
}

// tests/test.js cursor cases
var upstream_fast_diff_cursor_cases = [
  ["", 0, "", null, ""],
  ["", 0, "a", null, "+a"],
  ["a", 0, "aa", null, "+a=a"],
  ["a", 1, "aa", null, "=a+a"],
  ["aa", 0, "aaa", null, "+a=aa"],
  ["aa", 1, "aaa", null, "=a+a=a"],
  ["aa", 2, "aaa", null, "=aa+a"],
  ["aaa", 0, "aaaa", null, "+a=aaa"],
  ["aaa", 1, "aaaa", null, "=a+a=aa"],
  ["aaa", 2, "aaaa", null, "=aa+a=a"],
  ["aaa", 3, "aaaa", null, "=aaa+a"],
  ["a", 0, "", null, "-a"],
  ["a", 1, "", null, "-a"],
  ["aa", 0, "a", null, "-a=a"],
  ["aa", 1, "a", null, "-a=a"],
  ["aa", 2, "a", null, "=a-a"],
  ["aaa", 0, "aa", null, "-a=aa"],
  ["aaa", 1, "aa", null, "-a=aa"],
  ["aaa", 2, "aa", null, "=a-a=a"],
  ["aaa", 3, "aa", null, "=aa-a"],
  ["", 0, "", 0, ""],
  ["", 0, "a", 1, "+a"],
  ["a", 0, "aa", 1, "+a=a"],
  ["a", 1, "aa", 2, "=a+a"],
  ["aa", 0, "aaa", 1, "+a=aa"],
  ["aa", 1, "aaa", 2, "=a+a=a"],
  ["aa", 2, "aaa", 3, "=aa+a"],
  ["aaa", 0, "aaaa", 1, "+a=aaa"],
  ["aaa", 1, "aaaa", 2, "=a+a=aa"],
  ["aaa", 2, "aaaa", 3, "=aa+a=a"],
  ["aaa", 3, "aaaa", 4, "=aaa+a"],
  ["a", 1, "", 0, "-a"],
  ["aa", 1, "a", 0, "-a=a"],
  ["aa", 2, "a", 1, "=a-a"],
  ["aaa", 1, "aa", 0, "-a=aa"],
  ["aaa", 2, "aa", 1, "=a-a=a"],
  ["aaa", 3, "aa", 2, "=aa-a"],
  ["a", 1, "", 0, "-a"],
  ["aa", 1, "a", 0, "-a=a"],
  ["aa", 2, "a", 1, "=a-a"],
  ["aaa", 1, "aa", 0, "-a=aa"],
  ["aaa", 2, "aa", 1, "=a-a=a"],
  ["aaa", 3, "aa", 2, "=aa-a"],
  ["aaa", 3, "aa", 0, "=aa-a"],
  ["12345", [1, 2], "15", 1, "=1-234=5"],
  ["12345", [1, 2], "a545", 1, "-123+a5=45"],
  ["a", 0, "", 0, "-a"],
  ["aa", 0, "a", 0, "-a=a"],
  ["aa", 1, "a", 1, "=a-a"],
  ["aaa", 0, "aa", 0, "-a=aa"],
  ["aaa", 1, "aa", 1, "=a-a=a"],
  ["aaa", 2, "aa", 2, "=aa-a"],
  ["bob", 0, "bobob", null, "+bo=bob"],
  ["bob", 1, "bobob", null, "=b+ob=ob"],
  ["bob", 2, "bobob", null, "=bo+bo=b"],
  ["bob", 3, "bobob", null, "=bob+ob"],
  ["bob", 0, "bobob", 2, "+bo=bob"],
  ["bob", 1, "bobob", 3, "=b+ob=ob"],
  ["bob", 2, "bobob", 4, "=bo+bo=b"],
  ["bob", 3, "bobob", 5, "=bob+ob"],
  ["bobob", 2, "bob", null, "-bo=bob"],
  ["bobob", 3, "bob", null, "=b-ob=ob"],
  ["bobob", 4, "bob", null, "=bo-bo=b"],
  ["bobob", 5, "bob", null, "=bob-ob"],
  ["bobob", 2, "bob", 0, "-bo=bob"],
  ["bobob", 3, "bob", 1, "=b-ob=ob"],
  ["bobob", 4, "bob", 2, "=bo-bo=b"],
  ["bobob", 5, "bob", 3, "=bob-ob"],
  ["bob", 1, "b", null, "=b-ob"],
  ["hello", [0, 5], "h", 1, "-hello+h"],
  ["yay", [0, 3], "y", 1, "-yay+y"],
  ["bobob", [1, 4], "bob", 2, "=b-obo+o=b"],
];
for (var cursor_case_index = 0;
     cursor_case_index < upstream_fast_diff_cursor_cases.length;
     cursor_case_index++) {
  run_fast_diff_cursor_case(
    upstream_fast_diff_cursor_cases[cursor_case_index],
    cursor_case_index
  );
}

// tests/test.js emoji cases
var upstream_fast_diff_emoji_cases = [
  ["🐶", "🐯", "-🐶+🐯"],
  ["👨🏽", "👩🏽", "-👨+👩=🏽"],
  ["👩🏼", "👩🏽", "=👩-🏼+🏽"],
  ["🍏🍎", "🍎", "-🍏=🍎"],
  ["🍎", "🍏🍎", "+🍏=🍎"],
];
for (var emoji_case_index = 0;
     emoji_case_index < upstream_fast_diff_emoji_cases.length;
     emoji_case_index++) {
  var emoji_case = upstream_fast_diff_emoji_cases[emoji_case_index];
  var emoji_expected = parse_fast_diff_spec(emoji_case[2]);
  assert_fast_diff_case(
    emoji_case[0],
    emoji_case[1],
    null,
    false,
    emoji_expected,
    "upstream-emoji-" + emoji_case_index
  );
  assert_fast_diff_case(
    "x" + emoji_case[0],
    "x" + emoji_case[1],
    null,
    false,
    prepend_fast_diff_tuple(emoji_expected, "x"),
    "upstream-emoji-prepend-" + emoji_case_index
  );
  assert_fast_diff_case(
    emoji_case[0] + "x",
    emoji_case[1] + "x",
    null,
    false,
    append_fast_diff_tuple(emoji_expected, "x"),
    "upstream-emoji-append-" + emoji_case_index
  );
}

// tests/test.js semantic cleanup cases
var upstream_fast_diff_cleanup_cases = [
  ["The came", "The cat came"],
  ["The cat came", "The came"],
  ["111xxxabc", "111defxxx"],
  ["111xxxabcd", "111defxxx"],
];
for (var cleanup_case_index = 0;
     cleanup_case_index < upstream_fast_diff_cleanup_cases.length;
     cleanup_case_index++) {
  verify_diff(
    upstream_fast_diff_cleanup_cases[cleanup_case_index][0],
    upstream_fast_diff_cleanup_cases[cleanup_case_index][1],
    null,
    true,
    "upstream-cleanup-" + cleanup_case_index
  );
}

// tests/test.js fuzz tests, made deterministic and bounded for the LJS suite.
var upstream_fast_diff_seed = 24681357;
function next_upstream_fast_diff_random() {
  upstream_fast_diff_seed =
    (upstream_fast_diff_seed * 48271) % 2147483647;
  return upstream_fast_diff_seed / 2147483647;
}
function make_upstream_fast_diff_text(length) {
  var alphabet = "GATTACA";
  var result = "";
  for (var i = 0; i < length; i++) {
    result += alphabet.charAt(
      Math.floor(next_upstream_fast_diff_random() * alphabet.length)
    );
  }
  return result;
}
var upstream_fast_diff_iterations = 128;
var upstream_fast_diff_strings = [];
for (var string_index = 0;
     string_index <= upstream_fast_diff_iterations;
     string_index++) {
  upstream_fast_diff_strings.push(make_upstream_fast_diff_text(100));
}
for (var fuzz_case_index = 0;
     fuzz_case_index < upstream_fast_diff_iterations;
     fuzz_case_index++) {
  verify_diff(
    upstream_fast_diff_strings[fuzz_case_index],
    upstream_fast_diff_strings[fuzz_case_index + 1],
    null,
    false,
    "upstream-fuzz-" + fuzz_case_index
  );
}
for (var cursor_fuzz_index = 0;
     cursor_fuzz_index < upstream_fast_diff_iterations;
     cursor_fuzz_index++) {
  var fuzz_old_text = upstream_fast_diff_strings[cursor_fuzz_index];
  var fuzz_cursor = Math.floor(
    next_upstream_fast_diff_random() * fuzz_old_text.length + 1
  );
  verify_diff(
    fuzz_old_text,
    upstream_fast_diff_strings[cursor_fuzz_index + 1],
    fuzz_cursor,
    false,
    "upstream-fuzz-cursor-" + cursor_fuzz_index
  );
}
console.log("OFFICIAL_FAST_DIFF_TESTS_OK");
console.log("FAST_DIFF_DONE");
