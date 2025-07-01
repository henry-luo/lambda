
#include "transpiler.h"
extern "C" {
Input* xml_parse(const char* xml_string);
Input* markdown_parse(const char* markdown_string);
Input* html_parse(const char* html_string);
}

void run_test_script(Runtime *runtime, const char *script, StrBuf *strbuf) {
    char path[256];
    snprintf(path, sizeof(path), "%s%s", runtime->current_dir, script);
    Item ret = run_script_at(runtime, path);
    strbuf_append_format(strbuf, "\nScript '%s' result: ", script);
    print_item(strbuf, ret);
    strbuf_append_str(strbuf, "\n");
    printf("after print_item\n");
}

void test_input() {    
    // test xml parsing
    Input* xml_input = xml_parse("<?xml version=\"1.0\"?>\n<bookstore>\n  <book id=\"1\" category=\"fiction\">\n    <title>Great Gatsby</title>\n    <author>F. Scott Fitzgerald</author>\n    <price>12.99</price>\n  </book>\n  <book id=\"2\" category=\"science\">\n    <title>Brief History of Time</title>\n    <author>Stephen Hawking</author>\n    <price>15.99</price>\n  </book>\n</bookstore>");
    LambdaItem xml_item; xml_item.item = xml_input->root;
    printf("XML parse result: %llu, type: %d\n", xml_input->root, xml_item.type_id);
    print_item(xml_input->sb, xml_input->root);
    String *xml_result = (String*)xml_input->sb->str;
    printf("XML parsed: %s\n", xml_result->chars);
    
    // test markdown parsing
    Input* markdown_input = markdown_parse("# Welcome to Markdown\n\nThis is a **bold** paragraph with *italic* text and `inline code`.\n\n## Features\n\n- First item\n- Second item with [a link](https://example.com)\n- Third item\n\n### Code Example\n\n```python\ndef hello_world():\n    print(\"Hello, World!\")\n    return True\n```\n\n---\n\nAnother paragraph after horizontal rule.");
    LambdaItem markdown_item; markdown_item.item = markdown_input->root;
    printf("Markdown parse result: %llu, type: %d\n", markdown_input->root, markdown_item.type_id);
    print_item(markdown_input->sb, markdown_input->root);
    String *markdown_result = (String*)markdown_input->sb->str;
    printf("Markdown parsed: %s\n", markdown_result->chars);

    // test html parsing with minimal example first
    printf("Testing minimal HTML...\n");
    Input* html_simple = html_parse("<div><p>Hello World</p></div>");
    if (html_simple && html_simple->root != ITEM_NULL) {
        printf("Simple HTML parsed successfully\n");
        print_item(html_simple->sb, html_simple->root);
        String *simple_result = (String*)html_simple->sb->str;
        printf("Simple HTML result: %s\n", simple_result->chars);
    } else {
        printf("Simple HTML parsing failed\n");
    }
    
    // test html parsing with embedded CSS and JavaScript
    printf("Testing HTML with embedded CSS and JS...\n");
    const char* css_js_html = "<!DOCTYPE html>\n"
        "<html>\n"
        "<head>\n"
        "    <title>CSS and JS Test</title>\n"
        "    <style>\n"
        "        body { background: #f0f0f0; font-family: Arial; }\n"
        "        .highlight { background: yellow; padding: 5px; }\n"
        "        #main { max-width: 800px; margin: 0 auto; }\n"
        "    </style>\n"
        "</head>\n"
        "<body>\n"
        "    <div id=\"main\">\n"
        "        <h1 class=\"highlight\">JavaScript Test</h1>\n"
        "        <p>This page contains embedded JavaScript.</p>\n"
        "        <button onclick=\"alert('Hello!')\">Click Me</button>\n"
        "    </div>\n"
        "    <script>\n"
        "        console.log('Page loaded');\n"
        "        function greet(name) {\n"
        "            return 'Hello, ' + name + '!';\n"
        "        }\n"
        "        document.addEventListener('DOMContentLoaded', function() {\n"
        "            console.log('DOM ready');\n"
        "        });\n"
        "    </script>\n"
        "</body>\n"
        "</html>";
    Input* css_js_input = html_parse(css_js_html);
    if (css_js_input && css_js_input->root != ITEM_NULL) {
        printf("CSS/JS HTML parsed successfully\n");
        print_item(css_js_input->sb, css_js_input->root);
        String *css_js_result = (String*)css_js_input->sb->str;
        printf("CSS/JS HTML result: %s\n", css_js_result->chars);
    } else {
        printf("CSS/JS HTML parsing failed\n");
    }
    
    // Test comprehensive HTML
    printf("Testing comprehensive HTML with CSS and JavaScript...\n");
    const char* comprehensive_html = "<!DOCTYPE html>\n"
        "<html lang=\"en\" data-theme=\"light\">\n"
        "<head>\n"
        "    <meta charset=\"UTF-8\">\n"
        "    <meta name=\"viewport\" content=\"width=device-width, initial-scale=1.0\">\n"
        "    <meta name=\"description\" content=\"HTML5 Parser Test with CSS and JS\">\n"
        "    <title>HTML5 Parser Test</title>\n"
        "    <style>\n"
        "        /* CSS embedded in HTML */\n"
        "        body {\n"
        "            font-family: 'Arial', sans-serif;\n"
        "            margin: 0;\n"
        "            padding: 20px;\n"
        "            background: linear-gradient(135deg, #667eea 0%, #764ba2 100%);\n"
        "            color: #333;\n"
        "        }\n"
        "        .container {\n"
        "            max-width: 1200px;\n"
        "            margin: 0 auto;\n"
        "            background: white;\n"
        "            border-radius: 10px;\n"
        "            box-shadow: 0 4px 6px rgba(0, 0, 0, 0.1);\n"
        "            overflow: hidden;\n"
        "        }\n"
        "        header {\n"
        "            background: #2c3e50;\n"
        "            color: white;\n"
        "            padding: 20px;\n"
        "            text-align: center;\n"
        "        }\n"
        "        .nav-menu {\n"
        "            display: flex;\n"
        "            justify-content: center;\n"
        "            list-style: none;\n"
        "            padding: 0;\n"
        "            margin: 0;\n"
        "        }\n"
        "        .nav-menu li {\n"
        "            margin: 0 15px;\n"
        "        }\n"
        "        .nav-menu a {\n"
        "            color: #ecf0f1;\n"
        "            text-decoration: none;\n"
        "            transition: color 0.3s;\n"
        "        }\n"
        "        .nav-menu a:hover {\n"
        "            color: #3498db;\n"
        "        }\n"
        "        .content {\n"
        "            padding: 30px;\n"
        "        }\n"
        "        .feature-grid {\n"
        "            display: grid;\n"
        "            grid-template-columns: repeat(auto-fit, minmax(300px, 1fr));\n"
        "            gap: 20px;\n"
        "            margin-top: 30px;\n"
        "        }\n"
        "        .feature-card {\n"
        "            border: 1px solid #ddd;\n"
        "            border-radius: 8px;\n"
        "            padding: 20px;\n"
        "            transition: transform 0.3s, box-shadow 0.3s;\n"
        "        }\n"
        "        .feature-card:hover {\n"
        "            transform: translateY(-5px);\n"
        "            box-shadow: 0 8px 15px rgba(0, 0, 0, 0.1);\n"
        "        }\n"
        "        .btn {\n"
        "            display: inline-block;\n"
        "            padding: 12px 24px;\n"
        "            background: #3498db;\n"
        "            color: white;\n"
        "            text-decoration: none;\n"
        "            border-radius: 5px;\n"
        "            border: none;\n"
        "            cursor: pointer;\n"
        "            transition: background 0.3s;\n"
        "        }\n"
        "        .btn:hover {\n"
        "            background: #2980b9;\n"
        "        }\n"
        "        .code-block {\n"
        "            background: #f8f9fa;\n"
        "            border: 1px solid #e9ecef;\n"
        "            border-radius: 4px;\n"
        "            padding: 15px;\n"
        "            margin: 15px 0;\n"
        "            overflow-x: auto;\n"
        "        }\n"
        "        .highlight {\n"
        "            background: #fff3cd;\n"
        "            padding: 2px 4px;\n"
        "            border-radius: 3px;\n"
        "        }\n"
        "        @media (max-width: 768px) {\n"
        "            .nav-menu {\n"
        "                flex-direction: column;\n"
        "            }\n"
        "            .content {\n"
        "                padding: 20px;\n"
        "            }\n"
        "        }\n"
        "    </style>\n"
        "    <link rel=\"stylesheet\" href=\"external-styles.css\">\n"
        "    <link rel=\"preconnect\" href=\"https://fonts.googleapis.com\">\n"
        "</head>\n"
        "<body>\n"
        "    <div class=\"container\">\n"
        "        <header>\n"
        "            <h1>HTML5 Parser Test Suite</h1>\n"
        "            <nav>\n"
        "                <ul class=\"nav-menu\">\n"
        "                    <li><a href=\"#home\" data-section=\"home\">Home</a></li>\n"
        "                    <li><a href=\"#features\" data-section=\"features\">Features</a></li>\n"
        "                    <li><a href=\"#examples\" data-section=\"examples\">Examples</a></li>\n"
        "                    <li><a href=\"#contact\" data-section=\"contact\">Contact</a></li>\n"
        "                </ul>\n"
        "            </nav>\n"
        "        </header>\n"
        "        \n"
        "        <main class=\"content\">\n"
        "            <section id=\"home\">\n"
        "                <h2>Welcome to the HTML5 Parser</h2>\n"
        "                <p>This is a comprehensive test of HTML5 parsing capabilities including:</p>\n"
        "                <ul>\n"
        "                    <li><strong>Semantic HTML5 elements</strong> (header, nav, main, section, article, aside, footer)</li>\n"
        "                    <li><em>Embedded CSS</em> with modern features like grid, flexbox, and media queries</li>\n"
        "                    <li><mark>Inline JavaScript</mark> with event handling and DOM manipulation</li>\n"
        "                    <li>HTML5 form elements with validation</li>\n"
        "                    <li>Multimedia elements (audio, video, canvas)</li>\n"
        "                    <li>Accessibility features (ARIA attributes)</li>\n"
        "                </ul>\n"
        "            </section>\n"
        "            \n"
        "            <section id=\"features\">\n"
        "                <h2>Parser Features</h2>\n"
        "                <div class=\"feature-grid\">\n"
        "                    <article class=\"feature-card\">\n"
        "                        <h3>HTML5 Semantic Elements</h3>\n"
        "                        <p>Supports all HTML5 semantic elements including <code>&lt;header&gt;</code>, <code>&lt;nav&gt;</code>, <code>&lt;main&gt;</code>, <code>&lt;section&gt;</code>, <code>&lt;article&gt;</code>, <code>&lt;aside&gt;</code>, and <code>&lt;footer&gt;</code>.</p>\n"
        "                    </article>\n"
        "                    \n"
        "                    <article class=\"feature-card\">\n"
        "                        <h3>CSS Integration</h3>\n"
        "                        <p>Properly handles embedded CSS in <code>&lt;style&gt;</code> tags and external stylesheets via <code>&lt;link&gt;</code> tags.</p>\n"
        "                    </article>\n"
        "                    \n"
        "                    <article class=\"feature-card\">\n"
        "                        <h3>JavaScript Support</h3>\n"
        "                        <p>Parses inline JavaScript code blocks and external script references while preserving code structure.</p>\n"
        "                    </article>\n"
        "                </div>\n"
        "            </section>\n"
        "            \n"
        "            <section id=\"examples\">\n"
        "                <h2>Code Examples</h2>\n"
        "                \n"
        "                <h3>HTML5 Form with Validation</h3>\n"
        "                <form id=\"test-form\" novalidate>\n"
        "                    <fieldset>\n"
        "                        <legend>Contact Information</legend>\n"
        "                        \n"
        "                        <label for=\"name\">Name (required):</label>\n"
        "                        <input type=\"text\" id=\"name\" name=\"name\" required aria-describedby=\"name-help\">\n"
        "                        <small id=\"name-help\">Please enter your full name</small>\n"
        "                        \n"
        "                        <label for=\"email\">Email:</label>\n"
        "                        <input type=\"email\" id=\"email\" name=\"email\" placeholder=\"user@example.com\">\n"
        "                        \n"
        "                        <label for=\"phone\">Phone:</label>\n"
        "                        <input type=\"tel\" id=\"phone\" name=\"phone\" pattern=\"[0-9]{3}-[0-9]{3}-[0-9]{4}\" placeholder=\"123-456-7890\">\n"
        "                        \n"
        "                        <label for=\"age\">Age:</label>\n"
        "                        <input type=\"number\" id=\"age\" name=\"age\" min=\"18\" max=\"120\" step=\"1\">\n"
        "                        \n"
        "                        <label for=\"website\">Website:</label>\n"
        "                        <input type=\"url\" id=\"website\" name=\"website\" placeholder=\"https://example.com\">\n"
        "                        \n"
        "                        <label for=\"birthdate\">Birth Date:</label>\n"
        "                        <input type=\"date\" id=\"birthdate\" name=\"birthdate\">\n"
        "                        \n"
        "                        <label for=\"comments\">Comments:</label>\n"
        "                        <textarea id=\"comments\" name=\"comments\" rows=\"4\" cols=\"50\" placeholder=\"Enter your comments here...\"></textarea>\n"
        "                        \n"
        "                        <label for=\"country\">Country:</label>\n"
        "                        <select id=\"country\" name=\"country\">\n"
        "                            <option value=\"\">Select a country</option>\n"
        "                            <option value=\"us\">United States</option>\n"
        "                            <option value=\"ca\">Canada</option>\n"
        "                            <option value=\"uk\">United Kingdom</option>\n"
        "                            <option value=\"de\">Germany</option>\n"
        "                            <option value=\"fr\">France</option>\n"
        "                        </select>\n"
        "                        \n"
        "                        <fieldset>\n"
        "                            <legend>Preferences</legend>\n"
        "                            <input type=\"checkbox\" id=\"newsletter\" name=\"newsletter\" value=\"yes\">\n"
        "                            <label for=\"newsletter\">Subscribe to newsletter</label>\n"
        "                            \n"
        "                            <input type=\"radio\" id=\"contact-email\" name=\"contact-method\" value=\"email\">\n"
        "                            <label for=\"contact-email\">Email</label>\n"
        "                            \n"
        "                            <input type=\"radio\" id=\"contact-phone\" name=\"contact-method\" value=\"phone\">\n"
        "                            <label for=\"contact-phone\">Phone</label>\n"
        "                        </fieldset>\n"
        "                        \n"
        "                        <button type=\"submit\" class=\"btn\">Submit Form</button>\n"
        "                        <button type=\"reset\" class=\"btn\">Reset</button>\n"
        "                    </fieldset>\n"
        "                </form>\n"
        "                \n"
        "                <h3>HTML5 Multimedia</h3>\n"
        "                <figure>\n"
        "                    <video controls width=\"300\" height=\"200\" poster=\"video-poster.jpg\">\n"
        "                        <source src=\"video.mp4\" type=\"video/mp4\">\n"
        "                        <source src=\"video.webm\" type=\"video/webm\">\n"
        "                        <p>Your browser doesn't support HTML5 video. <a href=\"video.mp4\">Download the video</a> instead.</p>\n"
        "                    </video>\n"
        "                    <figcaption>Sample video with multiple source formats</figcaption>\n"
        "                </figure>\n"
        "                \n"
        "                <figure>\n"
        "                    <audio controls>\n"
        "                        <source src=\"audio.mp3\" type=\"audio/mpeg\">\n"
        "                        <source src=\"audio.ogg\" type=\"audio/ogg\">\n"
        "                        <p>Your browser doesn't support HTML5 audio. <a href=\"audio.mp3\">Download the audio</a> instead.</p>\n"
        "                    </audio>\n"
        "                    <figcaption>Sample audio with fallback options</figcaption>\n"
        "                </figure>\n"
        "                \n"
        "                <h3>Interactive Canvas</h3>\n"
        "                <canvas id=\"demo-canvas\" width=\"400\" height=\"200\" style=\"border: 1px solid #ccc;\">\n"
        "                    Your browser does not support the HTML5 canvas element.\n"
        "                </canvas>\n"
        "                \n"
        "                <h3>Code Demonstration</h3>\n"
        "                <pre class=\"code-block\"><code>// JavaScript Example\n"
        "function initializeApp() {\n"
        "    const canvas = document.getElementById('demo-canvas');\n"
        "    const ctx = canvas.getContext('2d');\n"
        "    \n"
        "    // Draw a gradient background\n"
        "    const gradient = ctx.createLinearGradient(0, 0, 400, 200);\n"
        "    gradient.addColorStop(0, '#667eea');\n"
        "    gradient.addColorStop(1, '#764ba2');\n"
        "    \n"
        "    ctx.fillStyle = gradient;\n"
        "    ctx.fillRect(0, 0, 400, 200);\n"
        "    \n"
        "    // Add some text\n"
        "    ctx.fillStyle = 'white';\n"
        "    ctx.font = '24px Arial';\n"
        "    ctx.textAlign = 'center';\n"
        "    ctx.fillText('HTML5 Canvas Demo', 200, 100);\n"
        "}\n"
        "</code></pre>\n"
        "            </section>\n"
        "            \n"
        "            <aside>\n"
        "                <h3>Additional Features</h3>\n"
        "                <ul>\n"
        "                    <li>Data attributes: <span class=\"highlight\" data-info=\"example\">data-info=\"example\"</span></li>\n"
        "                    <li>ARIA attributes: <span role=\"button\" aria-label=\"Example button\">role=\"button\"</span></li>\n"
        "                    <li>Custom elements: <code>&lt;custom-element&gt;</code></li>\n"
        "                    <li>Web Components support</li>\n"
        "                </ul>\n"
        "                \n"
        "                <details>\n"
        "                    <summary>Advanced HTML5 Features</summary>\n"
        "                    <p>This parser also supports advanced HTML5 features like:</p>\n"
        "                    <ul>\n"
        "                        <li>Shadow DOM elements</li>\n"
        "                        <li>Template elements</li>\n"
        "                        <li>Progressive enhancement patterns</li>\n"
        "                        <li>Microdata attributes</li>\n"
        "                    </ul>\n"
        "                </details>\n"
        "                \n"
        "                <template id=\"card-template\">\n"
        "                    <div class=\"card\">\n"
        "                        <h4 class=\"card-title\"></h4>\n"
        "                        <p class=\"card-content\"></p>\n"
        "                    </div>\n"
        "                </template>\n"
        "            </aside>\n"
        "        </main>\n"
        "        \n"
        "        <footer>\n"
        "            <p>&copy; 2024 HTML5 Parser Test Suite. Built with modern web standards.</p>\n"
        "            <address>\n"
        "                Contact: <a href=\"mailto:test@example.com\">test@example.com</a>\n"
        "            </address>\n"
        "        </footer>\n"
        "    </div>\n"
        "    \n"
        "    <script>\n"
        "        // Embedded JavaScript code\n"
        "        document.addEventListener('DOMContentLoaded', function() {\n"
        "            console.log('HTML5 Parser Test Suite loaded successfully!');\n"
        "            \n"
        "            // Initialize canvas demo\n"
        "            initializeApp();\n"
        "            \n"
        "            // Add click handlers to navigation\n"
        "            const navLinks = document.querySelectorAll('.nav-menu a');\n"
        "            navLinks.forEach(link => {\n"
        "                link.addEventListener('click', function(e) {\n"
        "                    e.preventDefault();\n"
        "                    const section = this.getAttribute('data-section');\n"
        "                    console.log('Navigating to section:', section);\n"
        "                    \n"
        "                    // Smooth scroll to section\n"
        "                    const targetElement = document.getElementById(section);\n"
        "                    if (targetElement) {\n"
        "                        targetElement.scrollIntoView({ behavior: 'smooth' });\n"
        "                    }\n"
        "                });\n"
        "            });\n"
        "            \n"
        "            // Form validation\n"
        "            const form = document.getElementById('test-form');\n"
        "            if (form) {\n"
        "                form.addEventListener('submit', function(e) {\n"
        "                    e.preventDefault();\n"
        "                    \n"
        "                    const formData = new FormData(form);\n"
        "                    const data = Object.fromEntries(formData.entries());\n"
        "                    \n"
        "                    console.log('Form data:', data);\n"
        "                    alert('Form submitted successfully! Check console for data.');\n"
        "                });\n"
        "            }\n"
        "            \n"
        "            // Feature card hover effects\n"
        "            const cards = document.querySelectorAll('.feature-card');\n"
        "            cards.forEach(card => {\n"
        "                card.addEventListener('mouseenter', function() {\n"
        "                    this.style.transform = 'translateY(-10px)';\n"
        "                });\n"
        "                \n"
        "                card.addEventListener('mouseleave', function() {\n"
        "                    this.style.transform = 'translateY(0)';\n"
        "                });\n"
        "            });\n"
        "        });\n"
        "        \n"
        "        // Canvas initialization function\n"
        "        function initializeApp() {\n"
        "            const canvas = document.getElementById('demo-canvas');\n"
        "            if (!canvas) return;\n"
        "            \n"
        "            const ctx = canvas.getContext('2d');\n"
        "            if (!ctx) return;\n"
        "            \n"
        "            // Draw a gradient background\n"
        "            const gradient = ctx.createLinearGradient(0, 0, 400, 200);\n"
        "            gradient.addColorStop(0, '#667eea');\n"
        "            gradient.addColorStop(1, '#764ba2');\n"
        "            \n"
        "            ctx.fillStyle = gradient;\n"
        "            ctx.fillRect(0, 0, 400, 200);\n"
        "            \n"
        "            // Add some text\n"
        "            ctx.fillStyle = 'white';\n"
        "            ctx.font = '24px Arial';\n"
        "            ctx.textAlign = 'center';\n"
        "            ctx.fillText('HTML5 Canvas Demo', 200, 100);\n"
        "            \n"
        "            // Add interactive elements\n"
        "            canvas.addEventListener('click', function(e) {\n"
        "                const rect = canvas.getBoundingClientRect();\n"
        "                const x = e.clientX - rect.left;\n"
        "                const y = e.clientY - rect.top;\n"
        "                \n"
        "                // Draw a circle at click position\n"
        "                ctx.beginPath();\n"
        "                ctx.arc(x, y, 10, 0, Math.PI * 2);\n"
        "                ctx.fillStyle = 'rgba(255, 255, 255, 0.8)';\n"
        "                ctx.fill();\n"
        "            });\n"
        "        }\n"
        "        \n"
        "        // Utility functions\n"
        "        const Utils = {\n"
        "            // Debounce function for performance\n"
        "            debounce: function(func, wait) {\n"
        "                let timeout;\n"
        "                return function executedFunction(...args) {\n"
        "                    const later = () => {\n"
        "                        clearTimeout(timeout);\n"
        "                        func(...args);\n"
        "                    };\n"
        "                    clearTimeout(timeout);\n"
        "                    timeout = setTimeout(later, wait);\n"
        "                };\n"
        "            },\n"
        "            \n"
        "            // Theme toggle functionality\n"
        "            toggleTheme: function() {\n"
        "                const html = document.documentElement;\n"
        "                const currentTheme = html.getAttribute('data-theme');\n"
        "                const newTheme = currentTheme === 'light' ? 'dark' : 'light';\n"
        "                html.setAttribute('data-theme', newTheme);\n"
        "                localStorage.setItem('theme', newTheme);\n"
        "            },\n"
        "            \n"
        "            // Form validation helper\n"
        "            validateForm: function(form) {\n"
        "                const inputs = form.querySelectorAll('input[required]');\n"
        "                let isValid = true;\n"
        "                \n"
        "                inputs.forEach(input => {\n"
        "                    if (!input.value.trim()) {\n"
        "                        input.classList.add('error');\n"
        "                        isValid = false;\n"
        "                    } else {\n"
        "                        input.classList.remove('error');\n"
        "                    }\n"
        "                });\n"
        "                \n"
        "                return isValid;\n"
        "            }\n"
        "        };\n"
        "        \n"
        "        // Global error handler\n"
        "        window.addEventListener('error', function(e) {\n"
        "            console.error('Global error:', e.error);\n"
        "        });\n"
        "        \n"
        "        // Performance monitoring\n"
        "        if ('performance' in window) {\n"
        "            window.addEventListener('load', function() {\n"
        "                setTimeout(function() {\n"
        "                    const perfData = performance.timing;\n"
        "                    const loadTime = perfData.loadEventEnd - perfData.navigationStart;\n"
        "                    console.log('Page load time:', loadTime + 'ms');\n"
        "                }, 0);\n"
        "            });\n"
        "        }\n"
        "    </script>\n"
        "    \n"
        "    <script src=\"external-script.js\" defer></script>\n"
        "</body>\n"
        "</html>";
    
    Input* html_input = html_parse(comprehensive_html);
    LambdaItem html_item; 
    html_item.item = html_input ? html_input->root : ITEM_NULL;
    printf("HTML parse result: %llu, type: %d\n", html_input ? html_input->root : ITEM_NULL, html_item.type_id);
    
    if (html_input && html_input->root != ITEM_NULL) {
        printf("Comprehensive HTML parsed successfully\n");
        if (html_input->sb && html_input->sb->str) {
            print_item(html_input->sb, html_input->root);
            String *html_result = (String*)html_input->sb->str;
            if (html_result && html_result->chars) {
                printf("Comprehensive HTML parsed: %s\n", html_result->chars);
            }
        }
    } else {
        printf("Comprehensive HTML parsing failed\n");
    }
}

int main(void) {
    _Static_assert(sizeof(bool) == 1, "bool size == 1 byte");
    _Static_assert(sizeof(uint8_t) == 1, "uint8_t size == 1 byte");
    _Static_assert(sizeof(uint16_t) == 2, "uint16_t size == 2 bytes");
    _Static_assert(sizeof(uint32_t) == 4, "uint32_t size == 4 bytes");
    _Static_assert(sizeof(uint64_t) == 8, "uint64_t size == 8 bytes");
    _Static_assert(sizeof(int32_t) == 4, "int32_t size == 4 bytes");
    _Static_assert(sizeof(int64_t) == 8, "int64_t size == 8 bytes");
    _Static_assert(sizeof(Item) == sizeof(double), "Item size == double size");
    _Static_assert(sizeof(LambdaItem) == sizeof(Item), "LambdaItem size == Item size");
    LambdaItem itm = {.item = ITEM_ERROR};
    assert(itm.type_id == LMD_TYPE_ERROR);

    Runtime runtime;
    runtime_init(&runtime);
    runtime.current_dir = "test/lambda/";
    StrBuf *strbuf = strbuf_new_cap(256);  Item ret;
    strbuf_append_str(strbuf, "Test result ===============\n");
    run_test_script(&runtime, "value.ls", strbuf);
    run_test_script(&runtime, "expr.ls", strbuf);
    run_test_script(&runtime, "box_unbox.ls", strbuf);
    run_test_script(&runtime, "func.ls", strbuf);
    run_test_script(&runtime, "mem.ls", strbuf);
    run_test_script(&runtime, "type.ls", strbuf);
    run_test_script(&runtime, "input.ls", strbuf);

    printf("%s", strbuf->str);
    strbuf_free(strbuf);
    runtime_cleanup(&runtime);

    // Test input parsers
    // test_input();

    // mpf_t f;
    // mpf_init(f);
    // mpf_set_str(f, "5e-2", 10);  // This works!
    // gmp_printf("f = %.10Ff\n", f);  // Output: f = 0.0500000000

    // mpf_set_str(f, "3.14159", 10); 
    // gmp_printf("f = %.10Ff\n", f);

    // mpf_clear(f);
    // printf("size of mpf_t: %zu\n", sizeof(mpf_t));  

    return 0;
}