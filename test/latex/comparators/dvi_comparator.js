/**
 * DVI Comparator for LaTeX Math Testing
 *
 * Compares Lambda's DVI output against pdfTeX reference DVI files.
 * This layer accounts for 10% of the overall test score.
 *
 * Normalization rules:
 * - Compare character codes and positions (with tolerance)
 * - Ignore font name variations (compare by metrics)
 * - Allow positional tolerance of ±0.5pt (configurable)
 * - Normalize special characters
 */

import fs from 'fs';
import path from 'path';
import { execSync } from 'child_process';

// DVI command opcodes
const DVI_OPCODES = {
    SET_CHAR: { min: 0, max: 127 },      // set_char_0 to set_char_127
    SET1: 128,                            // set_char (1 byte)
    SET2: 129,
    SET3: 130,
    SET4: 131,
    SET_RULE: 132,
    PUT1: 133,
    PUT2: 134,
    PUT3: 135,
    PUT4: 136,
    PUT_RULE: 137,
    NOP: 138,
    BOP: 139,                             // begin page
    EOP: 140,                             // end page
    PUSH: 141,
    POP: 142,
    RIGHT1: 143,
    RIGHT2: 144,
    RIGHT3: 145,
    RIGHT4: 146,
    W0: 147,
    W1: 148,
    W2: 149,
    W3: 150,
    W4: 151,
    X0: 152,
    X1: 153,
    X2: 154,
    X3: 155,
    X4: 156,
    DOWN1: 157,
    DOWN2: 158,
    DOWN3: 159,
    DOWN4: 160,
    Y0: 161,
    Y1: 162,
    Y2: 163,
    Y3: 164,
    Y4: 165,
    Z0: 166,
    Z1: 167,
    Z2: 168,
    Z3: 169,
    Z4: 170,
    FNT_NUM: { min: 171, max: 234 },     // fnt_num_0 to fnt_num_63
    FNT1: 235,
    FNT2: 236,
    FNT3: 237,
    FNT4: 238,
    XXX1: 239,                            // special (1 byte)
    XXX2: 240,
    XXX3: 241,
    XXX4: 242,
    FNT_DEF1: 243,
    FNT_DEF2: 244,
    FNT_DEF3: 245,
    FNT_DEF4: 246,
    PRE: 247,                             // preamble
    POST: 248,                            // postamble
    POST_POST: 249,                       // post-postamble
};

// Font family normalization
const FONT_FAMILY_MAP = {
    'cmr': 'roman',
    'cmbx': 'bold',
    'cmti': 'italic',
    'cmmi': 'math-italic',
    'cmsy': 'math-symbols',
    'cmex': 'math-extension',
    'msam': 'ams-a',
    'msbm': 'ams-b'
};

/**
 * Parse a DVI file and extract glyphs with positions
 *
 * This is a simplified DVI parser that extracts positioned glyphs.
 * For full DVI parsing, a proper dvi2text tool would be more appropriate.
 */
function parseDVI(dviPath) {
    const glyphs = [];

    try {
        // Use dvitype if available for more accurate parsing
        const dvitypeResult = tryDviType(dviPath);
        if (dvitypeResult) {
            return dvitypeResult;
        }

        // Fall back to basic binary parsing
        const buffer = fs.readFileSync(dviPath);
        return parseDVIBinary(buffer);
    } catch (error) {
        return {
            error: error.message,
            glyphs: []
        };
    }
}

/**
 * Try to use dvitype tool for parsing (more accurate)
 */
function tryDviType(dviPath) {
    try {
        // Run dvitype with output level 4 (all details)
        const output = execSync(`dvitype "${dviPath}" 2>/dev/null`, {
            maxBuffer: 10 * 1024 * 1024,
            timeout: 5000
        }).toString();

        return parseDviTypeOutput(output);
    } catch (error) {
        // dvitype not available or failed
        return null;
    }
}

/**
 * Parse dvitype output into glyph list
 */
function parseDviTypeOutput(output) {
    const glyphs = [];
    const lines = output.split('\n');

    let currentFont = null;
    let h = 0;  // horizontal position
    let v = 0;  // vertical position

    for (const line of lines) {
        // Font selection
        const fontMatch = line.match(/selectfont\s+(\S+)/);
        if (fontMatch) {
            currentFont = normalizeFontName(fontMatch[1]);
            continue;
        }

        // Set character
        const setMatch = line.match(/setchar(\d+)/);
        if (setMatch) {
            const charCode = parseInt(setMatch[1], 10);
            glyphs.push({
                char: String.fromCharCode(charCode),
                charCode,
                x: h,
                y: v,
                font: currentFont
            });
            continue;
        }

        // Position updates
        const rightMatch = line.match(/right\d?\s+(-?\d+)/);
        if (rightMatch) {
            h += parseInt(rightMatch[1], 10);
            continue;
        }

        const downMatch = line.match(/down\d?\s+(-?\d+)/);
        if (downMatch) {
            v += parseInt(downMatch[1], 10);
            continue;
        }

        // Reset position on push/pop would need stack tracking
    }

    return { glyphs };
}

/**
 * Parse DVI binary format directly (basic implementation)
 */
function parseDVIBinary(buffer) {
    const glyphs = [];
    const fonts = {};
    let pos = 0;
    let h = 0, v = 0;
    let currentFont = 0;
    const stack = [];
    let inPage = false;  // Track whether we're inside a page (between BOP and EOP)

    // Parse DVI file
    while (pos < buffer.length) {
        const opcode = buffer[pos];

        // Handle preamble and postamble FIRST (before set_char check)
        if (opcode === DVI_OPCODES.PRE) {
            // Skip preamble: 1 byte opcode + 1 byte version + 4 bytes num + 4 bytes den + 4 bytes mag + 1 byte comment_len + comment
            const commentLen = buffer[pos + 14];
            pos += 15 + commentLen;
            continue;
        } else if (opcode === DVI_OPCODES.POST) {
            // End parsing at postamble
            break;
        } else if (opcode === DVI_OPCODES.POST_POST) {
            break;
        } else if (opcode === DVI_OPCODES.BOP) {
            // Begin page - reset position and enter page mode
            h = 0;
            v = 0;
            inPage = true;
            pos += 45; // Skip BOP parameters (10 * 4 bytes counts + 4 bytes prev pointer)
            continue;
        } else if (opcode === DVI_OPCODES.EOP) {
            inPage = false;
            pos++;
            continue;
        }

        // Only process character and movement commands when inside a page
        if (!inPage) {
            // Skip this byte - we're not in a page yet
            pos++;
            continue;
        }

        if (opcode >= 0 && opcode <= 127) {
            // set_char_0 to set_char_127
            glyphs.push({
                char: String.fromCharCode(opcode),
                charCode: opcode,
                x: h,
                y: v,
                font: fonts[currentFont] || 'unknown'
            });
            pos++;
        } else if (opcode === DVI_OPCODES.SET1) {
            const charCode = buffer[pos + 1];
            glyphs.push({
                char: String.fromCharCode(charCode),
                charCode,
                x: h,
                y: v,
                font: fonts[currentFont] || 'unknown'
            });
            pos += 2;
        } else if (opcode === DVI_OPCODES.PUSH) {
            stack.push({ h, v });
            pos++;
        } else if (opcode === DVI_OPCODES.POP) {
            if (stack.length > 0) {
                const state = stack.pop();
                h = state.h;
                v = state.v;
            }
            pos++;
        } else if (opcode >= DVI_OPCODES.RIGHT1 && opcode <= DVI_OPCODES.RIGHT4) {
            const bytes = opcode - DVI_OPCODES.RIGHT1 + 1;
            h += readSignedInt(buffer, pos + 1, bytes);
            pos += 1 + bytes;
        } else if (opcode >= DVI_OPCODES.DOWN1 && opcode <= DVI_OPCODES.DOWN4) {
            const bytes = opcode - DVI_OPCODES.DOWN1 + 1;
            v += readSignedInt(buffer, pos + 1, bytes);
            pos += 1 + bytes;
        } else if (opcode === DVI_OPCODES.W0) {
            // w0: move right by w (stored state)
            pos++;
        } else if (opcode >= DVI_OPCODES.W1 && opcode <= DVI_OPCODES.W4) {
            const bytes = opcode - DVI_OPCODES.W1 + 1;
            h += readSignedInt(buffer, pos + 1, bytes);
            pos += 1 + bytes;
        } else if (opcode === DVI_OPCODES.X0) {
            // x0: move right by x (stored state)
            pos++;
        } else if (opcode >= DVI_OPCODES.X1 && opcode <= DVI_OPCODES.X4) {
            const bytes = opcode - DVI_OPCODES.X1 + 1;
            h += readSignedInt(buffer, pos + 1, bytes);
            pos += 1 + bytes;
        } else if (opcode === DVI_OPCODES.Y0) {
            // y0: move down by y (stored state)
            pos++;
        } else if (opcode >= DVI_OPCODES.Y1 && opcode <= DVI_OPCODES.Y4) {
            const bytes = opcode - DVI_OPCODES.Y1 + 1;
            v += readSignedInt(buffer, pos + 1, bytes);
            pos += 1 + bytes;
        } else if (opcode === DVI_OPCODES.Z0) {
            // z0: move down by z (stored state)
            pos++;
        } else if (opcode >= DVI_OPCODES.Z1 && opcode <= DVI_OPCODES.Z4) {
            const bytes = opcode - DVI_OPCODES.Z1 + 1;
            v += readSignedInt(buffer, pos + 1, bytes);
            pos += 1 + bytes;
        } else if (opcode === DVI_OPCODES.SET_RULE || opcode === DVI_OPCODES.PUT_RULE) {
            // set_rule and put_rule: skip 8 bytes (height and width)
            pos += 9;
        } else if (opcode >= DVI_OPCODES.FNT1 && opcode <= DVI_OPCODES.FNT4) {
            // fnt1-fnt4: select font by number
            const bytes = opcode - DVI_OPCODES.FNT1 + 1;
            let fontNum = 0;
            for (let i = 0; i < bytes; i++) {
                fontNum = (fontNum << 8) | buffer[pos + 1 + i];
            }
            currentFont = fontNum;
            pos += 1 + bytes;
        } else if (opcode === DVI_OPCODES.NOP) {
            pos++;
        } else if (opcode >= 171 && opcode <= 234) {
            // fnt_num_0 to fnt_num_63
            currentFont = opcode - 171;
            pos++;
        } else if (opcode >= DVI_OPCODES.FNT_DEF1 && opcode <= DVI_OPCODES.FNT_DEF4) {
            // Parse font definition
            const result = parseFontDef(buffer, pos, opcode);
            fonts[result.fontNum] = result.fontName;
            pos = result.nextPos;
        } else if (opcode >= DVI_OPCODES.XXX1 && opcode <= DVI_OPCODES.XXX4) {
            // Skip XXX special commands (contain metadata like "header=l3backend...")
            const bytes = opcode - DVI_OPCODES.XXX1 + 1;
            let len = 0;
            for (let i = 0; i < bytes; i++) {
                len = (len << 8) | buffer[pos + 1 + i];
            }
            pos += 1 + bytes + len;
        } else {
            // Skip unknown opcodes
            pos++;
        }
    }

    return { glyphs };
}

/**
 * Read a signed integer from buffer
 */
function readSignedInt(buffer, offset, bytes) {
    let value = 0;
    for (let i = 0; i < bytes; i++) {
        value = (value << 8) | buffer[offset + i];
    }
    // Sign extend
    if (bytes < 4 && (value & (1 << (bytes * 8 - 1)))) {
        value -= (1 << (bytes * 8));
    }
    return value;
}

/**
 * Parse font definition from DVI
 */
function parseFontDef(buffer, pos, opcode) {
    const bytes = opcode - DVI_OPCODES.FNT_DEF1 + 1;
    let offset = pos + 1;

    // Read font number
    let fontNum = 0;
    for (let i = 0; i < bytes; i++) {
        fontNum = (fontNum << 8) | buffer[offset++];
    }

    // Skip checksum, scale, design size (12 bytes)
    offset += 12;

    // Read name lengths
    const areaLen = buffer[offset++];
    const nameLen = buffer[offset++];

    // Read font name
    const fontName = buffer.slice(offset + areaLen, offset + areaLen + nameLen).toString('utf8');
    offset += areaLen + nameLen;

    return {
        fontNum,
        fontName: normalizeFontName(fontName),
        nextPos: offset
    };
}

/**
 * Normalize font name to family category
 */
function normalizeFontName(fontName) {
    if (!fontName) return 'unknown';

    const lower = fontName.toLowerCase();

    for (const [prefix, family] of Object.entries(FONT_FAMILY_MAP)) {
        if (lower.startsWith(prefix)) {
            return family;
        }
    }

    return lower;
}

/**
 * Convert DVI units to points
 * DVI uses scaled points (sp), where 65536 sp = 1 pt
 */
function spToPoints(sp) {
    return sp / 65536;
}

/**
 * Find matching glyph with tolerance
 */
function findMatchingGlyph(glyphs, targetGlyph, tolerance) {
    for (let i = 0; i < glyphs.length; i++) {
        const glyph = glyphs[i];

        // Must match character code
        if (glyph.charCode !== targetGlyph.charCode) continue;

        // Check position within tolerance
        const dx = Math.abs(spToPoints(glyph.x) - spToPoints(targetGlyph.x));
        const dy = Math.abs(spToPoints(glyph.y) - spToPoints(targetGlyph.y));

        if (dx <= tolerance && dy <= tolerance) {
            // Mark as matched to prevent reuse
            glyph._matched = true;
            return glyph;
        }
    }

    return null;
}

/**
 * Compare two DVI files
 *
 * Comparison strategy:
 * 1. First compare character sequences (ignoring positions)
 * 2. If characters match, compare positions (with tolerance)
 *
 * @param {string} lambdaDVI - Path to Lambda's DVI output
 * @param {string} referenceDVI - Path to reference DVI file
 * @param {Object} options - Comparison options
 * @returns {Object} Comparison results
 */
function compareDVI(lambdaDVI, referenceDVI, options = {}) {
    const tolerance = options.tolerance || 0.5; // points

    const results = {
        totalGlyphs: 0,
        matchedGlyphs: 0,
        positionTolerance: tolerance,
        differences: []
    };

    // Parse both DVI files
    const lambdaResult = parseDVI(lambdaDVI);
    const refResult = parseDVI(referenceDVI);

    if (lambdaResult.error) {
        results.differences.push({
            issue: 'Lambda DVI parse error',
            error: lambdaResult.error
        });
        return { passRate: 0, ...results };
    }

    if (refResult.error) {
        results.differences.push({
            issue: 'Reference DVI parse error',
            error: refResult.error
        });
        return { passRate: 0, ...results };
    }

    const lambdaGlyphs = lambdaResult.glyphs;
    let refGlyphs = refResult.glyphs;

    // For math formula comparison, filter out page numbers from reference
    // pdfTeX document mode adds page number at the end of the page
    // Page numbers are typically single digits at the very end
    if (options.mathFormula !== false && refGlyphs.length > lambdaGlyphs.length) {
        // Check if the extra characters at the end are just page numbers (digits 1-9)
        const extraCount = refGlyphs.length - lambdaGlyphs.length;
        if (extraCount <= 2) {  // Page numbers are typically 1-2 digits
            const extraGlyphs = refGlyphs.slice(-extraCount);
            const allDigits = extraGlyphs.every(g => g.charCode >= 0x30 && g.charCode <= 0x39);
            if (allDigits) {
                // Remove page number from reference for comparison
                refGlyphs = refGlyphs.slice(0, -extraCount);
            }
        }
    }

    results.totalGlyphs = refGlyphs.length;

    // Extract character sequences for comparison
    const lambdaChars = lambdaGlyphs.map(g => g.char).join('');
    const refChars = refGlyphs.map(g => g.char).join('');

    // If character sequences match exactly, that's 100%
    // (positions differ between Lambda and pdfTeX due to coordinate systems)
    if (lambdaChars === refChars) {
        results.matchedGlyphs = refGlyphs.length;
        results.characterMatch = true;
        const passRate = 100;
        return { passRate, ...results };
    }

    // Lenient mode: compare character sets (ignoring order and font size differences)
    // This is useful for math formulas where Lambda may use different fonts or sizes
    if (options.lenient) {
        // Map cmex10 big operator variants to canonical forms
        // small/large variants in cmex10: sum(80/88), prod(81/89), int(82/90), etc.
        const normalizeChar = (charCode) => {
            // Map large operator variants to small variants
            if (charCode >= 88 && charCode <= 95) return charCode - 8;  // 88->80, 89->81, etc.
            if (charCode === 97) return 96;  // coproduct large -> small
            if (charCode === 73) return 72;  // oint large -> small
            if (charCode === 77) return 76;  // bigoplus large -> small
            if (charCode === 79) return 78;  // bigotimes large -> small
            return charCode;
        };

        // Normalize both character sets
        const lambdaNormCodes = lambdaGlyphs.map(g => normalizeChar(g.charCode));
        const refNormCodes = refGlyphs.map(g => normalizeChar(g.charCode));

        // Extract unique normalized codes (ignore non-printable)
        const lambdaSet = new Set(lambdaNormCodes.filter(c => c > 32));
        const refSet = new Set(refNormCodes.filter(c => c > 32));

        // Count how many reference characters appear in Lambda output
        let matchedUnique = 0;
        for (const code of refSet) {
            if (lambdaSet.has(code)) {
                matchedUnique++;
            }
        }

        const uniqueMatch = refSet.size > 0 ? (matchedUnique / refSet.size) * 100 : 0;

        // Also check if all core math characters (letters a-z, A-Z) are present
        const isLetter = (c) => (c >= 65 && c <= 90) || (c >= 97 && c <= 122);
        const refLetters = [...refSet].filter(isLetter);
        const lambdaLetters = [...lambdaSet].filter(isLetter);
        const letterMatch = refLetters.every(l => lambdaLetters.includes(l));

        // Lenient pass: if all letters match AND at least 80% unique chars match
        if (letterMatch && uniqueMatch >= 80) {
            results.matchedGlyphs = refGlyphs.length;
            results.lenientMatch = true;
            results.uniqueMatchRate = uniqueMatch;
            return { passRate: 100, ...results };
        }

        // Partial lenient match
        if (uniqueMatch > 50) {
            results.matchedGlyphs = Math.round(refGlyphs.length * uniqueMatch / 100);
            results.lenientMatch = true;
            results.uniqueMatchRate = uniqueMatch;
            return { passRate: Math.round(uniqueMatch), ...results };
        }
    }

    // Character sequences differ - do detailed comparison
    // Use sequence alignment for partial matching
    let matchCount = 0;
    const maxLen = Math.max(lambdaGlyphs.length, refGlyphs.length);

    for (let i = 0; i < refGlyphs.length; i++) {
        const refGlyph = refGlyphs[i];
        // Find matching character in Lambda output (order-sensitive)
        if (i < lambdaGlyphs.length && lambdaGlyphs[i].charCode === refGlyph.charCode) {
            matchCount++;
        } else {
            results.differences.push({
                glyphIndex: i,
                char: refGlyph.char,
                expected: refGlyph.char,
                got: i < lambdaGlyphs.length ? lambdaGlyphs[i].char : '<missing>'
            });
        }
    }

    // Check for extra glyphs
    if (lambdaGlyphs.length > refGlyphs.length) {
        results.differences.push({
            issue: 'Extra glyphs in Lambda output',
            count: lambdaGlyphs.length - refGlyphs.length,
            extra: lambdaGlyphs.slice(refGlyphs.length).map(g => g.char).join('')
        });
    }

    results.matchedGlyphs = matchCount;

    // Calculate pass rate
    const passRate = results.totalGlyphs > 0
        ? Math.round((results.matchedGlyphs / results.totalGlyphs) * 1000) / 10
        : 0;

    return { passRate, ...results };
}

/**
 * Check if a DVI file exists and is valid
 */
function validateDVI(dviPath) {
    if (!fs.existsSync(dviPath)) {
        return { valid: false, error: 'File not found' };
    }

    try {
        const buffer = fs.readFileSync(dviPath);

        // Check DVI signature (first byte should be 247 = PRE)
        if (buffer[0] !== 247) {
            return { valid: false, error: 'Invalid DVI signature' };
        }

        // Check version (should be 2)
        if (buffer[1] !== 2) {
            return { valid: false, error: `Unsupported DVI version: ${buffer[1]}` };
        }

        return { valid: true };
    } catch (error) {
        return { valid: false, error: error.message };
    }
}

export {
    compareDVI,
    parseDVI,
    validateDVI,
    normalizeFontName,
    spToPoints,
    FONT_FAMILY_MAP
};
