/**
 * MathML Comparator for LaTeX Math Testing
 *
 * Compares Lambda's AST against MathLive's MathML output.
 * Maps Lambda's internal AST structure to MathML semantic elements.
 */

// Map Lambda AST types to MathML elements
const LAMBDA_TO_MATHML = {
    // Basic elements
    'ORD': 'mi',           // Identifier
    'BIN': 'mo',           // Binary operator
    'REL': 'mo',           // Relation
    'OP': 'mo',            // Operator (sum, int, etc.)
    'PUNCT': 'mo',         // Punctuation

    // Structures
    'FRAC': 'mfrac',       // Fraction
    'SQRT': 'msqrt',       // Square root
    'ROOT': 'mroot',       // nth root
    'SCRIPTS': 'msubsup',  // Sub/superscript
    'ROW': 'mrow',         // Row of elements
    'DELIMITED': 'mrow',   // Delimited expression

    // Special
    'OVERUNDER': 'munderover',  // Under/over operators
    'ACCENT': 'mover',          // Accent marks
    'ARRAY': 'mtable',          // Tables/matrices
};

// Map MathML elements to semantic types
const MATHML_SEMANTIC = {
    'mi': 'identifier',
    'mn': 'number',
    'mo': 'operator',
    'mfrac': 'fraction',
    'msqrt': 'radical',
    'mroot': 'radical',
    'msub': 'subscript',
    'msup': 'superscript',
    'msubsup': 'scripts',
    'munder': 'underscript',
    'mover': 'overscript',
    'munderover': 'underover',
    'mrow': 'row',
    'mtable': 'table',
    'mtr': 'table-row',
    'mtd': 'table-cell',
};

/**
 * Convert Lambda AST to semantic structure for comparison
 */
function lambdaASTToSemantic(node) {
    if (!node) return null;

    const result = {
        type: LAMBDA_TO_MATHML[node.type] || node.type?.toLowerCase(),
        value: node.value || null,
    };

    // Handle children
    if (node.body) {
        if (Array.isArray(node.body)) {
            result.children = node.body.map(lambdaASTToSemantic).filter(Boolean);
        } else {
            result.body = lambdaASTToSemantic(node.body);
        }
    }

    if (node.numer) result.numer = lambdaASTToSemantic(node.numer);
    if (node.denom) result.denom = lambdaASTToSemantic(node.denom);
    if (node.superscript) result.superscript = lambdaASTToSemantic(node.superscript);
    if (node.subscript) result.subscript = lambdaASTToSemantic(node.subscript);

    return result;
}

/**
 * Parse MathML string to semantic structure
 */
function mathmlToSemantic(mathml) {
    if (!mathml) return null;

    function parseElement(str) {
        const elements = [];
        // Match MathML elements - non-greedy matching for nested elements
        let remaining = str.trim();

        while (remaining.length > 0) {
            // Match opening tag
            const openMatch = remaining.match(/^<(m\w+)([^>]*)>/);
            if (!openMatch) break;

            const tag = openMatch[1];
            const afterOpen = remaining.slice(openMatch[0].length);

            // Find matching close tag (handling nesting)
            let depth = 1;
            let pos = 0;
            let content = '';

            while (depth > 0 && pos < afterOpen.length) {
                const nextOpen = afterOpen.indexOf(`<${tag}`, pos);
                const nextClose = afterOpen.indexOf(`</${tag}>`, pos);

                if (nextClose === -1) break;

                if (nextOpen !== -1 && nextOpen < nextClose) {
                    depth++;
                    pos = nextOpen + 1;
                } else {
                    depth--;
                    if (depth === 0) {
                        content = afterOpen.slice(0, nextClose);
                        remaining = afterOpen.slice(nextClose + `</${tag}>`.length).trim();
                    } else {
                        pos = nextClose + 1;
                    }
                }
            }

            if (depth !== 0) break; // Malformed

            const elem = {
                type: MATHML_SEMANTIC[tag] || tag,
            };

            // Check if content is text or nested elements
            if (content && !content.includes('<')) {
                elem.value = content.trim();
            } else if (content) {
                const children = parseElement(content);
                if (children.length > 0) {
                    elem.children = children;
                }
            }

            elements.push(elem);
        }

        return elements;
    }

    const parsed = parseElement(mathml);
    // Return the first (and usually only) top-level element, or wrap in root
    if (parsed.length === 1) {
        return parsed[0];
    } else if (parsed.length > 1) {
        return { type: 'row', children: parsed };
    }
    return null;
}

/**
 * Compare Lambda AST against MathML reference
 */
function compareASTToMathML(lambdaAST, mathmlRef) {
    const differences = [];
    let matchedElements = 0;
    let totalElements = 0;

    // Convert both to semantic format
    const lambdaSemantic = lambdaASTToSemantic(lambdaAST);

    // Get MathML from reference
    let mathmlSemantic;
    if (typeof mathmlRef === 'string') {
        mathmlSemantic = mathmlToSemantic(mathmlRef);
    } else if (mathmlRef.mathml) {
        mathmlSemantic = mathmlToSemantic(mathmlRef.mathml);
    } else {
        mathmlSemantic = mathmlRef;
    }

    // Compare structures
    function compareNodes(lambda, mathml, path = 'root') {
        totalElements++;

        if (!lambda && !mathml) {
            matchedElements++;
            return;
        }

        if (!lambda || !mathml) {
            differences.push({
                path,
                issue: lambda ? 'Extra element in Lambda' : 'Missing element in Lambda',
                lambda: lambda?.type || null,
                mathml: mathml?.type || null,
            });
            return;
        }

        // Compare types (with mapping)
        const lambdaType = lambda.type;
        const mathmlType = mathml.type;

        // Check if types are compatible
        const compatible = areTypesCompatible(lambdaType, mathmlType);

        if (compatible) {
            matchedElements++;
        } else {
            differences.push({
                path,
                issue: 'Type mismatch',
                lambda: lambdaType,
                mathml: mathmlType,
            });
        }

        // Compare values if present
        if (lambda.value && mathml.value) {
            if (normalizeValue(lambda.value) !== normalizeValue(mathml.value)) {
                differences.push({
                    path,
                    issue: 'Value mismatch',
                    lambda: lambda.value,
                    mathml: mathml.value,
                });
            }
        }

        // Compare children
        const lambdaChildren = getChildren(lambda);
        const mathmlChildren = mathml.children || [];

        const maxLen = Math.max(lambdaChildren.length, mathmlChildren.length);
        for (let i = 0; i < maxLen; i++) {
            compareNodes(
                lambdaChildren[i],
                mathmlChildren[i],
                `${path} > child[${i}]`
            );
        }
    }

    compareNodes(lambdaSemantic, mathmlSemantic);

    const passRate = totalElements > 0
        ? Math.round((matchedElements / totalElements) * 1000) / 10
        : 0;

    return {
        passRate,
        totalElements,
        matchedElements,
        differences: differences.slice(0, 10), // Limit to first 10 differences
    };
}

/**
 * Check if Lambda type is compatible with MathML type
 */
function areTypesCompatible(lambdaType, mathmlType) {
    if (!lambdaType || !mathmlType) return false;

    const lt = lambdaType.toLowerCase();
    const mt = mathmlType.toLowerCase();

    // Direct match
    if (lt === mt) return true;

    // Mapped matches
    const mappings = {
        'mfrac': ['fraction', 'frac'],
        'mi': ['identifier', 'ord', 'mi'],
        'mn': ['number', 'ord'],
        'mo': ['operator', 'op', 'bin', 'rel', 'punct'],
        'msqrt': ['radical', 'sqrt'],
        'mroot': ['radical', 'root'],
        'mrow': ['row', 'group', 'mrow'],
        'msubsup': ['scripts', 'msubsup'],
        'msub': ['subscript', 'sub'],
        'msup': ['superscript', 'sup'],
        'munderover': ['underover', 'overunder'],
    };

    for (const [key, values] of Object.entries(mappings)) {
        if ((key === mt || values.includes(mt)) &&
            (key === lt || values.includes(lt))) {
            return true;
        }
    }

    return false;
}

/**
 * Get all children from a node
 */
function getChildren(node) {
    if (!node) return [];

    const children = [];

    if (node.children) {
        children.push(...node.children);
    }
    if (node.body) {
        if (Array.isArray(node.body)) {
            children.push(...node.body);
        } else {
            children.push(node.body);
        }
    }
    if (node.numer) children.push(node.numer);
    if (node.denom) children.push(node.denom);
    if (node.superscript) children.push(node.superscript);
    if (node.subscript) children.push(node.subscript);

    return children.filter(Boolean);
}

/**
 * Normalize value for comparison
 */
function normalizeValue(value) {
    if (!value) return '';
    return String(value).trim().toLowerCase();
}

export {
    compareASTToMathML,
    lambdaASTToSemantic,
    mathmlToSemantic,
    areTypesCompatible,
    LAMBDA_TO_MATHML,
    MATHML_SEMANTIC,
};
