#!/usr/bin/env python3
"""
Generate an advanced test PDF with:
- Multiple fonts (built-in and custom)
- Vector graphics (lines, curves, shapes)
- Color spaces (RGB, CMYK, Gray)
- Text rendering with different styles
- Content streams with drawing commands
- Complex font dictionaries
- Images and graphics state operations
"""

from reportlab.lib.pagesizes import letter, A4
from reportlab.pdfgen import canvas
from reportlab.lib.colors import Color, red, green, blue, black, yellow, magenta, cyan
from reportlab.lib.units import inch, cm
from reportlab.platypus import SimpleDocTemplate, Paragraph, Spacer
from reportlab.lib.styles import getSampleStyleSheet, ParagraphStyle
from reportlab.graphics.shapes import Drawing, Circle, Rect, Line, Polygon
from reportlab.graphics import renderPDF
from reportlab.pdfbase import pdfmetrics
from reportlab.pdfbase.ttfonts import TTFont
import os

def create_advanced_pdf():
    """Create a comprehensive test PDF with advanced features."""
    
    output_path = "/Users/henryluo/Projects/Jubily/test/input/advanced_test.pdf"
    
    # Create canvas
    c = canvas.Canvas(output_path, pagesize=letter)
    width, height = letter
    
    # Page 1: Font showcase and text rendering
    c.setTitle("Advanced PDF Test - Fonts and Vector Graphics")
    
    # Title with large font
    c.setFont("Helvetica-Bold", 24)
    c.setFillColor(blue)
    c.drawString(50, height - 50, "Advanced PDF Test Document")
    
    # Subtitle
    c.setFont("Helvetica", 14)
    c.setFillColor(black)
    c.drawString(50, height - 80, "Testing font dictionaries, vector graphics, and content streams")
    
    # Different fonts and styles
    y_pos = height - 120
    fonts = [
        ("Helvetica", "Helvetica - Standard Sans Serif"),
        ("Helvetica-Bold", "Helvetica-Bold - Bold Sans Serif"),
        ("Helvetica-Oblique", "Helvetica-Oblique - Italic Sans Serif"),
        ("Times-Roman", "Times-Roman - Standard Serif"),
        ("Times-Bold", "Times-Bold - Bold Serif"),
        ("Times-Italic", "Times-Italic - Italic Serif"),
        ("Courier", "Courier - Monospace Font"),
        ("Courier-Bold", "Courier-Bold - Bold Monospace"),
        ("Symbol", "Symbol - Special Characters: αβγδε∑∏∫")
    ]
    
    for font_name, sample_text in fonts:
        c.setFont(font_name, 12)
        c.drawString(50, y_pos, sample_text)
        y_pos -= 20
    
    # Color showcase
    y_pos -= 20
    c.setFont("Helvetica-Bold", 14)
    c.setFillColor(black)
    c.drawString(50, y_pos, "Color Spaces Test:")
    y_pos -= 25
    
    # RGB colors
    colors = [
        (red, "RGB Red"),
        (green, "RGB Green"), 
        (blue, "RGB Blue"),
        (yellow, "RGB Yellow"),
        (magenta, "RGB Magenta"),
        (cyan, "RGB Cyan")
    ]
    
    for color, label in colors:
        c.setFillColor(color)
        c.setFont("Helvetica", 11)
        c.drawString(50, y_pos, label)
        y_pos -= 18
    
    # Vector Graphics Section
    c.setFillColor(black)
    c.setFont("Helvetica-Bold", 14)
    c.drawString(300, height - 120, "Vector Graphics:")
    
    # Basic shapes
    c.setStrokeColor(red)
    c.setFillColor(yellow)
    c.setLineWidth(2)
    
    # Rectangle with fill and stroke
    c.rect(300, height - 180, 100, 40, fill=1, stroke=1)
    
    # Circle (using Bezier curves)
    c.setStrokeColor(blue)
    c.setFillColor(green)
    c.circle(350, height - 220, 20, fill=1, stroke=1)
    
    # Line with different styles
    c.setStrokeColor(black)
    c.setLineWidth(1)
    c.line(300, height - 260, 400, height - 260)
    
    c.setLineWidth(3)
    c.setDash(6, 3)
    c.line(300, height - 270, 400, height - 270)
    
    # Bezier curve
    c.setDash()  # Reset dash
    c.setStrokeColor(magenta)
    c.setLineWidth(2)
    c.bezier(300, height - 300, 320, height - 280, 380, height - 320, 400, height - 300)
    
    # Polygon
    c.setStrokeColor(cyan)
    c.setFillColor(Color(1, 0.5, 0, alpha=0.5))  # Semi-transparent orange
    points = [(450, height - 150), (480, height - 120), (510, height - 150), 
              (500, height - 180), (460, height - 180)]
    
    path = c.beginPath()
    path.moveTo(*points[0])
    for point in points[1:]:
        path.lineTo(*point)
    path.close()
    c.drawPath(path, fill=1, stroke=1)
    
    # Text with rotation and transformation
    c.saveState()
    c.translate(450, height - 220)
    c.rotate(45)
    c.setFont("Helvetica-Bold", 12)
    c.setFillColor(red)
    c.drawString(0, 0, "Rotated Text")
    c.restoreState()
    
    # Start new page for more complex content
    c.showPage()
    
    # Page 2: Complex graphics and patterns
    c.setFont("Helvetica-Bold", 18)
    c.setFillColor(black)
    c.drawString(50, height - 50, "Page 2: Complex Vector Graphics")
    
    # Grid pattern
    c.setStrokeColor(Color(0.7, 0.7, 0.7))
    c.setLineWidth(0.5)
    for i in range(0, int(width), 20):
        c.line(i, 0, i, height)
    for i in range(0, int(height), 20):
        c.line(0, i, width, i)
    
    # Complex path with multiple subpaths
    c.setStrokeColor(blue)
    c.setFillColor(Color(0, 0, 1, alpha=0.3))
    c.setLineWidth(3)
    
    path = c.beginPath()
    # First subpath - star shape
    star_center = (150, height - 150)
    star_radius = 40
    import math
    
    for i in range(10):
        angle = i * math.pi / 5
        radius = star_radius if i % 2 == 0 else star_radius * 0.5
        x = star_center[0] + radius * math.cos(angle - math.pi/2)
        y = star_center[1] + radius * math.sin(angle - math.pi/2)
        if i == 0:
            path.moveTo(x, y)
        else:
            path.lineTo(x, y)
    path.close()
    
    # Second subpath - circle
    path.circle(300, height - 150, 30)
    
    c.drawPath(path, fill=1, stroke=1)
    
    # Gradient-like effect using multiple shapes
    for i in range(20):
        gray_level = i / 20.0
        c.setFillColor(Color(gray_level, gray_level, 1 - gray_level, alpha=0.1))
        c.circle(450, height - 150, 40 - i * 2, fill=1, stroke=0)
    
    # Text along a path (simulated)
    text = "CURVED TEXT EXAMPLE"
    radius = 80
    center = (150, height - 300)
    angle_step = 2 * math.pi / len(text)
    
    c.setFont("Helvetica-Bold", 12)
    c.setFillColor(red)
    
    for i, char in enumerate(text):
        angle = i * angle_step
        x = center[0] + radius * math.cos(angle)
        y = center[1] + radius * math.sin(angle)
        
        c.saveState()
        c.translate(x, y)
        c.rotate(math.degrees(angle + math.pi/2))
        c.drawString(-3, 0, char)
        c.restoreState()
    
    # Complex clipping path
    c.saveState()
    clip_path = c.beginPath()
    clip_path.circle(400, height - 300, 50)
    c.clipPath(clip_path)
    
    # Draw something that will be clipped
    c.setFillColor(yellow)
    c.rect(350, height - 350, 100, 100, fill=1)
    
    c.setStrokeColor(red)
    c.setLineWidth(2)
    for i in range(10):
        c.line(350 + i * 10, height - 350, 350 + i * 10, height - 250)
    
    c.restoreState()
    
    # Add metadata and document info
    c.setTitle("Advanced PDF Test")
    c.setAuthor("Lambda PDF Parser Test")
    c.setSubject("Testing PDF parsing capabilities")
    c.setKeywords(["PDF", "parsing", "fonts", "vector graphics", "test"])
    
    # Finalize the PDF
    c.save()
    print(f"Advanced PDF created: {output_path}")
    
    # Also create a simpler alternative using direct PDF commands
    create_raw_pdf_commands()

def create_raw_pdf_commands():
    """Create a second PDF with more explicit PDF commands for testing."""
    
    output_path = "/Users/henryluo/Projects/Jubily/test/input/raw_commands_test.pdf"
    
    c = canvas.Canvas(output_path, pagesize=A4)
    width, height = A4
    
    # Insert raw PDF commands through the canvas
    c.setTitle("Raw PDF Commands Test")
    
    # Page with explicit graphics state operations
    c.setFont("Helvetica", 12)
    c.drawString(50, height - 50, "Raw PDF Commands Test")
    
    # Graphics state operations
    c.saveState()
    
    # Use canvas methods for graphics state
    c.setLineCap(1)  # Round line cap
    c.setLineJoin(1)  # Round line join
    c.setMiterLimit(10)
    
    # Draw with custom graphics state
    c.setLineWidth(5)
    c.setStrokeColor(red)
    c.line(50, height - 100, 200, height - 100)
    
    c.restoreState()
    
    # Add some text with font operations
    c.setFont("Times-Roman", 14)
    c.drawString(50, height - 150, "Different font: Times-Roman")
    
    c.setFont("Courier", 10)
    c.drawString(50, height - 170, "Monospace font: Courier - 1234567890")
    
    # Color space operations
    c.setFillColorRGB(0.2, 0.8, 0.4)  # RGB color
    c.drawString(50, height - 200, "RGB Green Text")
    
    c.setFillColorCMYK(0, 1, 1, 0)  # CMYK red
    c.drawString(50, height - 220, "CMYK Red Text")
    
    # Form XObject (saved graphics state)
    c.saveState()
    c.translate(300, height - 200)
    c.scale(2, 1.5)
    c.setFillColor(blue)
    c.rect(0, 0, 50, 30, fill=1)
    c.restoreState()
    
    # More advanced graphics operations
    c.setFont("Helvetica-Bold", 16)
    c.setFillColor(black)
    c.drawString(50, height - 280, "Advanced Graphics State Tests:")
    
    # Test different blend modes and transparency
    c.saveState()
    c.setFillColor(Color(1, 0, 0, alpha=0.5))  # Semi-transparent red
    c.rect(50, height - 350, 100, 50, fill=1)
    c.setFillColor(Color(0, 0, 1, alpha=0.5))  # Semi-transparent blue
    c.rect(100, height - 350, 100, 50, fill=1)
    c.restoreState()
    
    # Test text rendering modes
    c.setFont("Helvetica-Bold", 20)
    c.setFillColor(green)
    c.setStrokeColor(red)
    c.setTextRenderMode(2)  # Fill and stroke
    c.drawString(50, height - 400, "Stroked and Filled Text")
    c.setTextRenderMode(0)  # Back to normal fill
    
    c.save()
    print(f"Raw commands PDF created: {output_path}")

if __name__ == "__main__":
    create_advanced_pdf()
