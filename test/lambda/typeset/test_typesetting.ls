# Typesetting System Test

# This Lambda script demonstrates the new typesetting system
# It shows how to create documents and render them to SVG pages

# Create a simple document with text and math
doc_content = <document>
    {
        'title': 'Test Document',
        'author': 'Lambda Typesetting System'
    }
    
    <paragraph>
        'This is a test document created with the Lambda typesetting system. '
        'It demonstrates the capability to create structured documents with '
        'text, mathematical expressions, and proper typography.'
    </paragraph>
    
    <heading level:1> 'Mathematical Examples' </heading>
    
    <paragraph>
        'Here is an inline math expression: '
        <math inline:true> 'x^2 + y^2 = z^2' </math>
        ' which demonstrates the Pythagorean theorem.'
    </paragraph>
    
    <math inline:false>
        '\\frac{d}{dx}\\left(\\sin(x)\\right) = \\cos(x)'
    </math>
    
    <paragraph>
        'The above equation shows the derivative of the sine function.'
    </paragraph>
    
    <heading level:2> 'Lists and Structure' </heading>
    
    <list ordered:true>
        <item> 'First item in the list' </item>
        <item> 'Second item with some <emphasis>emphasized text</emphasis>' </item>
        <item> 'Third item with math: <math inline:true>e^{i\\pi} + 1 = 0</math>' </item>
    </list>
    
    <paragraph>
        'This demonstrates the typesetting system\'s ability to handle '
        'complex document structures with proper formatting and layout.'
    </paragraph>
</document>

# Typeset the document with custom options
typeset_options = {
    'page_size': 'A4',
    'font_family': 'Times New Roman',
    'font_size': 12,
    'margin': 72,  # 1 inch margins
    'line_height': 1.2
}

# Render the document to SVG pages
svg_pages = typeset(doc_content, typeset_options)

# Output each page as a separate SVG file
page_num = 1
for page in svg_pages {
    filename = concat('test_document_page_', page_num, '.svg')
    output(filename, page.svg_content, 'svg')
    print('Generated page ', page_num, ': ', filename)
    page_num = page_num + 1
}

print('Typesetting complete. Generated ', len(svg_pages), ' pages.')

# Example of typesetting just a mathematical expression
math_expr = input('test_equation.math', {'type': 'math', 'flavor': 'latex'})
math_pages = typeset(math_expr, {
    'display_style': 'block',
    'font_size': 16,
    'page_size': 'Letter'
})

output('math_equation.svg', math_pages[0].svg_content, 'svg')
print('Generated mathematical expression: math_equation.svg')

# Example of typesetting from Markdown
markdown_content = '
# Test Markdown Document

This is a **bold** statement with *italic* text.

## Mathematical Content

The quadratic formula is: $x = \\frac{-b \\pm \\sqrt{b^2 - 4ac}}{2a}$

And here is a display equation:

$$\\int_{-\\infty}^{\\infty} e^{-x^2} dx = \\sqrt{\\pi}$$

### Lists

- First item
- Second item with `inline code`
- Third item

1. Numbered item one
2. Numbered item two
3. Numbered item three

### Tables

| Column 1 | Column 2 | Column 3 |
|----------|----------|----------|
| Cell 1   | Cell 2   | Cell 3   |
| Cell 4   | Cell 5   | Cell 6   |

> This is a blockquote with some important information.

```python
def hello_world():
    print("Hello, World!")
```

That concludes our test document.
'

md_pages = typeset_from_markdown(markdown_content, {
    'page_size': 'A4',
    'font_family': 'Arial',
    'font_size': 11
})

page_num = 1
for page in md_pages {
    filename = concat('markdown_page_', page_num, '.svg')
    output(filename, page.svg_content, 'svg')
    page_num = page_num + 1
}

print('Generated Markdown document with ', len(md_pages), ' pages.')

# Demonstrate LaTeX document processing
latex_content = '
\\documentclass{article}
\\usepackage{amsmath}
\\title{Sample LaTeX Document}
\\author{Lambda Typesetting System}
\\date{\\today}

\\begin{document}
\\maketitle

\\section{Introduction}
This is a sample LaTeX document processed by the Lambda typesetting system.

\\section{Mathematics}
Here are some mathematical expressions:

\\begin{equation}
E = mc^2
\\end{equation}

\\begin{align}
\\nabla \\cdot \\mathbf{E} &= \\frac{\\rho}{\\epsilon_0} \\\\
\\nabla \\cdot \\mathbf{B} &= 0 \\\\
\\nabla \\times \\mathbf{E} &= -\\frac{\\partial \\mathbf{B}}{\\partial t} \\\\
\\nabla \\times \\mathbf{B} &= \\mu_0 \\mathbf{J} + \\mu_0 \\epsilon_0 \\frac{\\partial \\mathbf{E}}{\\partial t}
\\end{align}

\\section{Conclusion}
The Lambda typesetting system successfully processes LaTeX documents.

\\end{document}
'

latex_pages = typeset_from_latex(latex_content, {
    'page_size': 'A4',
    'font_family': 'Computer Modern',
    'font_size': 10,
    'margin': 72
})

page_num = 1
for page in latex_pages {
    filename = concat('latex_page_', page_num, '.svg')
    output(filename, page.svg_content, 'svg')
    page_num = page_num + 1
}

print('Generated LaTeX document with ', len(latex_pages), ' pages.')

print('All typesetting tests completed successfully!')
