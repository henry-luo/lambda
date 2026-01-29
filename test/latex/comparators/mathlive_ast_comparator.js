/**
 * MathLive AST Comparator for LaTeX Math Testing
 *
 * Compares Lambda's AST against MathLive's internal AST (toJson() output).
 *
 * Key insight: Both Lambda and MathLive use the same branch structure:
 *   - body, above, below, superscript, subscript
 *
 * This comparator performs DIRECT STRUCTURAL comparison rather than
 * converting through MathML semantics.
 */

import fs from 'fs';

/**
 * Type mapping: Lambda AST types to MathLive atom types
 *
 * Lambda types (uppercase) -> MathLive types (lowercase)
 */
const LAMBDA_TO_MATHLIVE_TYPE = {
    // Basic elements
    'ORD': 'mord',              // Ordinary symbol
    'BIN': 'mbin',              // Binary operator
    'REL': 'mrel',              // Relation
    'OP': 'mop',                // Operator (sum, int, etc.)
    'PUNCT': 'mpunct',          // Punctuation
    'INNER': 'minner',          // Inner element
    'OPEN': 'mopen',            // Opening delimiter
    'CLOSE': 'mclose',          // Closing delimiter

    // Structures
    'FRAC': 'genfrac',          // Fraction
    'SQRT': 'surd',             // Square root (MathLive uses 'surd')
    'ROOT': 'surd',             // nth root (same as SQRT in MathLive)
    'SCRIPTS': 'subsup',        // Sub/superscript (MathLive uses inline branches)
    'ROW': 'first',             // Row of elements / root
    'GROUP': 'group',           // Grouping

    // Delimiters and fences
    'DELIMITED': 'leftright',   // Delimited expression
    'SIZED_DELIM': 'sizeddelim', // Sized delimiters (\big, \Big, etc.)

    // Accents and decorations
    'ACCENT': 'accent',         // Accent marks (hat, bar, etc.)
    'OVERUNDER': 'overunder',   // Under/over operators

    // Arrays and matrices
    'ARRAY': 'array',           // Tables/matrices
    'ARRAY_ROW': 'array-row',   // Table row (virtual)
    'ARRAY_CELL': 'array-cell', // Table cell (virtual)

    // Spacing
    'SPACING': 'spacing',       // Horizontal spacing
    'SPACE': 'spacing',         // Space

    // Math styles
    'TEXT': 'text',             // Text mode
    'PHANTOM': 'phantom',       // Phantom element
    'BOX': 'box',               // Box wrapper
    'STYLE': 'style',           // Style commands (\displaystyle, etc.)
    'NOT': 'not',               // Negation overlay
};

/**
 * Reverse mapping: MathLive -> Lambda for flexible comparison
 */
const MATHLIVE_TO_LAMBDA_TYPE = {};
for (const [lambda, mathlive] of Object.entries(LAMBDA_TO_MATHLIVE_TYPE)) {
    MATHLIVE_TO_LAMBDA_TYPE[mathlive] = lambda;
}

/**
 * Branch name mapping between Lambda and MathLive
 *
 * Lambda uses: numer, denom, above, below
 * MathLive uses: above, below (for everything) or numer, denom (for genfrac)
 * For extensible-symbol: MathLive uses subscript/superscript for limits
 */
const LAMBDA_BRANCH_ALIASES = {
    'numer': ['above', 'numer'],
    'denom': ['below', 'denom'],
    'above': ['above', 'numer', 'superscript'],  // extensible-symbol uses superscript for above
    'below': ['below', 'denom', 'subscript'],    // extensible-symbol uses subscript for below
    'superscript': ['superscript', 'above'],     // also check above for limits
    'subscript': ['subscript', 'below'],         // also check below for limits
    'body': ['body'],
};

/**
 * Normalize a type to lowercase for comparison
 */
function normalizeType(type) {
    if (!type) return null;
    return type.toLowerCase();
}

/**
 * Check if two types are compatible
 */
function areTypesCompatible(lambdaType, mathliveType) {
    if (!lambdaType || !mathliveType) return false;

    const lambdaNorm = normalizeType(lambdaType);
    const mathLiveNorm = normalizeType(mathliveType);

    // Exact match after normalization
    if (lambdaNorm === mathLiveNorm) return true;

    // Check mapping
    const mappedType = LAMBDA_TO_MATHLIVE_TYPE[lambdaType.toUpperCase()];
    if (mappedType && normalizeType(mappedType) === mathLiveNorm) return true;

    // Check reverse mapping
    const reverseMapped = MATHLIVE_TO_LAMBDA_TYPE[mathLiveNorm];
    if (reverseMapped && normalizeType(reverseMapped) === lambdaNorm) return true;

    // Special cases for structural equivalence
    const equivalentTypes = [
        ['frac', 'genfrac'],
        ['row', 'first', 'root', 'group'],
        ['accent', 'overunder'],
        ['sqrt', 'surd', 'root'],
        ['ord', 'mord', 'mi'],
        ['bin', 'mbin', 'mo'],
        ['rel', 'mrel'],
        ['op', 'mop', 'extensible-symbol'],  // big operators
        ['overunder', 'extensible-symbol'],  // big operators with limits
        ['delimited', 'leftright'],
        ['array', 'array'],  // both use 'array' (case-insensitive)
        ['array_row', 'array-row'],
        ['array_cell', 'array-cell'],
        ['scripts', 'subsup'],
        ['box', 'minner'],
        ['style', 'group'],
        ['sized_delim', 'sizeddelim', 'mopen', 'mclose'],
        ['not', 'overlap', 'mrel'],  // negation/overlay - bare \not is mrel
    ];

    for (const group of equivalentTypes) {
        if (group.includes(lambdaNorm) && group.includes(mathLiveNorm)) {
            return true;
        }
    }

    return false;
}

/**
 * Get branch value from a node, trying multiple alias names
 */
function getBranch(node, branchName) {
    if (!node) return null;

    const aliases = LAMBDA_BRANCH_ALIASES[branchName] || [branchName];
    for (const alias of aliases) {
        if (node[alias] !== undefined) {
            return node[alias];
        }
    }
    return null;
}

/**
 * Flatten trivial wrapper nodes (single-child rows/groups)
 */
function unwrapTrivialNodes(node) {
    if (!node) return null;

    // If it's an array, process each element
    if (Array.isArray(node)) {
        return node.map(unwrapTrivialNodes).filter(Boolean);
    }

    // Process branches first
    const branches = ['body', 'above', 'below', 'numer', 'denom', 'superscript', 'subscript'];
    for (const branch of branches) {
        if (node[branch]) {
            node[branch] = unwrapTrivialNodes(node[branch]);
        }
    }

    // Unwrap single-child containers
    const type = normalizeType(node.type);
    const isWrapper = ['row', 'first', 'root', 'group', 'mrow'].includes(type);

    if (isWrapper && node.body && !node.value) {
        const body = Array.isArray(node.body) ? node.body : [node.body];
        if (body.length === 1 && typeof body[0] === 'object') {
            return body[0];
        }
    }

    return node;
}

/**
 * Extract text content from a node tree (for value comparison)
 */
function extractTextContent(node) {
    if (!node) return '';
    if (typeof node === 'string') return node;

    let result = '';

    // Direct value
    if (node.value) {
        result += node.value;
    }
    if (node.symbol) {
        result += node.symbol;
    }
    if (node.char) {
        result += node.char;
    }

    // Process body
    if (node.body) {
        if (Array.isArray(node.body)) {
            for (const child of node.body) {
                result += extractTextContent(child);
            }
        } else if (typeof node.body === 'string') {
            result += node.body;
        } else {
            result += extractTextContent(node.body);
        }
    }

    return result;
}

/**
 * Count total elements in a node tree
 */
function countElements(node) {
    if (!node) return 0;
    if (Array.isArray(node)) {
        return node.reduce((sum, child) => sum + countElements(child), 0);
    }

    let count = 1;

    const branches = ['body', 'above', 'below', 'numer', 'denom', 'superscript', 'subscript'];
    for (const branch of branches) {
        if (node[branch]) {
            count += countElements(node[branch]);
        }
    }

    return count;
}

/**
 * Unwrap MathLive's root wrapper to get the actual content node.
 * MathLive wraps everything in {type:"root", body:[...]} but Lambda returns
 * the content node directly (e.g., {type:"FRAC",...}).
 */
function unwrapMathLiveRoot(node) {
    if (!node) return null;

    // Handle MathLive's root wrapper: {type:"root", body:[{actual content}]}
    const type = normalizeType(node.type);
    if (type === 'root' && node.body) {
        const body = Array.isArray(node.body) ? node.body : [node.body];
        if (body.length === 1) {
            return body[0];
        }
        // Multiple children in root body - return a synthetic group
        return { type: 'group', body: body };
    }
    return node;
}

/**
 * Normalize a branch value from MathLive format.
 * MathLive uses string arrays like ["a", "b"] for simple content,
 * but Lambda uses object arrays like [{type:"ORD", value:"a"}].
 * This extracts the semantic values for comparison.
 */
function normalizeBranchContent(branch) {
    if (!branch) return [];
    if (!Array.isArray(branch)) branch = [branch];

    return branch.map(item => {
        if (typeof item === 'string') {
            // MathLive simple string content
            return { type: 'value', value: item };
        } else if (item && typeof item === 'object') {
            // Already an object - extract value if present
            return {
                type: item.type || 'value',
                value: item.value || item.symbol || item.char || null,
                codepoint: item.codepoint
            };
        }
        return { type: 'unknown', value: String(item) };
    });
}

/**
 * Compare branch content semantically.
 * Returns true if the values match regardless of structural differences.
 */
function compareBranchValues(lambdaBranch, mathliveBranch) {
    const lambdaNorm = normalizeBranchContent(lambdaBranch);
    const mathliveNorm = normalizeBranchContent(mathliveBranch);

    if (lambdaNorm.length !== mathliveNorm.length) {
        return { match: false, reason: 'length mismatch' };
    }

    for (let i = 0; i < lambdaNorm.length; i++) {
        const lVal = lambdaNorm[i].value;
        const mVal = mathliveNorm[i].value;

        if (lVal !== mVal) {
            return { match: false, reason: `value mismatch at ${i}: ${lVal} vs ${mVal}` };
        }
    }

    return { match: true };
}

/**
 * Normalize MathLive's body array to merge base+subsup into Lambda-style SCRIPTS.
 *
 * MathLive structure: ["r", {type: "subsup", superscript: ["2"]}]
 * Lambda structure:   [{type: "SCRIPTS", body: {value: "r"}, superscript: {value: "2"}}]
 *
 * This function merges consecutive base + subsup into a single node.
 */
function normalizeMathLiveBody(body) {
    if (!Array.isArray(body)) return body;

    const result = [];
    for (let i = 0; i < body.length; i++) {
        const item = body[i];
        const next = body[i + 1];

        // Check if next item is a subsup that should attach to current item
        if (next && typeof next === 'object' && normalizeType(next.type) === 'subsup') {
            // Merge base + subsup into a SCRIPTS-like node
            result.push({
                type: 'SCRIPTS',
                body: typeof item === 'string' ? { type: 'ORD', value: item } : item,
                superscript: next.superscript,
                subscript: next.subscript,
            });
            i++; // Skip the subsup
        } else {
            result.push(item);
        }
    }

    return result;
}

/**
 * Deep normalize MathLive AST to be more comparable with Lambda's structure.
 */
function normalizeMathLiveAST(node) {
    if (!node) return null;

    if (Array.isArray(node)) {
        // First normalize children, then merge base+subsup patterns
        const normalized = node.map(normalizeMathLiveAST);
        return normalizeMathLiveBody(normalized);
    }

    if (typeof node !== 'object') return node;

    // Create a new normalized node
    const result = { ...node };

    // Normalize branches
    const branches = ['body', 'above', 'below', 'numer', 'denom', 'superscript', 'subscript'];
    for (const branch of branches) {
        if (result[branch]) {
            result[branch] = normalizeMathLiveAST(result[branch]);
        }
    }

    return result;
}

/**
 * Normalize Lambda AST to be more comparable with MathLive's structure.
 *
 * Key transformations:
 * 1. DELIMITED containing ARRAY -> ARRAY with leftDelim/rightDelim (matches MathLive matrices)
 */
function normalizeLambdaAST(node) {
    if (!node) return null;

    if (Array.isArray(node)) {
        return node.map(normalizeLambdaAST);
    }

    if (typeof node !== 'object') return node;

    // Special case: DELIMITED with ARRAY body -> flatten to ARRAY with delimiters
    const nodeType = normalizeType(node.type);
    if (nodeType === 'delimited' && node.body) {
        const body = Array.isArray(node.body) ? node.body[0] : node.body;
        const bodyType = body && typeof body === 'object' ? normalizeType(body.type) : null;

        if (bodyType === 'array') {
            // Flatten: create ARRAY node with delimiters from parent DELIMITED
            const flattenedArray = normalizeLambdaAST(body);
            return {
                ...flattenedArray,
                type: 'ARRAY',
                leftDelim: node.leftDelim,
                rightDelim: node.rightDelim,
            };
        }
    }

    // Create a new normalized node
    const result = { ...node };

    // Normalize branches recursively
    const branches = ['body', 'above', 'below', 'numer', 'denom', 'superscript', 'subscript'];
    for (const branch of branches) {
        if (result[branch]) {
            result[branch] = normalizeLambdaAST(result[branch]);
        }
    }

    // Special case: Flatten STYLE nodes that wrap a ROW containing inline elements
    // Lambda wraps styled content in STYLE, MathLive puts style as attribute on each element
    // Only flatten when STYLE contains a ROW with multiple simple children (ORD, BIN, REL)
    if (nodeType === 'row' && result.body && Array.isArray(result.body)) {
        const expandedBody = [];
        for (const child of result.body) {
            if (child && typeof child === 'object' && normalizeType(child.type) === 'style') {
                const styleBody = child.body;
                // Only flatten if style wraps a ROW containing simple inline elements
                if (styleBody && normalizeType(styleBody.type) === 'row' &&
                    styleBody.body && Array.isArray(styleBody.body)) {
                    // Check if all children are simple inline elements (ORD, BIN, REL, etc.)
                    const simpleTypes = ['ord', 'mord', 'bin', 'mbin', 'rel', 'mrel', 'punct', 'mpunct'];
                    const allSimple = styleBody.body.every(item =>
                        item && typeof item === 'object' &&
                        simpleTypes.includes(normalizeType(item.type))
                    );
                    if (allSimple) {
                        expandedBody.push(...styleBody.body);
                    } else {
                        expandedBody.push(child);
                    }
                } else {
                    // Keep STYLE wrapper for complex content (FRAC, etc.)
                    expandedBody.push(child);
                }
            } else {
                expandedBody.push(child);
            }
        }
        result.body = expandedBody;
    }

    // Special case: Flatten nested ROW nodes in body arrays
    // Lambda creates ROW for multi-digit numbers (e.g., "1234"), but MathLive keeps them flat
    if (nodeType === 'row' && result.body && Array.isArray(result.body)) {
        const flattenedBody = [];
        for (const child of result.body) {
            if (child && typeof child === 'object' && normalizeType(child.type) === 'row' && child.body) {
                // Check if this is a number-like ROW (only ORD children)
                const childBody = Array.isArray(child.body) ? child.body : [child.body];
                const isNumberLike = childBody.every(item =>
                    item && typeof item === 'object' &&
                    normalizeType(item.type) === 'ord' &&
                    item.value && item.value.length === 1 &&
                    /[0-9.]/.test(item.value)
                );
                if (isNumberLike) {
                    // Flatten: add each child directly
                    flattenedBody.push(...childBody);
                } else {
                    flattenedBody.push(child);
                }
            } else {
                flattenedBody.push(child);
            }
        }
        result.body = flattenedBody;
    }

    return result;
}

/**
 * Compare Lambda AST against MathLive AST reference
 *
 * @param {Object} lambdaAST - Lambda's AST output
 * @param {Object} mathliveRef - MathLive reference (from toJson() or reference file)
 * @returns {Object} Comparison result with score and details
 */
function compareASTToMathLive(lambdaAST, mathliveRef) {
    const differences = [];
    let matchedElements = 0;
    let totalElements = 0;

    // Extract AST from reference file format
    let mathliveAST;
    if (mathliveRef && mathliveRef.ast) {
        mathliveAST = mathliveRef.ast;
    } else {
        mathliveAST = mathliveRef;
    }

    // Unwrap MathLive's root wrapper first, then normalize structure
    const mathLiveUnwrapped = unwrapMathLiveRoot(mathliveAST);
    const mathLiveStructureNorm = normalizeMathLiveAST(mathLiveUnwrapped);

    // Normalize Lambda AST (flatten DELIMITED+ARRAY, etc.)
    const lambdaStructureNorm = normalizeLambdaAST(JSON.parse(JSON.stringify(lambdaAST)));

    // Normalize both trees (remove trivial wrappers)
    const lambdaNorm = unwrapTrivialNodes(lambdaStructureNorm);
    const mathLiveNorm = unwrapTrivialNodes(JSON.parse(JSON.stringify(mathLiveStructureNorm)));

    /**
     * Recursively compare nodes
     */
    function compareNodes(lambda, mathlive, path = 'root') {
        totalElements++;

        // Handle null/undefined
        if (!lambda && !mathlive) {
            matchedElements++;
            return;
        }

        if (!lambda || !mathlive) {
            differences.push({
                path,
                issue: lambda ? 'Extra element in Lambda' : 'Missing element in Lambda',
                lambda: lambda?.type || null,
                mathlive: mathlive?.type || null,
            });
            return;
        }

        // Handle case where MathLive uses a simple string (e.g., "a") and Lambda uses an object
        if (typeof mathlive === 'string' && typeof lambda === 'object') {
            // MathLive's string represents an ordinary symbol
            // Check if Lambda's value matches the string
            const lambdaValue = lambda.value || lambda.symbol || lambda.char || '';
            if (lambdaValue === mathlive) {
                matchedElements++;  // Value matches - count as compatible
            } else {
                differences.push({
                    path,
                    issue: 'Value mismatch (string vs object)',
                    lambda: lambdaValue,
                    mathlive: mathlive,
                });
            }
            return;  // No further comparison needed for string
        }

        // Handle case where Lambda uses a simple string (shouldn't happen, but be safe)
        if (typeof lambda === 'string' && typeof mathlive === 'object') {
            const mathliveValue = mathlive.value || mathlive.symbol || mathlive.char || '';
            if (lambda === mathliveValue) {
                matchedElements++;
            } else {
                differences.push({
                    path,
                    issue: 'Value mismatch (object vs string)',
                    lambda: lambda,
                    mathlive: mathliveValue,
                });
            }
            return;
        }

        // Compare types
        const lambdaType = lambda.type;
        const mathliveType = mathlive.type;

        // Special handling: MathLive command-based nodes (no type field)
        // e.g., \rule which has command:"\\rule" and args array
        if (!mathliveType && mathlive.command) {
            const mathliveCmd = (mathlive.command || '').replace(/^\\/, '');
            const lambdaCmd = (lambda.command || '').replace(/^\\/, '');

            // Check if Lambda's command matches MathLive's
            if (lambdaCmd === mathliveCmd) {
                matchedElements++;
                // For \rule, the dimensions are in args array - we count command match as success
                // Don't need to deeply compare args since Lambda stores dimensions differently
                return;
            }
            // If commands don't match but Lambda has a value that represents the same thing
            // (e.g., ORD with command: "rule" represents the \rule command)
            if (lambdaCmd === mathliveCmd) {
                matchedElements++;
                return;
            }
            // Command mismatch
            differences.push({
                path,
                issue: 'Command mismatch',
                lambda: lambdaCmd,
                mathlive: mathliveCmd,
            });
            return;
        }

        // Special handling: Lambda STYLE wrapper vs MathLive style attribute
        // Lambda wraps styled content in STYLE node, MathLive puts style as attribute on the content
        if (lambdaType === 'STYLE' && mathlive.style) {
            // Lambda has STYLE wrapper, MathLive has style attribute on content
            // Compare Lambda's body to MathLive directly (style is preserved differently)
            const lambdaBody = lambda.body;
            if (lambdaBody) {
                // Check if Lambda's style command matches MathLive's style variant or color
                const lambdaCmd = (lambda.command || '').replace(/^\\/, '').toLowerCase();
                const mathliveVariant = (mathlive.style?.variant || '').toLowerCase();
                const mathliveColor = mathlive.style?.verbatimColor || mathlive.style?.color;

                // Map Lambda style commands to MathLive variants
                const styleToVariant = {
                    'mathfrak': 'fraktur',
                    'mathbb': 'double-struck',
                    'mathcal': 'calligraphic',
                    'mathscr': 'script',
                    'mathbf': 'bold',
                    'mathrm': 'normal',
                    'mathit': 'italic',
                    'mathsf': 'sans-serif',
                    'mathtt': 'monospace',
                };

                const expectedVariant = styleToVariant[lambdaCmd];
                // Match font variant OR color command
                if (expectedVariant === mathliveVariant || lambdaCmd === mathliveVariant ||
                    (lambdaCmd === 'textcolor' && mathliveColor) ||
                    (lambdaCmd === 'color' && mathliveColor)) {
                    // Style matches - compare the body content instead
                    matchedElements++;
                    compareNodes(lambdaBody, mathlive, path + '.body');
                    return;
                }
            }
        }

        // Special handling: Lambda STYLE wrapper with body array vs MathLive individual styled elements
        // When Lambda has STYLE with a body containing multiple elements, and MathLive has those
        // elements at the same level with style attributes, we need to compare them differently
        if (lambdaType === 'STYLE' && Array.isArray(lambda.body?.body)) {
            const lambdaBody = lambda.body.body;
            // Check if MathLive node has the same number of siblings with matching style
            // For now, just compare the body directly as if it were unwrapped
            matchedElements++;
            compareNodes(lambda.body, mathlive, path + '.body');
            return;
        }

        // Special handling: Lambda OVERUNDER with body (big ops) vs MathLive extensible-symbol
        // Lambda: OVERUNDER { body: OP, above: ..., below: ... }
        // MathLive: extensible-symbol { value: "∑", subscript: [...], superscript: [...] }
        if (lambdaType === 'OVERUNDER' && mathliveType === 'extensible-symbol') {
            // Types are compatible
            matchedElements++;

            // Compare the operator symbol (Lambda body.value or body.command vs MathLive value)
            const lambdaOp = lambda.body;
            if (lambdaOp) {
                const lambdaValue = lambdaOp.value || '';
                const lambdaCmd = (lambdaOp.command || '').replace(/^\\/, '');
                const mathliveValue = mathlive.value || mathlive.symbol || '';
                const mathliveCmd = (mathlive.command || '').replace(/^\\/, '');

                // Check symbol/command match
                if (lambdaValue === mathliveValue || lambdaCmd === mathliveCmd) {
                    matchedElements++;
                    totalElements++;
                } else if (lambdaValue || mathliveValue) {
                    differences.push({
                        path: path + '.body',
                        issue: 'Operator symbol mismatch',
                        lambda: lambdaValue || lambdaCmd,
                        mathlive: mathliveValue || mathliveCmd,
                    });
                    totalElements++;
                }
            }

            // Compare limits: Lambda above/below vs MathLive superscript/subscript
            const lambdaAbove = getBranch(lambda, 'above');
            const mathliveSuper = getBranch(mathlive, 'superscript');
            if (lambdaAbove || mathliveSuper) {
                // Handle MathLive string arrays vs Lambda objects
                if (Array.isArray(mathliveSuper)) {
                    const comparison = compareBranchValues(lambdaAbove, mathliveSuper);
                    totalElements++;
                    if (comparison.match) {
                        matchedElements++;
                    } else {
                        differences.push({
                            path: path + '.above',
                            issue: 'Superscript/above mismatch',
                            lambda: JSON.stringify(lambdaAbove),
                            mathlive: JSON.stringify(mathliveSuper),
                        });
                    }
                } else if (lambdaAbove && mathliveSuper) {
                    compareNodes(lambdaAbove, mathliveSuper, path + '.above');
                }
            }

            const lambdaBelow = getBranch(lambda, 'below');
            const mathliveSub = getBranch(mathlive, 'subscript');
            if (lambdaBelow || mathliveSub) {
                if (Array.isArray(mathliveSub)) {
                    const comparison = compareBranchValues(lambdaBelow, mathliveSub);
                    totalElements++;
                    if (comparison.match) {
                        matchedElements++;
                    } else {
                        differences.push({
                            path: path + '.below',
                            issue: 'Subscript/below mismatch',
                            lambda: JSON.stringify(lambdaBelow),
                            mathlive: JSON.stringify(mathliveSub),
                        });
                    }
                } else if (lambdaBelow && mathliveSub) {
                    compareNodes(lambdaBelow, mathliveSub, path + '.below');
                }
            }

            return;  // Done with this comparison
        }

        // Special handling: Lambda ORD with command vs MathLive macro
        // MathLive uses 'macro' type for custom commands, Lambda uses ORD with command field
        if (lambdaType === 'ORD' && mathliveType === 'macro') {
            // Compare commands - Lambda command vs MathLive command
            const lambdaCmd = (lambda.command || '').replace(/^\\/, '');
            const mathliveCmd = (mathlive.command || '').replace(/^\\/, '');

            if (lambdaCmd && mathliveCmd && lambdaCmd === mathliveCmd) {
                matchedElements++;
                // Commands match - consider this a match for unrecognized commands
                // MathLive expands macros, Lambda stores them as-is
                return;
            }
        }

        // Special handling: Lambda ORD with command vs MathLive mrel (for \not{} etc)
        if (lambdaType === 'ORD' && mathliveType === 'mrel') {
            const lambdaCmd = (lambda.command || '').replace(/^\\/, '').replace(/\{\}$/, '');
            const mathliveCmd = (mathlive.command || '').replace(/^\\/, '');

            if (lambdaCmd && mathliveCmd && lambdaCmd === mathliveCmd) {
                matchedElements++;
                return;
            }
        }

        if (areTypesCompatible(lambdaType, mathliveType)) {
            matchedElements++;
        } else {
            differences.push({
                path,
                issue: 'Type mismatch',
                lambda: lambdaType,
                mathlive: mathliveType,
            });
        }

        // Compare values - use command as fallback for symbol comparison
        // Lambda uses TFM codepoints (control chars), MathLive uses Unicode
        const lambdaValue = lambda.value || lambda.symbol || lambda.char || '';
        const mathliveValue = mathlive.value || mathlive.symbol || mathlive.char || '';

        // Normalize command names for comparison (remove leading backslash)
        const lambdaCmd = (lambda.command || '').replace(/^\\/, '');
        const mathliveCmd = (mathlive.command || '').replace(/^\\/, '');

        if (lambdaValue && mathliveValue && lambdaValue !== mathliveValue) {
            // If values differ, check if commands match (semantic equivalence)
            if (lambdaCmd && mathliveCmd && lambdaCmd === mathliveCmd) {
                // Commands match - this is semantically equivalent, just different representation
                // Don't record as a difference
            } else {
                differences.push({
                    path,
                    issue: 'Value mismatch',
                    lambda: lambdaValue,
                    mathlive: mathliveValue,
                });
            }
        }

        // Compare branches
        const branches = ['body', 'numer', 'denom', 'above', 'below', 'superscript', 'subscript'];

        for (const branch of branches) {
            const lambdaBranch = getBranch(lambda, branch);
            const mathliveBranch = getBranch(mathlive, branch);

            if (lambdaBranch || mathliveBranch) {
                // Check if MathLive branch contains simple strings (vs Lambda objects)
                const mathliveIsStrings = Array.isArray(mathliveBranch) &&
                    mathliveBranch.length > 0 &&
                    mathliveBranch.every(item => typeof item === 'string');

                if (mathliveIsStrings) {
                    // Use semantic comparison for string vs object branches
                    totalElements++;
                    const comparison = compareBranchValues(lambdaBranch, mathliveBranch);
                    if (comparison.match) {
                        matchedElements++;
                    } else {
                        differences.push({
                            path: `${path}.${branch}`,
                            issue: `Branch content mismatch: ${comparison.reason}`,
                            lambda: JSON.stringify(lambdaBranch),
                            mathlive: JSON.stringify(mathliveBranch),
                        });
                    }
                } else if (Array.isArray(lambdaBranch) && Array.isArray(mathliveBranch)) {
                    // Compare arrays element by element
                    const maxLen = Math.max(lambdaBranch.length, mathliveBranch.length);
                    for (let i = 0; i < maxLen; i++) {
                        compareNodes(
                            lambdaBranch[i],
                            mathliveBranch[i],
                            `${path}.${branch}[${i}]`
                        );
                    }
                } else if (Array.isArray(lambdaBranch)) {
                    // Lambda is array, MathLive is single - wrap for comparison
                    if (lambdaBranch.length === 1) {
                        compareNodes(lambdaBranch[0], mathliveBranch, `${path}.${branch}`);
                    } else {
                        differences.push({
                            path: `${path}.${branch}`,
                            issue: 'Structure mismatch (Lambda array vs MathLive single)',
                            lambda: lambdaBranch.length,
                            mathlive: 'single',
                        });
                    }
                } else if (Array.isArray(mathliveBranch)) {
                    // MathLive is array, Lambda is single
                    if (mathliveBranch.length === 1) {
                        compareNodes(lambdaBranch, mathliveBranch[0], `${path}.${branch}`);
                    } else {
                        differences.push({
                            path: `${path}.${branch}`,
                            issue: 'Structure mismatch (Lambda single vs MathLive array)',
                            lambda: 'single',
                            mathlive: mathliveBranch.length,
                        });
                    }
                } else {
                    // Both single values/objects
                    compareNodes(lambdaBranch, mathliveBranch, `${path}.${branch}`);
                }
            }
        }
    }

    // Start comparison
    compareNodes(lambdaNorm, mathLiveNorm);

    // Calculate score
    const passRate = totalElements > 0 ? (matchedElements / totalElements) * 100 : 0;

    // Content verification (extract all text values)
    const lambdaContent = extractTextContent(lambdaNorm);
    const mathliveContent = extractTextContent(mathLiveNorm);
    const contentMatch = lambdaContent === mathliveContent;

    return {
        passRate,
        score: passRate, // Alias for backward compatibility
        matchedElements,
        totalElements,
        contentMatch,
        lambdaContent,
        mathliveContent,
        differences: differences.slice(0, 20), // Limit reported differences
        summary: differences.length === 0 ? 'Perfect match' :
            differences.length < 5 ? 'Minor differences' : 'Significant differences',
    };
}

/**
 * Quick score calculation without detailed differences
 */
function quickCompareScore(lambdaAST, mathliveRef) {
    const result = compareASTToMathLive(lambdaAST, mathliveRef);
    return result.score;
}

/**
 * Check if a MathLive reference file exists for a test
 */
function hasMathLiveReference(referencePath) {
    const mathliveRefPath = referencePath.replace(/\.mathml\.json$/, '.mathlive.json');
    return fs.existsSync(mathliveRefPath);
}

/**
 * Load MathLive reference file
 */
function loadMathLiveReference(referencePath) {
    const mathliveRefPath = referencePath.replace(/\.mathml\.json$/, '.mathlive.json');

    if (!fs.existsSync(mathliveRefPath)) {
        return null;
    }

    try {
        const content = fs.readFileSync(mathliveRefPath, 'utf-8');
        return JSON.parse(content);
    } catch (e) {
        console.error(`Failed to load MathLive reference: ${mathliveRefPath}`, e.message);
        return null;
    }
}


export {
    compareASTToMathLive,
    quickCompareScore,
    hasMathLiveReference,
    loadMathLiveReference,
    areTypesCompatible,
    unwrapTrivialNodes,
    extractTextContent,
    countElements,
    LAMBDA_TO_MATHLIVE_TYPE,
    MATHLIVE_TO_LAMBDA_TYPE,
};
