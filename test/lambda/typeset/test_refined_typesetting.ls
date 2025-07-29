#!/usr/bin/env lambda

# Test script for refined Lambda Typesetting System
# Demonstrates device-independent view tree creation and multi-format rendering

print("Testing Refined Lambda Typesetting System...")

# Create sample document content
doc_content = <document title:"Sample Research Paper">
    <metadata>
        <author>"Dr. Jane Smith"</author>
        <subject>"Mathematical Typography"</subject>
        <date>"2025-07-29"</date>
    </metadata>
    
    <heading level:1 id:"intro">"Introduction"</heading>
    
    <paragraph>
        "This paper demonstrates advanced mathematical typesetting capabilities. "
        "We examine the rendering of complex expressions such as integrals and fractions."
    </paragraph>
    
    <heading level:2>"Mathematical Examples"</heading>
    
    <paragraph>
        "Consider the following integral: "
        <math inline:true>"\\int_{-\\infty}^{\\infty} e^{-x^2} dx"</math>
        " which evaluates to "
        <math inline:true>"\\sqrt{\\pi}"</math>
        "."
    </paragraph>
    
    <math display:true>
        "\\frac{d}{dx}\\left[\\int_{a}^{x} f(t) dt\\right] = f(x)"
    </math>
    
    <paragraph>
        "The above equation demonstrates the "
        <emphasis>"Fundamental Theorem of Calculus"</emphasis>
        "."
    </paragraph>
    
    <heading level:2>"Typographical Features"</heading>
    
    <list type:"unordered">
        <item>"Device-independent coordinate system"</item>
        <item>"Precise mathematical typography"</item>
        <item>"Multiple output formats from single view tree"</item>
        <item>"Programmable document manipulation"</item>
    </list>
    
    <table>
        <header>
            <cell>"Format"</cell>
            <cell>"Precision"</cell>
            <cell>"Scalability"</cell>
        </header>
        <row>
            <cell>"SVG"</cell>
            <cell>"Vector"</cell>
            <cell>"Infinite"</cell>
        </row>
        <row>
            <cell>"PDF"</cell>
            <cell>"Vector"</cell>
            <cell>"Print Quality"</cell>
        </row>
        <row>
            <cell>"HTML"</cell>
            <cell>"CSS Layout"</cell>
            <cell>"Responsive"</cell>
        </row>
    </table>
</document>

# Test 1: Create device-independent view tree
print("\\n=== Test 1: Creating View Tree ===")

typeset_options = {
    'page_size': [595.276, 841.89],  # A4 in points
    'margins': [72, 72, 72, 72],     # 1 inch margins
    'default_font_family': 'Times New Roman',
    'default_font_size': 12,
    'line_height': 1.4,
    'math_scale': 1.0,
    'optimize_layout': true
}

# Create view tree - this is the core device-independent representation
view_tree = typeset(doc_content, typeset_options)

print("View tree created successfully!")
print("- Document size:", view_tree.document_size)
print("- Page count:", view_tree.page_count)
print("- Total nodes:", view_tree.stats.total_nodes)
print("- Text runs:", view_tree.stats.text_runs)
print("- Math elements:", view_tree.stats.math_elements)

# Test 2: Serialize view tree as Lambda element tree
print("\\n=== Test 2: Lambda Element Tree Serialization ===")

serialization_options = {
    'pretty_print': true,
    'include_positioning': true,
    'include_styling': true,
    'include_metadata': true
}

lambda_tree = serialize_to_lambda(view_tree, serialization_options)
print("Lambda element tree:")
print(lambda_tree)

# Test 3: Multiple format rendering
print("\\n=== Test 3: Multi-Format Rendering ===")

# Render to HTML
html_options = {
    'use_semantic_html': true,
    'inline_css': false,
    'generate_toc': true,
    'html_version': 'HTML5',
    'pretty_print': true
}

html_output = render(view_tree, 'html', html_options)
print("HTML output generated (", length(html_output), " characters)")

# Save HTML to file
write_file("output.html", html_output)
print("HTML saved to output.html")

# Render to SVG
svg_options = {
    'embed_fonts': true,
    'optimize_paths': true,
    'decimal_precision': 2,
    'use_viewbox': true
}

svg_output = render(view_tree, 'svg', svg_options)
print("SVG output generated (", length(svg_output), " characters)")

# Save SVG to file
write_file("output.svg", svg_output)
print("SVG saved to output.svg")

# Render to TeX/LaTeX
tex_options = {
    'doc_class': 'article',
    'use_packages': true,
    'generate_preamble': true,
    'math_mode': 'amsmath',
    'output_xelatex': true
}

tex_output = render(view_tree, 'tex', tex_options)
print("TeX output generated (", length(tex_output), " characters)")

# Save TeX to file
write_file("output.tex", tex_output)
print("TeX saved to output.tex")

# Render back to Markdown
markdown_options = {
    'flavor': 'github',
    'include_math': true,
    'inline_math_dollars': true,
    'display_math_dollars': true,
    'use_tables': true
}

markdown_output = render(view_tree, 'markdown', markdown_options)
print("Markdown output generated (", length(markdown_output), " characters)")

# Save Markdown to file
write_file("output.md", markdown_output)
print("Markdown saved to output.md")

# Test 4: View tree manipulation
print("\\n=== Test 4: View Tree Manipulation ===")

# Find specific nodes
intro_heading = view_tree_find_by_id(view_tree, "intro")
if intro_heading {
    print("Found introduction heading at position:", intro_heading.position)
    
    # Modify styling
    modify_node_style(intro_heading, {
        'color': [0, 0, 1, 1],    # Blue color
        'font_size': 18
    })
    print("Modified heading style")
}

# Find all math elements
math_nodes = view_tree_find_by_type(view_tree, VIEW_NODE_MATH_ELEMENT)
print("Found", length(math_nodes), "mathematical elements")

for math_node in math_nodes {
    print("- Math element at", math_node.position, "size:", math_node.size)
}

# Test 5: View tree analysis
print("\\n=== Test 5: View Tree Analysis ===")

stats = view_tree_calculate_stats(view_tree)
print("Updated statistics:")
print("- Memory usage:", stats.memory_usage, "bytes")
print("- Layout time:", stats.layout_time, "seconds")
print("- Total text length:", view_tree_get_total_text_length(view_tree), "characters")

bounding_box = view_tree_get_bounding_box(view_tree)
print("Bounding box:", bounding_box)

# Test 6: Page extraction and manipulation
print("\\n=== Test 6: Page Operations ===")

if view_tree.page_count > 1 {
    # Extract first page only
    first_page = view_tree_extract_pages(view_tree, 1, 1)
    print("Extracted first page")
    
    # Render first page to SVG
    page_svg = render(first_page, 'svg', svg_options)
    write_file("page1.svg", page_svg)
    print("First page saved as page1.svg")
}

# Test 7: Advanced transformations
print("\\n=== Test 7: Transformations ===")

# Create a scaled version
scale_transform = create_scale_transform(1.2, 1.2)
view_tree_apply_transform(view_tree, scale_transform)
print("Applied 1.2x scale transform")

# Render scaled version
scaled_svg = render(view_tree, 'svg', svg_options)
write_file("output_scaled.svg", scaled_svg)
print("Scaled version saved as output_scaled.svg")

print("\\n=== All Tests Completed Successfully! ===")

# Final summary
print("\\nGenerated Files:")
print("- output.html (HTML version)")
print("- output.svg (SVG version)")  
print("- output.tex (LaTeX version)")
print("- output.md (Markdown version)")
print("- page1.svg (First page only)")
print("- output_scaled.svg (Scaled version)")

print("\\nThe refined typesetting system demonstrates:")
print("1. Device-independent view tree representation")
print("2. Single layout computation, multiple output formats")
print("3. Precise mathematical typography")
print("4. Programmable document manipulation")
print("5. Lambda element tree serialization")
print("6. High-quality multi-format rendering")
