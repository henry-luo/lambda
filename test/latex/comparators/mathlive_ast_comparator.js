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
    'SQRT': 'sqrt',             // Square root (also handles nth root)
    'ROOT': 'sqrt',             // nth root (same as SQRT in MathLive)
    'SCRIPTS': 'subsup',        // Sub/superscript (MathLive uses inline branches)
    'ROW': 'first',             // Row of elements / root
    'GROUP': 'group',           // Grouping

    // Delimiters and fences
    'DELIMITED': 'leftright',   // Delimited expression

    // Accents and decorations
    'ACCENT': 'overunder',      // Accent marks (handled with above/below)
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
 */
const LAMBDA_BRANCH_ALIASES = {
    'numer': ['above', 'numer'],
    'denom': ['below', 'denom'],
    'above': ['above', 'numer'],
    'below': ['below', 'denom'],
    'superscript': ['superscript'],
    'subscript': ['subscript'],
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
        ['sqrt', 'root'],
        ['ord', 'mord', 'mi'],
        ['bin', 'mbin', 'mo'],
        ['rel', 'mrel'],
        ['op', 'mop'],
        ['delimited', 'leftright'],
        ['scripts', 'subsup'],
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

    // Normalize both trees
    const lambdaNorm = unwrapTrivialNodes(JSON.parse(JSON.stringify(lambdaAST)));
    const mathLiveNorm = unwrapTrivialNodes(JSON.parse(JSON.stringify(mathliveAST)));

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

        // Compare types
        const lambdaType = lambda.type;
        const mathliveType = mathlive.type;

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

        // Compare values
        const lambdaValue = lambda.value || lambda.symbol || lambda.char || '';
        const mathliveValue = mathlive.value || mathlive.symbol || mathlive.char || '';

        if (lambdaValue && mathliveValue && lambdaValue !== mathliveValue) {
            differences.push({
                path,
                issue: 'Value mismatch',
                lambda: lambdaValue,
                mathlive: mathliveValue,
            });
        }

        // Compare branches
        const branches = ['body', 'numer', 'denom', 'above', 'below', 'superscript', 'subscript'];

        for (const branch of branches) {
            const lambdaBranch = getBranch(lambda, branch);
            const mathliveBranch = getBranch(mathlive, branch);

            if (lambdaBranch || mathliveBranch) {
                if (Array.isArray(lambdaBranch) && Array.isArray(mathliveBranch)) {
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
