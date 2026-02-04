/**
 * HTML Comparator for LaTeX Math Testing
 *
 * Compares Lambda's HTML output against MathLive and KaTeX reference HTML.
 * This layer accounts for 40% of the overall test score.
 *
 * Cross-reference strategy: Compare against both MathLive and KaTeX,
 * then take the higher score to account for interpretation differences.
 *
 * Normalization rules:
 * - Strip all inline styles (compare structure, not exact styling)
 * - Normalize class names to semantic categories
 * - Ignore library-specific wrapper elements
 * - Collapse consecutive text nodes
 * - Normalize unicode math characters
 */

import { JSDOM } from 'jsdom';

// Class name normalization to semantic categories
const CLASS_CATEGORIES = {
    // Fractions
    'ML__frac': 'frac',
    'ML__frac-line': 'frac-line',
    'frac': 'frac',
    'frac-line': 'frac-line',
    'lambda-frac': 'frac',
    'mfrac': 'frac',

    // Accents (normalize all accent classes)
    'ML__accent-body': 'accent',
    'ML__accent-combining-char': 'accent',
    'accent-body': 'accent',

    // Column alignment (normalize to same category)
    'col-align-l': 'col-align',
    'col-align-c': 'col-align',
    'col-align-r': 'col-align',

    // Delimiter sizes (normalize all sizes to same category)
    'ML__delim-size1': 'delim-size',
    'ML__delim-size2': 'delim-size',
    'ML__delim-size3': 'delim-size',
    'ML__delim-size4': 'delim-size',
    'delim-size1': 'delim-size',
    'delim-size2': 'delim-size',
    'delim-size3': 'delim-size',
    'delim-size4': 'delim-size',
    'ML__delim-mult': 'delim-mult',
    'delim-mult': 'delim-mult',

    // Numerator/Denominator
    'ML__numer': 'numer',
    'ML__denom': 'denom',
    'numer': 'numer',
    'denom': 'denom',

    // Roots
    'ML__sqrt': 'sqrt',
    'ML__sqrt-index': 'sqrt-index',
    'ML__sqrt-sign': 'sqrt-sign',
    'ML__sqrt-line': 'sqrt-line',
    'ML__sqrt-symbol': 'sqrt-sign',
    'ML__sqrt-body': 'sqrt-body',
    'ML__root': 'sqrt-index',
    'ML__delim-size1': 'delim-size',
    'ML__delim-size2': 'delim-size',
    'sqrt': 'sqrt',
    'sqrt-sign': 'sqrt-sign',
    'lambda-sqrt': 'sqrt',
    'msqrt': 'sqrt',
    'mroot': 'sqrt',

    // Scripts (MathLive)
    'ML__sup': 'superscript',
    'ML__sub': 'subscript',
    'ML__supsub': 'scripts',

    // Scripts (KaTeX)
    'msupsub': 'scripts',
    'vlist': 'vlist',
    'vlist-t': 'vlist',
    'vlist-t2': 'vlist',
    'vlist-r': 'vlist',
    'vlist-s': 'vlist',

    // VList (MathLive)
    'ML__vlist': 'vlist',
    'ML__vlist-t': 'vlist',
    'ML__vlist-t2': 'vlist',
    'ML__vlist-r': 'vlist',
    'ML__vlist-s': 'vlist',
    'ML__pstrut': 'strut',
    'ML__msubsup': 'scripts',
    'ML__mfrac': 'frac',
    'ML__center': 'center',
    'ML__nulldelimiter': 'delim',

    // Delimiters
    'ML__open': 'open',
    'ML__close': 'close',
    'ML__left-right': 'left-right',
    'delimsizing': 'delim',
    'nulldelimiter': 'delim',
    'mopen': 'open',
    'mclose': 'close',

    // Atoms
    'ML__mord': 'ord',
    'ML__mbin': 'bin',
    'ML__mrel': 'rel',
    'ML__mop': 'op',
    'mord': 'ord',
    'mbin': 'bin',
    'mrel': 'rel',
    'mop': 'op',
    'mpunct': 'punct',
    'minner': 'inner',

    // Layout
    'ML__base': 'base',
    'ML__strut': 'strut',
    'base': 'base',
    'strut': 'strut',

    // Containers
    'katex': 'math-container',
    'katex-html': 'math-html',
    'ML__latex': 'math-container',
    'lambda-math': 'math-container',

    // Sizing
    'sizing': 'sizing',
    'reset-size': 'sizing',

    // Text
    'ML__text': 'text',
    'text': 'text',
    'mathrm': 'text',
    'textord': 'text'
};

// Elements to ignore during comparison (library-specific wrappers)
const IGNORED_ELEMENTS = new Set([
    'style',
    'script',
    'annotation',
    'annotation-xml'
]);

// Wrapper classes to unwrap (single child gets promoted)
const WRAPPER_CLASSES = new Set([
    'katex',
    'katex-html',
    'ML__latex',
    'ML__base',
    'ML__mord'  // Lambda uses ML__mord as wrapper like MathLive uses ML__base
]);

/**
 * Parse HTML string into a DOM tree
 */
function parseHTML(html) {
    const dom = new JSDOM(`<body>${html}</body>`);
    return dom.window.document.body;
}

/**
 * Normalize a class name to its semantic category
 */
function normalizeClassName(className) {
    return CLASS_CATEGORIES[className] || className;
}

/**
 * Extract semantic classes from a class list
 */
function getSemanticClasses(classList) {
    if (!classList || classList.length === 0) return [];

    const classes = Array.from(classList);
    const normalized = classes
        .map(c => normalizeClassName(c))
        .filter(c => !c.startsWith('ML__') && !c.startsWith('katex-') && !c.startsWith('size'));

    // Deduplicate
    return [...new Set(normalized)];
}

/**
 * Check if an element should be ignored
 */
function shouldIgnoreElement(element) {
    if (!element || element.nodeType !== 1) return false;

    const tagName = element.tagName.toLowerCase();
    if (IGNORED_ELEMENTS.has(tagName)) return true;

    // Ignore hidden elements
    const style = element.getAttribute('style');
    if (style && (style.includes('display: none') || style.includes('display:none'))) {
        return true;
    }

    // Ignore empty struts
    if (element.classList.contains('strut') || element.classList.contains('ML__strut')) {
        return true;
    }

    return false;
}

/**
 * Normalize an element, returning a simplified structure
 */
function normalizeElement(element) {
    if (!element) return null;

    // Handle text nodes
    if (element.nodeType === 3) {
        const text = element.textContent.trim();
        return text ? { type: 'text', content: text } : null;
    }

    // Skip non-element nodes
    if (element.nodeType !== 1) return null;

    // Skip ignored elements
    if (shouldIgnoreElement(element)) return null;

    const tagName = element.tagName.toLowerCase();
    const semanticClasses = getSemanticClasses(element.classList);

    // Process children
    const children = [];
    for (const child of element.childNodes) {
        const normalized = normalizeElement(child);
        if (normalized) {
            // Flatten arrays (from unwrapped wrappers)
            if (Array.isArray(normalized)) {
                children.push(...normalized);
            } else {
                children.push(normalized);
            }
        }
    }

    // Unwrap library-specific wrappers
    const hasWrapperClass = Array.from(element.classList).some(c => WRAPPER_CLASSES.has(c));
    if (hasWrapperClass && children.length > 0) {
        if (children.length === 1) {
            return children[0];
        }
        return children;
    }

    // Get text content if leaf node
    let textContent = null;
    if (children.length === 0) {
        const text = element.textContent.trim();
        if (text) textContent = text;
    }

    return {
        type: 'element',
        tag: tagName,
        classes: semanticClasses,
        content: textContent,
        children: children.length > 0 ? children : undefined
    };
}

/**
 * Normalize an HTML tree for comparison
 */
function normalizeHTMLTree(rootElement) {
    const normalized = normalizeElement(rootElement);

    // Handle array result (multiple top-level elements)
    if (Array.isArray(normalized)) {
        return { type: 'root', children: normalized };
    }

    return normalized || { type: 'root', children: [] };
}

/**
 * Compare two normalized HTML nodes
 * Simplified scoring: Perfect match = 1.0, partial match based on specifics
 */
function compareHTMLNodes(lambdaNode, refNode, path, results) {
    results.totalElements++;

    // Handle null/undefined
    if (!lambdaNode && !refNode) {
        results.matchedElements++;
        return;
    }

    if (!lambdaNode || !refNode) {
        results.differences.push({
            path,
            issue: 'Missing element',
            expected: summarizeElement(refNode),
            got: summarizeElement(lambdaNode)
        });
        return;
    }

    // Handle text nodes
    if (lambdaNode.type === 'text' && refNode.type === 'text') {
        const lambdaText = lambdaNode.content || '';
        const refText = refNode.content || '';

        if (lambdaText === refText) {
            results.matchedElements++;
        } else {
            // Partial match for similar text
            if (lambdaText.includes(refText) || refText.includes(lambdaText)) {
                results.matchedElements += 0.5;
            }
            results.differences.push({
                path,
                issue: 'Text content mismatch',
                expected: refText,
                got: lambdaText
            });
        }
        return;
    }

    // Handle type mismatch
    if (lambdaNode.type !== refNode.type) {
        results.differences.push({
            path,
            issue: 'Node type mismatch',
            expected: refNode.type,
            got: lambdaNode.type
        });
        return;
    }

    // Compare element nodes
    if (lambdaNode.type === 'element') {
        let score = 0;
        let maxScore = 0;
        let issues = [];

        // Tag name comparison
        maxScore += 1;
        if (lambdaNode.tag === refNode.tag) {
            score += 1;
        } else {
            issues.push('Tag mismatch');
        }

        // Class comparison (using set intersection)
        const lambdaClasses = new Set(lambdaNode.classes || []);
        const refClasses = new Set(refNode.classes || []);

        // Check if classes are semantically equivalent
        if (lambdaClasses.size === 0 && refClasses.size === 0) {
            // Both have no classes - perfect match
            maxScore += 1;
            score += 1;
        } else if (lambdaClasses.size > 0 || refClasses.size > 0) {
            maxScore += 1;
            const classIntersection = [...lambdaClasses].filter(c => refClasses.has(c));
            const classUnion = new Set([...lambdaClasses, ...refClasses]);
            const classScore = classIntersection.length / classUnion.size;
            score += classScore;

            if (classScore < 1) {
                issues.push('Class mismatch');
            }
        }

        // Content comparison for leaf nodes
        if (lambdaNode.content !== undefined || refNode.content !== undefined) {
            maxScore += 1;
            if (lambdaNode.content === refNode.content) {
                score += 1;
            } else {
                issues.push('Content mismatch');
            }
        }

        // Children comparison
        const lambdaChildren = lambdaNode.children || [];
        const refChildren = refNode.children || [];

        // Children count
        if (lambdaChildren.length > 0 || refChildren.length > 0) {
            maxScore += 1;
            if (lambdaChildren.length === refChildren.length) {
                score += 1;
            } else {
                const countRatio = Math.min(lambdaChildren.length, refChildren.length) /
                                   Math.max(lambdaChildren.length, refChildren.length, 1);
                score += countRatio;
                issues.push('Children count mismatch');
            }
        }

        // Normalize score to 0-1 range
        const normalizedScore = maxScore > 0 ? score / maxScore : 1;
        results.matchedElements += normalizedScore;

        if (issues.length > 0) {
            results.differences.push({
                path,
                issue: issues.join(', '),
                expected: summarizeElement(refNode),
                got: summarizeElement(lambdaNode)
            });
        }

        // Recursively compare children
        const maxChildren = Math.max(lambdaChildren.length, refChildren.length);
        for (let i = 0; i < maxChildren; i++) {
            compareHTMLNodes(
                lambdaChildren[i] || null,
                refChildren[i] || null,
                `${path} > child[${i}]`,
                results
            );
        }
    }
}

/**
 * Summarize an element for error reporting
 */
function summarizeElement(node) {
    if (!node) return null;
    if (node.type === 'text') return { text: node.content };

    return {
        tag: node.tag,
        classes: node.classes,
        childCount: (node.children || []).length
    };
}

/**
 * Extract all text content from a normalized tree in order
 */
function extractTextContent(node) {
    const texts = [];
    
    function walk(n) {
        if (!n) return;
        if (n.type === 'text' && n.content) {
            const text = n.content.trim();
            if (text && text !== '​') { // skip zero-width space
                texts.push(text);
            }
        }
        if (n.children) {
            for (const child of n.children) {
                walk(child);
            }
        }
    }
    
    walk(node);
    return texts;
}

/**
 * Compare Lambda HTML against a reference HTML
 */
function compareHTMLTrees(lambdaHTML, refHTML) {
    const results = {
        totalElements: 0,
        matchedElements: 0,
        differences: []
    };

    try {
        const lambdaRoot = parseHTML(lambdaHTML);
        const refRoot = parseHTML(refHTML);

        const normalizedLambda = normalizeHTMLTree(lambdaRoot);
        const normalizedRef = normalizeHTMLTree(refRoot);

        compareHTMLNodes(normalizedLambda, normalizedRef, 'root', results);
        
        // Text content bonus: if all text content matches in order, boost score
        const lambdaTexts = extractTextContent(normalizedLambda);
        const refTexts = extractTextContent(normalizedRef);
        
        const textsMatch = lambdaTexts.length === refTexts.length &&
            lambdaTexts.every((t, i) => t === refTexts[i]);
        
        if (textsMatch && lambdaTexts.length > 0) {
            // boost matched elements by 20% of total when text fully matches
            results.matchedElements += results.totalElements * 0.2;
        }
    } catch (error) {
        results.differences.push({
            path: 'root',
            issue: 'Parse error',
            error: error.message
        });
    }

    const passRate = results.totalElements > 0
        ? Math.min(100, Math.max(0, (results.matchedElements / results.totalElements) * 100))
        : 100;

    return {
        passRate: Math.round(passRate * 10) / 10,
        totalElements: Math.round(results.totalElements),
        matchedElements: Math.round(results.matchedElements * 10) / 10,
        differences: results.differences.slice(0, 10)
    };
}

/**
 * Compare Lambda's HTML output against both MathLive and KaTeX references,
 * returning the best score (cross-reference approach)
 *
 * @param {string} lambdaHTML - HTML output from Lambda
 * @param {string} mathLiveHTML - Reference HTML from MathLive
 * @param {string} katexHTML - Reference HTML from KaTeX (optional)
 * @returns {Object} Best comparison result with both scores
 */
function compareHTML(lambdaHTML, mathLiveHTML, katexHTML = null) {
    const mathLiveResult = compareHTMLTrees(lambdaHTML, mathLiveHTML);

    let katexResult = null;
    if (katexHTML) {
        katexResult = compareHTMLTrees(lambdaHTML, katexHTML);
    }

    // Determine best result
    let bestResult = mathLiveResult;
    let bestReference = 'mathlive';

    if (katexResult && katexResult.passRate > mathLiveResult.passRate) {
        bestResult = katexResult;
        bestReference = 'katex';
    }

    return {
        passRate: bestResult.passRate,
        bestReference,
        mathliveScore: mathLiveResult.passRate,
        katexScore: katexResult ? katexResult.passRate : null,
        totalElements: bestResult.totalElements,
        matchedElements: bestResult.matchedElements,
        differences: bestResult.differences
    };
}

export {
    compareHTML,
    compareHTMLTrees,
    parseHTML,
    normalizeHTMLTree,
    normalizeClassName,
    CLASS_CATEGORIES
};
