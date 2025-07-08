#!/usr/bin/env python3
"""
Generate a simpler second test PDF focusing on raw PDF features.
"""

from reportlab.lib.pagesizes import A4
from reportlab.pdfgen import canvas
from reportlab.lib.colors import Color, red, green, blue, black
import os

def create_simple_advanced_pdf():
    """Create a second PDF with focus on PDF structure and commands."""
    
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
    
    # Test different transparency levels
    c.saveState()
    c.setFillColor(Color(1, 0, 0, alpha=0.5))  # Semi-transparent red
    c.rect(50, height - 350, 100, 50, fill=1)
    c.setFillColor(Color(0, 0, 1, alpha=0.5))  # Semi-transparent blue
    c.rect(100, height - 350, 100, 50, fill=1)
    c.restoreState()
    
    # Test text with stroke
    c.setFont("Helvetica-Bold", 20)
    c.setFillColor(green)
    c.setStrokeColor(red)
    c.setLineWidth(1)
    
    # Manually create stroked text effect
    c.drawString(50, height - 400, "Bold Text with Effects")
    
    # Test various line styles
    c.setStrokeColor(black)
    y_start = height - 450
    
    # Solid line
    c.setLineWidth(2)
    c.line(50, y_start, 150, y_start)
    c.drawString(160, y_start - 5, "Solid line")
    
    # Dashed line
    c.setDash(6, 3)
    c.line(50, y_start - 20, 150, y_start - 20)
    c.drawString(160, y_start - 25, "Dashed line")
    
    # Dotted line
    c.setDash(2, 2)
    c.line(50, y_start - 40, 150, y_start - 40)
    c.drawString(160, y_start - 45, "Dotted line")
    
    # Reset dash
    c.setDash()
    
    # Complex path with curves
    c.setStrokeColor(blue)
    c.setLineWidth(3)
    
    path = c.beginPath()
    path.moveTo(300, height - 450)
    path.curveTo(320, height - 430, 340, height - 470, 360, height - 450)
    path.curveTo(380, height - 430, 400, height - 470, 420, height - 450)
    c.drawPath(path, stroke=1)
    
    c.save()
    print(f"Raw commands PDF created: {output_path}")

if __name__ == "__main__":
    create_simple_advanced_pdf()
