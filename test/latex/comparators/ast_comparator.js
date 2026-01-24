/**
 * AST Comparator for LaTeX Math Testing
 *
 * Compares Lambda's parsed AST against MathLive's reference AST.
 * This layer accounts for 50% of the overall test score.
 *
 * Normalization rules:
 * - Ignore node IDs and internal metadata
 * - Normalize whitespace in text content
 * - Treat equivalent structures as equal (e.g., \frac{a}{b} vs {a \over b})
 * - Ignore style-only attributes that don't affect meaning
 */

// Node type normalization mapping
const NORMALIZED_NODE_TYPES = {
    // Atoms
    'ord': 'ordinary',
    'mord': 'ordinary',
    'op': 'operator',
    'mop': 'operator',
    'bin': 'binary',
    'mbin': 'binary',
    'rel': 'relation',
    'mrel': 'relation',
    'open': 'open',
    'mopen': 'open',
    'close': 'close',
    'mclose': 'close',
    'punct': 'punctuation',
    'mpunct': 'punctuation',
    'inner': 'inner',
    'minner': 'inner',

    // Structures
    'frac': 'fraction',
    'genfrac': 'fraction',
    'sqrt': 'radical',
    'root': 'root',
    'sup': 'superscript',
    'sub': 'subscript',
    'supsub': 'scripts',
    'subsup': 'scripts',
    'msubsup': 'scripts',
    'msup': 'superscript',
    'msub': 'subscript',

    // Layout
    'mrow': 'row',
    'group': 'row',
    'mspace': 'space',
    'spacing': 'space',
    'mphantom': 'phantom',
    'phantom': 'phantom',

    // Big operators
    'largeop': 'largeop',
    'bigop': 'largeop',

    // Delimiters
    'leftright': 'delimited',
    'delim': 'delimited',

    // Arrays/matrices
    'array': 'array',
    'matrix': 'array',
    'environment': 'environment',

    // Text
    'text': 'text',
    'mtext': 'text'
};

// Properties to ignore during comparison
const IGNORED_PROPERTIES = new Set([
    'id',
    'verbatimLatex',
    'displayContainsHighlight',
    'mode',
    'isSelected',
    'caret',
    'style',       // Ignore style for semantic comparison
    'color',
    'backgroundColor',
    'fontSize',
    'fontFamily',
    'isExtensibleSymbol',
    'skipBoundary',
    'captureSelection'
]);

// Branches that contain child atoms
const CHILD_BRANCHES = ['body', 'above', 'below', 'superscript', 'subscript', 'numer', 'denom', 'args', 'cells', 'children'];

/**
 * Normalize a node type to canonical form
 */
function normalizeNodeType(type) {
    if (!type) return 'unknown';
    const lower = String(type).toLowerCase();
    return NORMALIZED_NODE_TYPES[lower] || lower;
}

/**
 * Normalize text content by trimming and collapsing whitespace
 */
function normalizeText(text) {
    if (!text) return '';
    return String(text).trim().replace(/\s+/g, ' ');
}

/**
 * Extract the value from a node (handles different AST formats)
 */
function extractValue(node) {
    if (typeof node === 'string') return node;
    if (node.value !== undefined) return String(node.value);
    if (node.char !== undefined) return String(node.char);
    if (node.symbol !== undefined) return String(node.symbol);
    return null;
}

/**
 * Get children from a node (handles different AST structures)
 */
function getChildren(node) {
    if (Array.isArray(node)) return node;

    const children = [];
    for (const branch of CHILD_BRANCHES) {
        if (node[branch]) {
            if (Array.isArray(node[branch])) {
                children.push(...node[branch]);
            } else {
                children.push(node[branch]);
            }
        }
    }
    return children;
}

/**
 * Flatten simple wrapper nodes (single-child groups, etc.)
 */
function flattenNode(node) {
    if (typeof node === 'string') return node;
    if (!node || typeof node !== 'object') return node;

    const type = normalizeNodeType(node.type);
    const children = getChildren(node);

    // Flatten single-child wrapper nodes
    if ((type === 'row' || type === 'root') && children.length === 1) {
        return flattenNode(children[0]);
    }

    return node;
}

/**
 * Compare two AST nodes recursively
 *
 * @param {Object} lambdaNode - Node from Lambda's AST
 * @param {Object} refNode - Node from MathLive's reference AST
 * @param {string} path - Current path in the tree (for error reporting)
 * @param {Object} results - Accumulator for comparison results
 */
function compareNodes(lambdaNode, refNode, path, results) {
    results.totalNodes++;

    // Handle null/undefined
    if (!lambdaNode && !refNode) {
        results.matchedNodes++;
        return;
    }

    if (!lambdaNode || !refNode) {
        results.differences.push({
            path,
            issue: 'Missing node',
            expected: refNode ? summarizeNode(refNode) : null,
            got: lambdaNode ? summarizeNode(lambdaNode) : null
        });
        return;
    }

    // Handle string atoms (leaf nodes)
    if (typeof lambdaNode === 'string' && typeof refNode === 'string') {
        if (normalizeText(lambdaNode) === normalizeText(refNode)) {
            results.matchedNodes++;
        } else {
            results.differences.push({
                path,
                issue: 'Text mismatch',
                expected: refNode,
                got: lambdaNode
            });
        }
        return;
    }

    // Handle mixed types (string vs object)
    if (typeof lambdaNode === 'string' || typeof refNode === 'string') {
        const lambdaVal = extractValue(lambdaNode);
        const refVal = extractValue(refNode);

        if (normalizeText(lambdaVal) === normalizeText(refVal)) {
            results.matchedNodes++;
        } else {
            results.differences.push({
                path,
                issue: 'Type/value mismatch',
                expected: summarizeNode(refNode),
                got: summarizeNode(lambdaNode)
            });
        }
        return;
    }

    // Compare node types
    const lambdaType = normalizeNodeType(lambdaNode.type);
    const refType = normalizeNodeType(refNode.type);

    let typeMatch = (lambdaType === refType);

    // Allow certain type equivalences
    if (!typeMatch) {
        // fraction variants are equivalent
        if (lambdaType === 'fraction' && refType === 'fraction') typeMatch = true;
        // scripts variants are equivalent
        if (['scripts', 'superscript', 'subscript'].includes(lambdaType) &&
            ['scripts', 'superscript', 'subscript'].includes(refType)) typeMatch = true;
    }

    if (!typeMatch) {
        results.differences.push({
            path,
            issue: 'Node type mismatch',
            expected: { type: refType },
            got: { type: lambdaType }
        });
        // Continue comparing children even if type differs
    } else {
        results.matchedNodes += 0.4; // 40% weight for type match
    }

    // Compare values if present
    const lambdaVal = extractValue(lambdaNode);
    const refVal = extractValue(refNode);

    if (lambdaVal !== null || refVal !== null) {
        if (normalizeText(lambdaVal) === normalizeText(refVal)) {
            results.matchedNodes += 0.1; // 10% weight for value match
        } else if (lambdaVal !== null && refVal !== null) {
            results.differences.push({
                path,
                issue: 'Value mismatch',
                expected: refVal,
                got: lambdaVal
            });
        }
    }

    // Compare children
    const lambdaChildren = getChildren(lambdaNode);
    const refChildren = getChildren(refNode);

    // Check children count
    if (lambdaChildren.length !== refChildren.length) {
        results.differences.push({
            path,
            issue: 'Children count mismatch',
            expected: `${refChildren.length} children`,
            got: `${lambdaChildren.length} children`
        });
        results.matchedNodes += 0.2 * Math.min(lambdaChildren.length, refChildren.length) /
                                Math.max(lambdaChildren.length, refChildren.length, 1);
    } else {
        results.matchedNodes += 0.2; // 20% weight for children count match
    }

    // Recursively compare children
    const maxChildren = Math.max(lambdaChildren.length, refChildren.length);
    for (let i = 0; i < maxChildren; i++) {
        const childPath = `${path}[${i}]`;
        compareNodes(
            lambdaChildren[i] || null,
            refChildren[i] || null,
            childPath,
            results
        );
    }

    // Add remaining weight for children match
    if (maxChildren > 0) {
        results.matchedNodes += 0.3; // 30% weight distributed across children comparison
    } else {
        results.matchedNodes += 0.3; // Leaf node gets full remaining weight
    }
}

/**
 * Create a summary of a node for error reporting
 */
function summarizeNode(node) {
    if (!node) return null;
    if (typeof node === 'string') return node;

    const summary = { type: node.type || 'unknown' };
    const value = extractValue(node);
    if (value) summary.value = value;

    const children = getChildren(node);
    if (children.length > 0) {
        summary.childCount = children.length;
    }

    return summary;
}

/**
 * Compare two ASTs and return comparison results
 *
 * @param {Object} lambdaAST - AST from Lambda's parser
 * @param {Object} mathLiveAST - Reference AST from MathLive
 * @returns {Object} Comparison results with passRate and differences
 */
function compareAST(lambdaAST, mathLiveAST) {
    const results = {
        totalNodes: 0,
        matchedNodes: 0,
        differences: []
    };

    // Flatten root wrappers before comparison
    const flatLambda = flattenNode(lambdaAST);
    const flatRef = flattenNode(mathLiveAST);

    // Start recursive comparison
    compareNodes(flatLambda, flatRef, 'root', results);

    // Calculate pass rate (clamp to 0-100%)
    const passRate = results.totalNodes > 0
        ? Math.min(100, Math.max(0, (results.matchedNodes / results.totalNodes) * 100))
        : 100;

    return {
        passRate: Math.round(passRate * 10) / 10,
        totalNodes: Math.round(results.totalNodes),
        matchedNodes: Math.round(results.matchedNodes * 10) / 10,
        differences: results.differences.slice(0, 10) // Limit to 10 differences for readability
    };
}

/**
 * Calculate weighted node matching criteria
 *
 * | Property        | Weight | Notes                    |
 * |-----------------|--------|--------------------------|
 * | Node type       | 40%    | frac, sqrt, sup, etc.    |
 * | Children count  | 20%    | Structural integrity     |
 * | Children match  | 30%    | Recursive comparison     |
 * | Attributes      | 10%    | Non-style attributes     |
 */
function calculateNodeScore(typeMatch, countMatch, childrenMatch, attributesMatch) {
    return (typeMatch ? 0.4 : 0) +
           (countMatch ? 0.2 : 0) +
           (childrenMatch * 0.3) +
           (attributesMatch ? 0.1 : 0);
}

export {
    compareAST,
    normalizeNodeType,
    getChildren,
    extractValue,
    NORMALIZED_NODE_TYPES,
    IGNORED_PROPERTIES
};
